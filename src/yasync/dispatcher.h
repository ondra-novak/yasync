#pragma once

#include <functional>
#include <thread>
#include "refcnt.h"
#include "alertfn.h"


namespace yasync {

	enum _XNewThread {
		newThread
	};

	enum _XThisThread {
		thisThread
	};


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
	///Retrieves dispatch function for current thread.
	static DispatchFn thisThread();
	///Retrieves dispatch function which will always create a new thread
	static DispatchFn newThread();
	///Creates special thread which is able to dispatch functions. 
	/** Functions are dispatched through a queue. The last reference of this
	    thread destroys the thread, but after all messages are dispatched */
	static DispatchFn newDispatchThread();


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
	bool operator>>(const AbstractDispatcher::Fn &fn) const {
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

///Route the dispatching through multiple dispatchers
/**
 @param first first dispatcher, which receives function to dispatch. If dispatcher
 rejects the request, dispatching fails. 
 @param second second dispatcher, where the function will be routed to dispatch, If
  dispatcher fails, the function executes in context of first dispatcher
 @return DispatchFn which handles whole routing.

 You can for example route function from the scheduler to a specified thread 

 @code
 yasync::at(1000) >> mythread >> [] { std::cout << "Hello"; };
 @endcode

 Above example will at given time dispatches function at the right side through the mythread

*/
DispatchFn operator >> (DispatchFn first, DispatchFn second);

///Dispatch function from the first dispatcher to the new thread
/**
@note This is faster equivalent of first >> DispatchFn::newThreaad()
*/
DispatchFn operator >> (DispatchFn first, _XNewThread);

///Dispatch function from the first dispatcher to the current thread
/**
@param first first dispatcher.

The "current thread" is evaluated in context of this call, not context of the dispatcher. Operator
is able to post function back to current thread. The current thread need to execute dispatching
functions time to time.

@note This is faster equivalent of first >> DispatchFn::thisThread()
*/
DispatchFn operator >> (DispatchFn first, _XThisThread);

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
auto operator >> (_XNewThread, const Fn &fn) -> typename RunThreadFn<Fn, typename std::result_of<Fn()>::type>::ReturnType {
	return RunThreadFn<Fn, typename std::result_of<Fn()>::type>::runThread(fn);
}

template<typename Fn>
auto operator >> (_XThisThread, const Fn &fn) {
	return DispatchFn::thisThread() >> fn;
}


}
