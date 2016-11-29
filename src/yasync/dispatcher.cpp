/*
 * dispatcher.cpp
 *
 *  Created on: 25. 11. 2016
 *      Author: ondra
 */


#include "dispatcher.h"

#include "lockScope.h"
#include <deque>

#include "fastmutex.h"
namespace yasync {

class Dispatcher: public AbstractDispatcher {
public:

	Dispatcher(const AlertFn &alert):alert(alert),opened(true) {}
	virtual bool dispatch(const Fn &fn) throw();
	virtual bool sleep(const Timeout &tm, std::uintptr_t *reason);
	virtual std::uintptr_t halt();


	void close();

protected:
	FastMutex lk;
	AlertFn alert;
	std::deque<Fn> fnqueue;
	bool opened;

};

class DispatcherControl: public RefCntPtr<Dispatcher> {
public:
	DispatcherControl() {}
	DispatcherControl& operator=(Dispatcher *p) {
		RefCntPtr<Dispatcher>::operator=(p);
		return *this;
	}
	~DispatcherControl() {
		if (this->ptr) this->ptr->close();
	}
};


static thread_local DispatcherControl curDispatcher;

static RefCntPtr<Dispatcher> getCurrentDispatcher()  {
	RefCntPtr<Dispatcher> x = curDispatcher;
	if (x == nullptr) {
		x = curDispatcher = new Dispatcher(AlertFn::currentThread());
	}
	return curDispatcher;
}

DispatchFn DispatchFn::currentThread() {
	RefCntPtr<AbstractDispatcher> x = RefCntPtr<AbstractDispatcher>::staticCast(getCurrentDispatcher());
	return DispatchFn(x);
}


bool sleepAndDispatch(const Timeout& tm, std::uintptr_t* reason) {
	return getCurrentDispatcher()->sleep(tm,reason);
}

std::uintptr_t haltAndDispatch() {
	return getCurrentDispatcher()->halt();
}


bool Dispatcher::dispatch(const Fn& fn) throw () {
	LockScope<FastMutex> _(lk);
	if (!opened) return false;
	bool e = fnqueue.empty();
	fnqueue.push_front(fn);
	if (e) {
		alert();
	}
	return true;
}

bool Dispatcher::sleep(const Timeout& tm, std::uintptr_t* reason) {
	lk.lock();
	if (fnqueue.empty()) {
		lk.unlock();
		if (::yasync::sleep(tm,reason)) return true;
		lk.lock();
	}
	if (!fnqueue.empty()) {
		Fn fn = fnqueue.front();
		fnqueue.pop_back();
		lk.unlock();
		fn->run();
	} else {
		lk.unlock();
	}
	return false;

}

std::uintptr_t Dispatcher::halt() {
	std::uintptr_t reason = 0;
	lk.lock();
	if (fnqueue.empty()) {
		lk.unlock();
		reason = ::yasync::halt();
		lk.lock();
	}
	if (!fnqueue.empty()) {
		Fn fn = fnqueue.front();
		fnqueue.pop_back();
		lk.unlock();
		fn->run();
	} else {
		lk.unlock();
	}
	return reason;
}

void Dispatcher::close() {
	LockScope<FastMutex> _(lk);
	opened = false;
	fnqueue.clear();


}

AlertFn operator >> (DispatchFn  dispatcher, AlertFn target)
{
	return AlertFn::call([dispatcher, target](const std::uintptr_t *reason) {
		if (reason) {
			std::uintptr_t reasonValue = *reason;
			dispatcher >> [target,reasonValue] {
				target(reasonValue);
			};
		}
		else {
			dispatcher >> [target]{
				target();
			};
		}
	});
}


}
