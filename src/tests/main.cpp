/*
 * main.cpp
 *
 *  Created on: 10. 10. 2016
 *      Author: ondra
 */

#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <memory>
#include <fstream>
#include "testClass.h"
#include <vector>

#include "../yasync/fastmutexrecursive.h"
#include "../yasync/gate.h"
#include "../yasync/semaphore.h"
#include "../yasync/rwmutex.h"
#include <functional>
#include "../yasync/future.h"
#include "../yasync/futuredispatch.h"
#include "../yasync/checkpoint.h"
#include "../yasync/pool.h"
#include "../yasync/weakref.h"





static unsigned int timeSlice = 0;


class FNV1a {
public:
	std::uint64_t hash;

	FNV1a():hash(14695981039346656037UL) {}
	void operator()(unsigned int b) {
		hash = hash ^ (b & 0xFF);
		if (hash == 0) hash = 14695981039346656037UL;
		hash = hash * 1099511628211L;
	}

};


int main(int , char **) {
	TestSimple tst;

	{
		timeSlice = 0;
		auto d = std::chrono::milliseconds(1);
		auto n = std::chrono::steady_clock::now();		
		while (std::chrono::steady_clock::now()-n < d) {
			timeSlice++;
		}

	}

	tst.test("Thread", "testing") >> [](std::ostream &out) {
		yasync::AlertFn fin = yasync::AlertFn::thisThread();
		yasync::newThread >> [&out,&fin] {
			out << "testing";
			fin();
		};
		yasync::halt();
	};

	tst.test("Dispatch", "testing") >> [](std::ostream &out) {
		yasync::AlertFn fin = yasync::AlertFn::thisThread();
		yasync::thisThread >> [&out, &fin] {
			out << "testing";
			fin();
		};
		yasync::haltAndDispatch();
	};

	tst.test("FastMutex", "400") >> [](std::ostream &out) {
		unsigned int counter = 0;
			yasync::FastMutex mx;
		yasync::CountGate cgate(4);

		auto fn = [&] {
			for (int i = 0; i < 100; i++) {
				mx.lock();				
				unsigned int c = counter;
				c++;
				for (unsigned int c = 0; c < timeSlice; c++) std::chrono::steady_clock::now();
				counter = c;
				mx.unlock();
				for (unsigned int c = 0; c < timeSlice; c++) std::chrono::steady_clock::now();
			}
			cgate();
		};

		for (int i = 0; i < 4; i++) {
			yasync::newThread >> fn;
		}
		cgate.wait();
		out << counter;
	};

	tst.test("Dispatch thread", "0,1,2,3,4,5,6,7,8,9,done") >> [](std::ostream &out) {
		yasync::CountGate fin(10);
		yasync::DispatchFn dt = yasync::DispatchFn::newDispatchThread();
		for (unsigned int i = 0; i < 10; i++) {
			dt >> [i, &out, &fin] {
				out << i << ",";
				fin();
			};
		}
		fin.wait();
		out << "done";
	};
	tst.test("Future.directRun", "42,1") >> [](std::ostream &out) {
		yasync::Future<int> f;
		f.getPromise().setValue(42);
		std::uintptr_t myid = yasync::thisThreadId();
		std::uintptr_t hid;
		f >> [&](int x) {
			out << x;
			hid = yasync::thisThreadId();
		};
		out << "," << (myid == hid) ? 1 : 0;
	};
	tst.test("Future.inForeignThread", "42,1") >> [](std::ostream &out) {
		yasync::Future<int> f = yasync::newThread >> [] {
			yasync::sleep(100);
			return 42;
		};
		
		std::uintptr_t myid = yasync::thisThreadId();
		std::uintptr_t hid;
		f >> [&](int x) {
			out << x;
			hid = yasync::thisThreadId();
		};
		f.wait();
		out << "," << (myid != hid) ? 1 : 0;
	};
	tst.test("Future.dispatchChain", "42,1,1,0") >> [](std::ostream &out) {
		yasync::Future<int> f;
		f.getPromise().setValue(42);
		yasync::Checkpoint fin;
		std::uintptr_t myid = yasync::thisThreadId();
		std::uintptr_t hid1,hid2;
		f >> yasync::newThread >>[&](int x) {
			out << x;
			hid1 = yasync::thisThreadId();
		} >> [&] {
			hid2 = yasync::thisThreadId();
		} >> fin;
		fin.wait();
		out << "," << (myid != hid1) ? 1 : 0;
		out << "," << (myid != hid2) ? 1 : 0;
		out << "," << (hid1 != hid2) ? 1 : 0;
	};



	tst.test("Scheduler", "A:100, B:150, C:70, D:160") >> [](std::ostream &out) {
		auto n = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point endA, endB, endC,endD, start = std::chrono::steady_clock::now();
		yasync::CountGate finish;
		yasync::at(1000) >> [&] {
			endA = std::chrono::steady_clock::now();
			finish();
		};
		yasync::at(1500) >> yasync::newThread >> [&] {
			endB = std::chrono::steady_clock::now();
			finish();
		};
		yasync::at(700) >> [&] {
			endC = std::chrono::steady_clock::now();
			finish();
		};
		yasync::sleep(100);
		(finish=3).wait();
		yasync::at(100) >> [&] {
			endD = std::chrono::steady_clock::now();
			finish();
		};
		(finish=1).wait();
		out << "A:" << ((std::chrono::duration_cast<std::chrono::milliseconds>(endA - start).count() + 5) / 10)
			<< ", B:" << ((std::chrono::duration_cast<std::chrono::milliseconds>(endB - start).count() + 5) / 10)
			<< ", C:" << ((std::chrono::duration_cast<std::chrono::milliseconds>(endC - start).count() + 5) / 10)
			<< ", D:" << ((std::chrono::duration_cast<std::chrono::milliseconds>(endD - start).count() + 5) / 10);
	};

	tst.test("Pool", "10816640488088513931") >> [](std::ostream &out) {
		std::vector<std::vector<unsigned char> > buffer;
		yasync::ThreadPool poolCfg;
		yasync::Checkpoint finish;
		double left = -1.153;
		double right = -1.154;
		double top = 0.201;
		double bottom = 0.202;
		static const unsigned int sizeX = 2000;
		static const unsigned int sizeY = 2000;
		poolCfg.setFinalStop(finish);
		{
			yasync::DispatchFn pool = poolCfg.start();
			buffer.resize(sizeY);
			for (int i = 0; i < sizeY; i++) {
				std::vector<unsigned char> &row = buffer[i];
				row.resize(sizeX);
				pool >> [=, &buffer] {
					std::vector<unsigned char> &row = buffer[i];

					double y = top + ((bottom - top)*(i / double(sizeY)));
					for (int j = 0; j < sizeX; j++) {
						double x = left + ((right - left)*(j / double(sizeX)));
						double newRe = 0, newIm = 0, oldRe = 0, oldIm = 0;
						int i;
						for (i = 0; i < 255; i++)
						{
							oldRe = newRe;
							oldIm = newIm;
							double oldRe2 = oldRe * oldRe;
							double oldIm2 = oldIm * oldIm;
							if (oldRe2 + oldIm2 > 4) {
								i--; break;
							}
							newRe = oldRe2 - oldIm2+ x;
							newIm = 2 * oldRe * oldIm + y;
						}
						row[j] = (unsigned char)i;
					}
				};
			}
		}
		finish.wait();
		FNV1a hash;
		{
	/*		std::ofstream f("testimg.pgm");
			f << "P2" << std::endl;
			f << sizeX << " " << sizeY << std::endl;
			f << "256" << std::endl;*/
			for (int i = 0; i < sizeX; i++) {
				for (int j = 0; j < sizeY; j++) {
//					f << (int)buffer[i][j] << " ";
					hash(buffer[i][j]);
				}
//				f << std::endl;
			}
		}
		out << hash.hash;
	};


	return tst.didFail()?1:0;
}
