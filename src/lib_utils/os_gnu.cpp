#include "os.hpp"
#include <stdexcept>

using namespace std;

#include <pthread.h>
#include <sys/stat.h> // mode constants
#include <fcntl.h>    // O_CREAT
#include <unistd.h>   // chdir, getpid
#include <dlfcn.h>    // dlopen
#include <libgen.h>   // dirname
#include <sys/mman.h>

int getPid() {
	return getpid();
}

bool setHighThreadPriority() {
	sched_param sp {};
	sp.sched_priority = 1;
	if (pthread_setschedparam(pthread_self(), SCHED_RR, &sp))
		return false;

	return true;
}

std::string getEnvironmentVariable(string name) {
	const char* value = std::getenv(name.c_str());
	if(!value)
		value = "";
	return value;
}

bool dirExists(string path) {
	struct stat sb;
	return stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

void mkdir(string path) {
	if(::mkdir(path.c_str(), 0755) != 0)
		throw runtime_error("couldn't create dir \"" + path + "\": please check you have sufficient permissions");
}

void moveFile(string src, string dst) {
	if(rename(src.c_str(), dst.c_str()))
		throw runtime_error("can't move file");
}

void changeDir(string path) {
	if (chdir(path.c_str()) < 0)
		throw runtime_error("can't change to dir '" + path + "'");
}

string currentDir() {
	shared_ptr<char> path(get_current_dir_name(), &free);
	if(!path)
		throw runtime_error("couldn't get the current directory");
	return path.get() + string("/");
}

string thisExeDir() {
	char buffer[4096] {};
	auto r = readlink("/proc/self/exe", buffer, sizeof buffer);
	if(r == -1)
		throw runtime_error("can't read link /proc/self/exe");

	buffer[r] = 0;

	return dirname(buffer) + string("/");
}

struct DynLibGnu : DynLib {
	DynLibGnu(const char* name) : handle(dlopen(name, RTLD_NOW | RTLD_NODELETE)) {
		if(!handle) {
			string msg = "can't load '";
			msg += name;
			msg += "' (";
			msg += dlerror();
			msg += ")";
			throw runtime_error(msg);
		}
	}

	~DynLibGnu() {
		dlclose(handle);
	}

	virtual void* getSymbol(const char* name) {
		auto func = dlsym(handle, name);
		if(!func) {
			string msg = "can't find symbol '";
			msg += name;
			msg += "'";
			throw runtime_error(msg);
		}

		return func;
	}

	void* const handle;
};

unique_ptr<DynLib> loadLibrary(const char* name) {
	return make_unique<DynLibGnu>(name);
}

struct SharedMemRWCGnu : SharedMemory {
	SharedMemRWCGnu(int size, const char* name) : filename(name), size(size) {
		fd = shm_open(name, (O_CREAT | O_RDWR), (S_IRUSR | S_IWUSR));
		if (fd == -1) {
			string msg = "SharedMemRWCGnu: shm_open could not create \"";
			msg += name;
			msg += "\"";
			throw runtime_error(msg);
		}
		int rc = ftruncate(fd, size);
		if(rc == -1)
			throw runtime_error("SharedMemRWCGnu: shm_open can't set map size");
		ptr = mmap(0, size, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0);
		if (ptr == MAP_FAILED) {
			string msg = "SharedMemRWCGnu: mmap could not create for name \"";
			msg += name;
			msg += "\"";
			throw runtime_error(msg);
		}
	}

	~SharedMemRWCGnu() {
		munmap(ptr, size);
		close(fd);
		shm_unlink(filename.c_str());
	}

	void* data() override {
		return ptr;
	}

	int fd;
	string filename;
	int size;
	void *ptr;
};

unique_ptr<SharedMemory> createSharedMemory(int size, const char* name) {
	return make_unique<SharedMemRWCGnu>(size, name);
}
