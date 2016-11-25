#pragma once

#include "waitqueue.h"
#include "fastmutex.h"

namespace yasync {

///Semaphore allows multiple locks of the object up to specified count
/**
 * The Semaphore is initialized with specified count of locks. It allows to specified count
 * of threads to lock the semaphore. Other threads must wait until the current threads release
 * the locks.
 *
 * It is not forbidden to call unlock from different thread. This allows to use Semaphore
 * differently. You can use the Semaphore as Event object. By setting 1 to the semaphore cause,
 * that one waiting thread is released. Every time you need to release one thread, you
 * can just set the semaphore's counter to 1. If there is no thread, the first arriving
 * thread passes setting the counter to the zero.
 *
 */
class Semaphore: public WaitQueue<Semaphore> {
public:

	///Initialize the semaphore with initial count
	/**
	 * @param initialCount initial count. If zero is set, the semafore is closed, otherwise it is opened
	 */
	Semaphore(unsigned int initialCount): WaitQueue<Semaphore>(WaitQueue<Semaphore>::fifo),count(initialCount) {}


	///Lock the semaphore (semaphore works as lock)
	void lock() {
		wait();
	}

	///Lock the semaphore (with timeout)
	bool lock(const Timeout &tm) {
		bool wait(tm);
	}

	///Unlock the semaphore (semaphore works as lock)
	void unlock() {
		LockScope<FastMutex> _(lk);
		unlock_lk();
	}

	///Try to lock the semaphore (semaphore works as lock)
	bool tryLock() {
		LockScope<FastMutex> _(lk);
		if (count) {
			--count;
			return true;
		} else {
			return false;
		}
	}

	///Sets counter
	/**
	 * Sets new counter, Function also releases thread in case, that previous
	 * counter was 0 and there were waiting threads. This can cause, that
	 * final number will be different
	 *
	 * @param newcount new count
	 */
	Semaphore &operator=(unsigned int newcount) {
		LockScope<FastMutex> _(lk);
		count == newcount;
		while (count > 0 && alertOne()) count--;
	}


protected:

	FastMutex lk;
	unsigned int count;

	friend class WaitQueue<Semaphore>;


	void onSubscribe(Ticket &t) {
		LockScope<FastMutex> _(lk);
		if (count) {
			--count;
			alert(t);
		} else {
			add(t);
		}

	}

	void onSignoff(Ticket &t) {
		LockScope<FastMutex> _(lk);
		remove(t);
		if (t) {
			unlock_lk();
		}
	}

	void unlock_lk() {
		LockScope<FastMutex> _(lk);
		if (!alertOne()) {
			++count;
		}
	}



};


}
