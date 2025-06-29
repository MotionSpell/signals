#pragma once

#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/helper_input.hpp" // Modules::Input
#include <thread>
#include <condition_variable>
#include <mutex>

namespace {

struct Passthru : public Modules::ModuleS {
	Passthru(Modules::KHost*) {
		addOutput();
	}
	void processOne(Modules::Data) override {
	}
};

struct InfiniteSource : Modules::Module {
	InfiniteSource(Modules::KHost* host) {
		out = addOutput();
		host->activate(true);
	}
	void process() override {
		out->post(out->allocData<Modules::DataRaw>(1));
	}
	Modules::OutputDefault* out;
};

struct FakeSource : Modules::Module {
	FakeSource(Modules::KHost* host, int maxNumRepetition = 50) : numRepetition(maxNumRepetition), host(host) {
		out = addOutput();
		host->activate(true);
	}
	void process() override {
		out->post(out->allocData<Modules::DataRaw>(1));
		if(--numRepetition <= 0)
			host->activate(false);
	}
	int numRepetition;
	Modules::KHost* host;
	Modules::OutputDefault* out;
};

struct FakeSink : public Modules::ModuleS {
	FakeSink(Modules::KHost*) {
	}
	void processOne(Modules::Data) override {
	}
};

class DualInput : public Modules::Module {
	public:
		DualInput(Modules::KHost*) {
			input0 = (Modules::Input*)addInput();
			input1 = (Modules::Input*)addInput();
			out = addOutput();
		}

		void process() {
			if (!got0 || !got1) {
				if(!got0) {
					Modules::Data data;
					got0 = input0->tryPop(data);
				}
				if(!got1) {
					Modules::Data data;
					got1 = input1->tryPop(data);
				}
				if(got0 && got1)
					out->post(out->allocData<Modules::DataRaw>(1));
			}

			input0->clear();
			input1->clear();
		}

	private:
		bool got0 = false;
		bool got1 = false;
		Modules::Input* input0;
		Modules::Input* input1;
		Modules::OutputDefault* out;
};

}

class ThreadedDualInput : public Modules::Module {
	public:
		ThreadedDualInput(Modules::KHost*) {
			input0 = (Modules::Input*)addInput();
			input1 = (Modules::Input*)addInput();
			addOutput();
			numCalls = 0;
			workingThread = std::thread(&ThreadedDualInput::threadProc, this);
		}

		virtual ~ThreadedDualInput() {
			if (workingThread.joinable()) {
				for(auto& input : inputs)
					input->push(nullptr);
				workingThread.join();
			}
		}

		void process() {
		}

		void flush() {
			std::unique_lock<std::mutex> lock(m_protectDone);
			flushed.wait(lock, [this]() {
				return done;
			});
		}

		void threadProc() {
			numCalls++;

			if (!done) {
				input0->pop();
				input1->pop();
			}

			{
				std::unique_lock<std::mutex> lock(m_protectDone);
				done = true;
				flushed.notify_one();
			}

			input0->clear();
			input1->clear();
		}

		static uint64_t numCalls;

	private:
		bool done = false;
		std::thread workingThread;
		std::mutex m_protectDone;
		std::condition_variable flushed;
		Modules::Input* input0;
		Modules::Input* input1;
};

