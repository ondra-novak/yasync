/*
 * pool.h
 *
 *  Created on: 30. 11. 2016
 *      Author: ondra
 */

#pragma once
#include <algorithm>

#include "alertfn.h"
#include "dispatcher.h"


namespace yasync {

///Definition of thread pool
/** This class defines parameters of a thread pool. Note that the instance of this class is not running
 * the thread pool. Underlying implementation is not directly available, because it keeps things simpler.
 *
 * You can use ThreadPool instance to define pool parameters. The instance can be copied and moved without limitations.
 * To create running pool, call ThreadPool::start(). The function returns DispatchFn, which is used to
 * post tasks to the running pool. The pool is destroyed with the last reference to it. The pool stops its threads at
 * background, so destroying the last reference will not involve blocking call.
 */
class ThreadPool {
public:

	///setup the thread pool to initial settings
	/**
	 * maxThreads = available CPUs
	 * minThreads = 0
	 * idleTimeout = 1second
	 * maxQueue = 0
	 * threadStop = not defined
	 * threadStart = not defined
	 */
	ThreadPool();

	///Starts the thread pool
	/** Once the thread pool is started, its parameters cannot be changed. If you need to change parameters, you
	 * have to start new pool. You should also destroy the reference to old pool
	 * @return
	 */
	DispatchFn start();

	///Gets timeout for idle waiting
	/**
	 * When thread has no task assigned, it doing idle waiting. You can specify timeout in milliseconds, how
	 *  long the thread can be idle until it is stopped
	 *
	 * @return time in milliseconds to allowed wait idle
	 */
	unsigned int getIdleTimeout() const {
		return idleTimeout;
	}

	///sets timeout for idle waiting
	/**
	 * When thread has no task assigned, it doing idle waiting. You can specify timeout in milliseconds, how
	 *  long the thread can be idle until it is stopped
	 *
	 * @param idleTimeout how long in milliseconds will thread wait idle until it is stopped.
	 *   The zero argument means no timeout
	 */
	ThreadPool &setIdleTimeout(unsigned int idleTimeout) {
		this->idleTimeout = idleTimeout;
		return *this;
	}

	unsigned int getMaxQueue() const {
		return maxQueue;
	}

	///Specifies maximum items in the queue
	/**Allows to limit count of tasks in the queue. Once the queue is full, caller is blocked until at least
	 * one task is removed from the queue. Blocked thread cannot perform any action during waiting on the queue (
	 * unless dispatchOnWait is enabled).
	 *
	 * @param maxQueue count of task allowed to wait in queue. Minimum (and default) is 1
	 */
	ThreadPool &setMaxQueue(unsigned int maxQueue) {
		this->maxQueue = std::max<unsigned int>(maxQueue,1);
		return *this;
	}

	///Retrieves current max threads
	unsigned int getMaxThreads() const {
		return maxThreads;
	}


	///Sets max threads
	/**
	 * @param maxThreads maximum threads. This number cannot be 0. Default value is equal to count of cores
	 */
	ThreadPool &setMaxThreads(unsigned int maxThreads) {
		this->maxThreads = std::max<unsigned int>(maxThreads,1);
		return *this;
	}

	const AlertFn& getThreadStart() const {
		return threadStart;
	}

	///Defines alert function, which is called when new thread is started
	/**
	 * @param threadStart You can define function alert using AlertFn::callFn if you need to monitor creation of pool's threads
	 * or initialize TLS of this thread. Alert is executed in context of newly created thread
	 * @return
	 */
	ThreadPool &setThreadStart(const AlertFn& threadStart) {
		this->threadStart = threadStart;
		return *this;
	}

	const AlertFn& getThreadStop() const {
		return threadStop;
	}

	///Defines alert function, which is called when thread exits
	/**
	 *
	 * @param threadStop You can define function alert using AlertFn::callFn if you need to monitor destruction of pool's threads
	 * or cleanup TLS of this thread. Alert is executed in context of exiting thread
	 * @return
	 */
	ThreadPool &setThreadStop(const AlertFn& threadStop) {
		this->threadStop = threadStop;
		return *this;
	}

	bool isDispatchOnWait() const {
		return dispatchOnWait;
	}

	///Enables dispatching while a thread is waiting for queue
	/**
	 * @param dispatchOnWait set false (default) to disable dispatching, set true to enable dispatching.
	 *
	 * When dispatching is allowed, the thread which is blocked on full queue is allowed to dispatch functions.
	 * This can prevent various deadlocks, if - for example - pool's thread sending synchornous request to the
	 * caller. The thread can wait to dispatch message in thread which waiting on full queue can result in deadlock
	 */
	ThreadPool &setDispatchOnWait(bool dispatchOnWait) {
		this->dispatchOnWait = dispatchOnWait;
		return *this;
	}

	unsigned int getQueueTimeout() const {
		return queueTimeout;
	}

	///Sets queue timeout
	/**
	 * @param queueTimeout timeout in milliseconds. Note that value 0 has meaning "infinity" - wait forever and
	 * it is default. You can define own timeout in this function. Timeouted dispatching is reported through return
	 * value of DispatchFn or through canceled future in case that futures has been used
	 */
	ThreadPool &setQueueTimeout(unsigned int queueTimeout) {
		this->queueTimeout = queueTimeout;
		return *this;
	}

	const AlertFn& getFinalStop() const {
		return finalStop;
	}

	///Sets alertFn which is alerted when the last thread of the pool is stopped
	/**
	 * @param finalStop alert function which is alerted when the last thread of the pool is stopped after the
	 * last reference of the dispatch function has been destroyed. This allows to wait to finish all pending tasks.
	 *
	 * By destroying last refernece of the dispatch function causes that thread pool finishes all scheduled
	 * task and then it destroyes itself. This is done without blocking the thread which destroyed last reference.
	 * To determine right moment when everything is done, you can define final stop alert though this function.
	 *
	 *
	 */
	ThreadPool &setFinalStop(const AlertFn& finalStop) {
		this->finalStop = finalStop;
		return *this;
	}

	///this command passed to the pool causes, that current queue will be cleared.
	/** This doesn't stop currently running tasks, just clears pending tasks in the queue. You can use
	 * this command before the last reference of the pool is destroyed.
	 */
	static RefCntPtr<AbstractDispatchedFunction> clearQueueCmd;

	private:
		unsigned int maxThreads;
		unsigned int maxQueue;
		unsigned int idleTimeout;
		unsigned int queueTimeout;
		bool dispatchOnWait;
		AlertFn threadStart;
		AlertFn threadStop;
		AlertFn finalStop;

	};

}
