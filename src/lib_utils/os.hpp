#pragma once

#include <string>

// process
int getPid();
bool setHighThreadPriority();
std::string getEnvironmentVariable(std::string name);

// filesystem
bool dirExists(std::string path);
void mkdir(std::string path);
void moveFile(std::string src, std::string dst);
void changeDir(std::string path);
std::string currentDir();
std::string thisExeDir();
std::string baseName(const char *path);

// dynamic library

#include <memory>

struct DynLib {
  virtual ~DynLib() = default;
  virtual void *getSymbol(const char *name) = 0;
};

std::unique_ptr<DynLib> loadLibrary(const char *name);

// shared inter-process memory

struct SharedMemory {
  virtual ~SharedMemory() = default;
  virtual void *data() = 0;
};

std::unique_ptr<SharedMemory> createSharedMemory(int size, const char *name, bool owner = false);
