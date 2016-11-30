/*
 * sleepingobject.h
 *
 *  Created on: 29.10.2010
 *      Author: ondra
 */

#include <cstdint>

#include "refcnt.h"
#pragma once

namespace yasync {

///Objects that are able to wait for an event
/**

 */
class AbstractAlertFunction: public RefCntObj {
public:

	///Notifies about event and wake ups the waiting object
	/**
	 * If this object is Thread, function resumes the sleeping thread. If
	 * thread doesn't sleep, it records this event and prevent sleeping
	 * on next sleep() call.
	 *
	 * If object is not Thread, behavior depends on implementation. Function
	 * is designed to send notification between the threads.
	 *
	 * Function should not process long-term tasks. It should finish it work
	 * as soon as possible. In most of cases function releases a semaphore
	 * or unblocks the event object.
	 *
	 * @param reason optional argument associated with the request. It can
	 * carry information about reason of wake up. If not specified, it has
	 * value zero. Also target object can ignore this value when it don't need it.
	 *
	 * @note threads should not be waken up with reason, because storing
	 * and carrying reason is not MT safe and it can be lost during processing.
	 */
	virtual void wakeUp(const std::uintptr_t *reason = nullptr) throw() = 0;

	///Virtual destructor need to correct destruction through this interface
	virtual ~AbstractAlertFunction() {}
};


///Wraps AbstractAlertFunction into function
/** AlertFn is function which can be used to alert sleeping thread, or
 * any other object which can be alerted.
 */
class AlertFn
{
public:
	///Construct AlertFn using existing AbstractAlertFunction object
	/** it is ok to construct AlertFn with nullptr. Such alert variable will not generate alert */
	AlertFn(RefCntPtr<AbstractAlertFunction> obj):obj(obj) {}
	///Create alert function which wakes current thread
	/**
	  @return function which wakes current thread. Function can
	  be used from other thread.
	*/
	static AlertFn currentThread();
	
	///Create alert which calls specified function
	/**
	  @param fn function. The function must have one argument - pointer to
	  std::uintptr_t which carries a reason, if supplied. Pointer can be
	  null in case when no reason has been supplied 
	  
	  @return alert function

	  @note such alert is executed in context of caller. If you need
	  to call it in the context of other thread, you need to pass the
	  alert through the dispatcher. See DispatchFn
	  */
	template<typename Fn>
	static AlertFn callFn(const Fn &fn) {
		class F : public AbstractAlertFunction {
		public:
			virtual void wakeUp(const std::uintptr_t *reason) throw() {
				fn(reason);
			}
			F(const Fn &fn) :fn(fn) {}
		protected:
			Fn fn;
		};
		return AlertFn(RefCntPtr<AbstractAlertFunction>(new F(fn)));
	}
	


	///Make alert without reason
	void operator()() const throw() {
		if (obj != nullptr) obj->wakeUp();
	}
	///Make alert with a reason
	/**
	 * @param reason reason carried with the alert
	 *
	 * @note function cannot guarantee that reason will be delivered. It depends
	 * on target implementation
	 */
	void operator()(std::uintptr_t reason) const throw() {
		if (obj != nullptr) obj->wakeUp(&reason);
	}

	bool operator==(const AlertFn &other) const { return obj == other.obj; }
	bool operator!=(const AlertFn &other) const { return obj != other.obj; }

protected:
	RefCntPtr<AbstractAlertFunction> obj;


};

class Timeout;
///Makes current thread sleep, until it is alerted
/**
 * @param tm timeout defines when the sleeping ends
 * @param reason when thread is alerted, the variable receives the reason. If argument
 * is nullptr, no reason is stored. When reason is stored, it is also internally
 * reset to zero.
 *
 * @retval true sleeping successful, no alert happened
 * @retval false alerted, reason stored
 */
bool sleep(const Timeout &tm, std::uintptr_t *reason = nullptr) ;

///Halts current thread until alert is triggered
/**
function is equivalent to sleep(nullptr) but contains less code.

@return function returns reason. 
*/
std::uintptr_t halt();


///Returns this thread identificator
/**
 * The function returns a number, which is associated with current thread.
 *
 *
 * @return
 */
std::uintptr_t thisThreadId();


}


