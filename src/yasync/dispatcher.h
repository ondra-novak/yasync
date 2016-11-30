#pragma once

#include <functional>
#include "refcnt.h"
#include "alertfn.h"


namespace yasync {

class Timeout;

class AbstractDispatchedFunction: public RefCntObj {
public:

	virtual void run() throw() = 0;
	virtual ~AbstractDispatchedFunction() {}

};

class AbstractDispatcher: public RefCntObj {
public:

	typedef RefCntPtr<AbstractDispatchedFunction> Fn;

	virtual bool dispatch(const Fn &fn) throw() = 0;

	virtual ~AbstractDispatcher() {}

};


template<typename Fn, typename RetV> class DispatchedFunction;
template<typename Fn> class DispatchedFunction<Fn, void> : public AbstractDispatchedFunction {
public:
	typedef bool RetT;

	virtual void run() throw() {
		fn();
	}
	DispatchedFunction(const Fn &fn) :fn(fn) {}
	
	static RetT dispatch(AbstractDispatcher *disp, const Fn &fn) {
		return disp->dispatch(new DispatchedFunction(fn));
	}
	
protected:
	Fn fn;
};

///Dispatcher function
/** Dispatcher function allows to pass a function to a different thread. The function
 * is executed in context of target thread. This require a dispatching thread (the
 * thread uses sleepAndDispatch() or haltAndDispatch() functions) The thread processes
 * the function during this calls. If multiple functions are dispatched, they are
 * queued into fifo queue. There is no way to remove function from the queue. If you need
 * such a function, you need to neutralize the function before it is executed.
 *
 *
 * The dispatcher removes all functions from the queue before exits. It also disallow
 * to pass new functions when the thread is no longer running. You can
 * use function objects to detect situation, when thread exits before it can
 * dispatch the message.
 */
class DispatchFn {
public:
	///Construct dispatch function
	DispatchFn(RefCntPtr<AbstractDispatcher> obj):obj(obj) {}
	///Retrieve dispatch function for current thread.
	static DispatchFn currentThread();


	///Dispatch a function to the target thread
	/**
	 * @param fn function to dispatch
	 * @retval true function sent to the dispatcher
	 * @retval false dispatcher is no longer connected to a thread, because the original
	 * thread exited. Function was not dispatched
	 */
	template<typename Fn>
	typename DispatchedFunction<Fn,typename std::result_of<Fn()>::type>::RetT operator>>(const Fn &fn) const throw() {
		return DispatchedFunction<Fn, typename std::result_of<Fn()>::type>::dispatch(obj, fn);
	}

	///Dispatch already prepared AbstractDispatchedFunction
	bool operator>>(const RefCntPtr<AbstractDispatchedFunction> fn) {
		return obj->dispatch(fn);
	}

	bool operator==(const DispatchFn &other) const { return obj == other.obj; }
	bool operator!=(const DispatchFn &other) const { return obj != other.obj; }

protected:
	RefCntPtr<AbstractDispatcher> obj;

};


///Dispatches an alert
/**
An alert which can be target to an function is routed through the dispatcher.
This causes, that alert is called in context of dispatching thread.

@note After destroying the thread, the alert is discarded. This may be
source of deadlock when an other thread is waiting for such alert. If you
need to handle this situation, use promises, which reports such situation
through the exception
*/
AlertFn operator>> (DispatchFn dispatcher, AlertFn target);

///sleeping for specified time which can be interrupted by an alert or dispatched function
/**
 * @param tm maximal timeout to wait
 * @param reason reason carried through the alert
 * @retval true sleep timeout
 * @retval false interrupted
 *
 * Sleeping is also interrupted by a dispatching event. If you need to dispatch
 * more events, you need to call this function in cycle.
 *
 */
bool sleepAndDispatch(const Timeout &tm, std::uintptr_t *reason = nullptr);

///Halts thread for infinite time, until alert or dispatch event happened
/**
 * @return reason carried by an alert otherwise zero.
 */
std::uintptr_t haltAndDispatch();


}
