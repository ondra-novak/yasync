#pragma once
#include "waitqueue.h"

namespace yasync {


///Conditional variable
/** It works similar to std::conditional_variable, except it takes minimum of system resources,
 * and allows to use up features of yasync library.
 *
 * CondVar supports both synchronous and asynchronous waiting and allows to change order of threads
 * in the waiting queue. You can also define mutex which will be used to lock internals. If your
 * internals is running complete under lock, you can use NullLock to disable locking. Note that every
 * method of CondVar must run under a lock. If you need to wait under a lock, you should use
 * unlockAndWait method.
 *
 * The class inherits WaitQueue, so some method are available from that class as well.
 *
 * @tparam Lock Class responsible to lock internals. It can be FastMutex, std::mutex, or NullLock
 */
template<typename Lock>
class CondVar: public WaitQueue<CondVar<Lock> > {
	typedef WaitQueue<CondVar<Lock> > Super;
public:


	///Construct condition variable
	/**
	 * @param lock reference to a lock object to lock internals
	 * @param lifo true will change order to lifo, default is fifo
	 */
	CondVar(Lock &lock, bool lifo = false)
		:Super(lifo?Super::lifo:Super::fifo)
		,lk(lock) {}


	///Notify one waiting thread
	/**
	 * @retval true one thread left the variable
	 * @retval no threads available
	 */
	bool notifyOne() {
		LockScope<Lock> _(lk);
		return Super::alertOne();
	}

	///Notify all waiting thread
	/**
	 * @retval true at least one thread left the variable
	 * @retval no threads available
	 */
	bool notifyAll() {
		LockScope<Lock> _(lk);
		return Super::alertAll();
	}

	///Notifies top thread but also allows some processing, before the thread leaves the queue.
	/**
	 * @param fn function which is executed with top level thread. It accepts his Ticket. During
	 *  executing this function, the lock is held, so thread cannot leave the variable (it
	 *  is at least blocked in the lock). The function should be short, it should perform a few
	 *  simple actions. The function returns true to release top thread, or false, to return
	 *  without release
	 *
	 * @retval true one thread left the variable
	 * @retval false no thread, or the function returned without releasing the thread
	 */
	template<typename Fn>
	bool notifyOne(const Fn &fn) {
		LockScope<Lock> _(lk);
		if (Super::top) {
			bool ntf = fn(*Super::top);
			if (ntf) {
				return Super::alertOne();
			}
		}
		return false;
	}



protected:

	friend class WaitQueue<CondVar>;

	void onSubscribe(typename Super::Ticket &t) {
		LockScope<Lock> _(lk);
		Super::add(t);
	}

	bool onSignoff(typename Super::Ticket &t) {
		LockScope<Lock> _(lk);
		return Super::remove(t);
	}

	Lock &lk;

};

}
