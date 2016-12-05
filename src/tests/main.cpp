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





static unsigned int timeSlice = 0;

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
	tst.test("Thread.alert", "testing") >> [](std::ostream &out) {
		bool done = false;
		yasync::AlertFn fin = yasync::AlertFn::thisThread() >> [&done] {done = true; };
		yasync::newThread >> [&out, &fin] {
			out << "testing";
			fin();
		};
		while (!done) {
			yasync::halt();
		}
	};
	tst.test("Thread.alert", "42") >> [](std::ostream &out) {
		bool done = false;
		uintptr_t reason;
		yasync::AlertFn fin = yasync::AlertFn::thisThread()
					>> [&done, &reason](uintptr_t r) {reason = r; done = true; };
		yasync::newThread >> [&out, &fin] {
			fin(42);
		};
		while (!done) {
			yasync::halt();
		}
		out << reason;
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

	tst.test("Pool", "A:100, B:150, C:70, D:160") >> [](std::ostream &out) {
		std::vector<std::vector<unsigned char> > buffer;
		yasync::ThreadPool poolCfg;
		yasync::Checkpoint finish;
		double left = -1.154;
		double right = -1.155;
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
						for (i = 0; i < 256; i++)
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
		unsigned char xchk = 0;
		{
			std::ofstream f("testimg.pgm");
			f << "P2" << std::endl;
			f << sizeX << " " << sizeY << std::endl;
			f << "255" << std::endl;
			std::ostringstream tmp;
			for (int i = 0; i < sizeX; i++) {
				for (int j = 0; j < sizeY; j++) {
					f << (int)buffer[i][j] << " ";
					xchk ^= (int)buffer[i][j];
				}
				f << std::endl;
			}
		}
		out << xchk;	
	};



		return tst.didFail()?1:0;
}
