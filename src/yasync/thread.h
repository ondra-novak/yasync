#pragma once
#include "dispatcher.h"
#include <thread>

namespace yasync {


DispatchFn newThread();


enum _XThread {
	thread
};

template<typename Fn, typename RetV>
struct RunThreadFn;

template<typename Fn>
struct RunThreadFn<Fn, void> {
	typedef void ReturnType;
	static void runThread(const Fn &fn) {
		std::thread t(fn);
		t.detach();
	}
};


template<typename Fn>
auto operator>>(_XThread, const Fn &fn) -> typename RunThreadFn<Fn, decltype((*(Fn *)nullptr)())>::ReturnType {
	return RunThreadFn<Fn,decltype((*(Fn *)nullptr)())>::runThread(fn);
}



}
