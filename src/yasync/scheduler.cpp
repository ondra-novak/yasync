/*
 * scheduler.cpp
 *
 *  Created on: 1. 12. 2016
 *      Author: ondra
 */

#include "scheduler.h"

#include "lockScope.h"
namespace yasync {

Scheduler::Scheduler():workerAlert(nullptr),running(false) {
	// TODO Auto-generated constructor stub

}

DispatchFn Scheduler::operator ()(const Timeout& tm) {

	RefCntPtr<AbstractDispatcher> x(new ScheduledFn(tm, this));
	return x;
}

void Scheduler::enqueue(RefCntPtr<ScheduledFn> fn) {

	LockScope<FastMutex> _(lk);
	queue.push(fn);
	if (!running) {
		newThread >> [this] {
			this->runWorker();
		};
		running = true;
	} else {
		AlertFn a = workerAlert;
		a();
	}
}

void Scheduler::runWorker() {
	LockScope<FastMutex> _(lk);
	workerAlert = AlertFn::currentThread();
	Timeout tm = timeOfNextEvent();
	while (tm != Timeout::infinity) {
		Timeout now = Timeout::now();
		if (tm <= now) {
			runFirstEvent();
		} else {
			UnlockScope<FastMutex> _(lk);
			sleep(tm);
		}
	}
	running = false;
}

Timeout Scheduler::timeOfNextEvent() const {
	if (queue.empty()) return Timeout::infinity;
	RefCntPtr<ScheduledFn> t = queue.top();
	return t->getTime();
}

void Scheduler::runFirstEvent() {
	RefCntPtr<ScheduledFn> t = queue.top();
	queue.pop();
	UnlockScope<FastMutex> _(lk);
	return t->runScheduled();
}

bool Scheduler::ScheduledFn::dispatch(const Fn& fn) throw ()
{
	LockScope<FastMutex> _(lk);
	switch (state) {
	case initializing: this->fn = fn;
					this->owner->enqueue(this);
					this->state = queued;
					return true;
	case queued: this->fn = fn;
				 return true;
	default:
	case fired: return false;
	}
}

void Scheduler::ScheduledFn::runScheduled() throw() {

	Fn f;
	{
		LockScope<FastMutex> _(lk);
		state = fired;
		f = this->fn;
	};
	f->run();
}


Scheduler at;


}
