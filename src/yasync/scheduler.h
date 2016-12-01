/*
 * scheduler.h
 *
 *  Created on: 1. 12. 2016
 *      Author: ondra
 */

#pragma once
#include <map>
#include <queue>

#include "dispatcher.h"
#include "fastmutex.h"
namespace yasync {



///Scheduler handles scheduling of task that are represented as functions.
/**
 * Scheduler allows to schedule execution of a function. The object combines DispatchFn with ability
 * to schedule the dispatch.
 *
 * Scheduler acts as function, which accepts Timeout, which defines when the function will be scheduled. The function
 * is executed once Timeout expires. Result of this function is DispatchFn object. You can use operator >> to
 * set function to dispatch. Once the function is dispatched, the executing is scheduled. It is allowed to
 * change the function anytime later before the timeout expires. This allows to for example camcel the scheduled function. You
 * can simple dispatch a function with empty body as cancelation.
 *
 */
class Scheduler {
public:
	Scheduler();


	///Create a dispatcher which is scheduled specified time
	/**
	 *
	 * Dispatcher is scheduled once the function is dispatched. Dispatched function is executed at specified time
	 * on any time later, or immediately, if the timeout already elapsed. Execution is always processed in the scheduler's
	 * thread. The scheduling or execution will happened after the function is dispatched. Dispatching other function before
	 * execution causes, that current function is canceled and new function is prepared for execution. Dispatching
	 * function after execution will reject the dispatching
	 *
	 * @param tm specified timeout after the function is dispatched
	 * @return dispatcher
	 */
	DispatchFn operator()(const Timeout &tm);

protected:

	class ScheduledFn: public AbstractDispatcher {
	public:

		enum State {
			initializing,
			queued,
			fired
		};

		ScheduledFn(const Timeout &tm, Scheduler *owner):state(initializing),owner(owner),tm(tm) {}
		virtual bool dispatch(const Fn &fn) throw();
		void runScheduled() throw();
		const Timeout &getTime() const {return tm;}

	protected:
		Fn fn;
		FastMutex lk;
		State state;
		Scheduler *owner;
		Timeout tm;
	};

	struct CompareItems {
		bool operator()(const RefCntPtr<ScheduledFn> &a,const  RefCntPtr<ScheduledFn> &b) const;
	};

	typedef std::priority_queue<RefCntPtr<ScheduledFn>,
					std::vector<RefCntPtr<ScheduledFn> >,
					CompareItems> TimeQueue;
	TimeQueue queue;
	FastMutex lk;
	AlertFn workerAlert;
	bool running;

	void runWorker();
	Timeout timeOfNextEvent() const;
	void runFirstEvent();
	void enqueue(RefCntPtr<ScheduledFn> fn);


};

extern Scheduler at;


} /* namespace yasync */


