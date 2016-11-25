#pragma once

#include "fastmutex.h"
#include "waitqueue.h"
#include "lockScope.h"

namespace yasync {

///Gate is object which can be either opened or closed
/**
 * Gate allows to threads pass through when it is opened. The gate can be closed, which
 * starts to block the threads until it is opened again.  The gate can be manually opened or
 * closed
 */
class Gate: public WaitQueue<Gate> {
	typedef WaitQueue<Gate> Super;
public:

	///Construct the gate closed by default
	Gate():Super(Super::fifo),opened(false) {}
	///Construct the gate in specified state
	/**
	 * @param opened initial state
	 */
	explicit Gate(bool opened):Super(Super::fifo),opened(opened) {}


	///Open the gate
	/** Function opens the gate by releasing all threads inside. The gate remains
	 * opened allowing other threads to pass through
	 */
	void open() throw() {
		LockScope<FastMutex> _(lock);
		if (!opened) {
			opened = true;
			Super::alertAll();
		}
	}
	///Close the gate
	/** Closes the gate, no more threads can pass */
	void close() throw() {
		LockScope<FastMutex> _(lock);
		opened = false;
	}

	///Open and close gate
	/** Releases all threads waiting on gate, but the gate remains closed */
	void pulse() throw() {
		LockScope<FastMutex> _(lock);
		Super::alertAll();
	}

	///Sets state
	void setState(bool state) {
		if (state) open();else close();
	}

	///Sets state
	Gate &operator=(bool x) {setState(x);return *this;}

	bool operator!() const {return !opened;}
	operator bool() const {opened;}

	Gate(const Gate &) = delete;
	Gate &operator=(const Gate &) = delete;

protected:

	FastMutex lock;
	bool opened;

	void onSubscribe(Ticket &t) {
		LockScope<FastMutex> _(lock);
		if (opened) {
			alert(t);
		} else {
			add(t);
		}
	}

	void onSignoff(Ticket &t) {
		LockScope<FastMutex> _(lock);
		remove(t);
	}

	friend class WaitQueue<Gate>;

};

///Counting gate - opens when the counter reaches specified number
class CountGate: public WaitQueue<CountGate>  {
	typedef WaitQueue<CountGate> Super;
public:

	///Initialize counting gate. Set the counter
	/**
	 * @param initCount initial count. If this argument is zero, then gate is initialy open.
	 * Setting other number closes gate and starts counting for events
	 */
	CountGate(unsigned int initCount)
		:Super(Super::fifo)
		,curCount(initCount) {}

	CountGate(const CountGate &) = delete;
	CountGate &operator=(const CountGate &) = delete;

	///Sets new counter
	CountGate &operator=(unsigned int count) {
		LockScope<FastMutex> _(lock);
		curCount = count;
		if (count == 0) {
			Super::alertAll();
		}
		return *this;
	}

	///Decreases counter and opens gate, when it reaches zero
	CountGate& operator()() {
		LockScope<FastMutex> _(lock);
		if (curCount) --curCount;
		if (!curCount) {
			Super::alertAll();
		}
		return *this;
	}


protected:

	FastMutex lock;
	unsigned int curCount;

	void onSubscribe(Ticket &t) {
		LockScope<FastMutex> _(lock);
		if (curCount == 0) {
			alert(t);
		} else {
			add(t);
		}
	}

	void onSignoff(Ticket &t) {
		LockScope<FastMutex> _(lock);
		remove(t);
	}

	friend class WaitQueue<CountGate>;
};











}
