#pragma once
#include "dispatcher.h"
#include <thread>

namespace yasync {


DispatchFn newThread();


enum _XThread {
	thread
};


template<typename Fn>
void operator>>(_XThread, const Fn &fn) {
	std::thread t(fn);
	t.detach();
}



}
