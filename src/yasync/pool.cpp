/*
 * pool.cpp
 *
 *  Created on: 30. 11. 2016
 *      Author: ondra
 */
#include <thread>
#include <deque>
#include "pool.h"

#include "condvar.h"

#include "semaphore.h"
#include "fastmutex.h"
#include "dispatcher.h"
#include "nulllock.h"

using std::deque;
namespace yasync {


	class ThreadPoolImpl;
	typedef RefCntPtr<ThreadPoolImpl> PPool;

	class ThreadPoolImpl: public AbstractDispatcher {
	public:

		typedef ThreadPool Config;

		ThreadPoolImpl(const Config &cfg);

		DispatchFn createControl();

		bool dispatch(const AbstractDispatcher::Fn &fn) throw();
		void finish();

		~ThreadPoolImpl() {
			cfg.getFinalStop()();
		}

		bool yield(unsigned int recursion) throw();


	protected:
		Config cfg;
		FastMutex lk;
		CondVar<NullLock> workerTrigger;
		CondVar<NullLock> queueTrigger;
		std::deque<AbstractDispatcher::Fn> queue;
		unsigned int threadCount;
		bool finishFlag;

		class Control: public AbstractDispatcher {
		public:

			Control(const PPool &pool):pool(pool) {}

			virtual bool dispatch(const Fn &fn) throw() {
				return pool->dispatch(fn);
			}
			virtual ~Control() {
				pool->finish();
			}

			void operator delete(void *, std::size_t) {}
		protected:
			PPool pool;
		};

		unsigned char controlSpace[sizeof(Control)];





		void startThread();
		void runWorker() throw();
		void runWorkerCycle() throw();
	bool queueIsFull();
	bool queueIsEmpty();

};

	ThreadPoolImpl::ThreadPoolImpl(const Config& cfg)
		:cfg(cfg)
		,workerTrigger(nullLock,true)
		,queueTrigger(nullLock,false)
		,threadCount(0),finishFlag(false) {

	}

	DispatchFn ThreadPoolImpl::createControl() {
		void *p = controlSpace;
		RefCntPtr<AbstractDispatcher> d(new(p) Control(this));
		return DispatchFn(d);
	}

	bool ThreadPoolImpl::queueIsFull() {
		return queue.size() >= cfg.getMaxQueue();
	}

	bool ThreadPoolImpl::dispatch(const AbstractDispatcher::Fn& fn) throw()  {
		LockScope<FastMutex> _(lk);
		if (fn == ThreadPool::clearQueueCmd) {
			queue.clear();
			return true;
		}

		//if queue is full
		if (queueIsFull()) {
			//calculate timeout
			Timeout tm ( cfg.getQueueTimeout() == 0?Timeout(nullptr):Timeout(cfg.getIdleTimeout()));
			//select wait operation
			bool dow = cfg.isDispatchOnWait();
			//this will set to true when timeouted
			bool timeouted = false;
			//cycle while not timeouted and queue is full
			while (queueIsFull() && !timeouted) {
				//acquire queue lock
				auto t = queueTrigger.ticket();
				//unlock scope for waiting
				UnlockScope<FastMutex> _(lk);
				//depend on wait op
				if (dow) {
					//while not timeouted and not signaled
					while (!t && !timeouted) {
						//sleep and dispatch, mark if timeouted
						timeouted = sleepAndDispatch(tm);
					}
				}  else {
					//while not timeouted and not signaled
					while (!t && !timeouted) {
						//sleep and dispatch, mark if timeouted
						timeouted = sleep(tm);
					}
				}
				//we are signaled or timeouted
			}
			//wait block finished (whathever reason), finnaly check queue
			if (queueIsFull())
				//still full, we cannot continue
				return false;
		}
		//push task to the thread
		queue.push_back(fn);
		//alert one worker. if none available, the create new one
		if (!workerTrigger.notifyOne() && threadCount < cfg.getMaxThreads()) {
			startThread();
		}
		return true;
	}

	void ThreadPoolImpl::finish() {
		LockScope<FastMutex> _(lk);
		//mark finish flag
		finishFlag = true;
		//release all waiting workers
		workerTrigger.notifyAll();

	}

	bool ThreadPoolImpl::queueIsEmpty() {
		return queue.empty() && !finishFlag;
	}

	bool ThreadPoolImpl::yield(unsigned int recursion) throw()
	{
		if (recursion > cfg.getMaxYieldRecursion()) 
			return false;
		if (queue.empty()) {
			return false;
		}
		LockScope<FastMutex> _(lk);
		if (!queue.empty()) {
			AbstractDispatcher::Fn fn = queue.front();
			queue.pop_front();
			//alert any waiting thread for empty queue
			queueTrigger.notifyOne();
			//unlock pool - task will not interact with it
			UnlockScope<FastMutex> _(lk);
			//run task
			fn->run();
			return true;
		}
		return false;

	}


class ThreadQueueState : public IDispatchQueueControl {
public:
	ThreadQueueState(ThreadPoolImpl *poolImpl) :poolImpl(poolImpl), recursionCount(0){}


	virtual bool yield()  throw() {
		recursionCount++;
		bool res = poolImpl->yield(recursionCount);
		recursionCount--;
		return res;
	}
	virtual DispatchFn getDispatch() throw() {
		return RefCntPtr<AbstractDispatcher>(poolImpl);
	}

	ThreadPoolImpl *poolImpl;
	unsigned int recursionCount;

};


void ThreadPoolImpl::startThread() {
	PPool me = this;
	++threadCount;
	::yasync::newThread >> [me] {
		ThreadQueueState st(me);
		ThreadQueueState::setThreadQueueControl(&st);
		me->runWorker();
	};
}


void ThreadPoolImpl::runWorker() throw() {


	cfg.getThreadStart()();

	//run worker's cycle
	runWorkerCycle();

	cfg.getThreadStop()();
}

void ThreadPoolImpl::runWorkerCycle() throw() {
	do {
		//lock the pool - we will interact with it
		LockScope<FastMutex> _(lk);
		//check queue, is empty?
		if (queueIsEmpty()) {
			//queue is empty, we must wait now - define how long
			Timeout tm(cfg.getIdleTimeout());
			//cycle while queue is empty and not timeout
			while (queueIsEmpty()) {
				//unlock scope and wait for trigger
				if (!workerTrigger.unlockAndWait(tm,lk)) break;
			}
		}
		//this part can be reached if
		// - queue is not empty
		// - finishFlag is true
		// - waiting has timeouted
		// check queue - has tasks? - finishFlag applied after all task are processed
		if (!queue.empty()) {
			//pick task from queue
			AbstractDispatcher::Fn fn = queue.front();
			queue.pop_front();
			//alert any waiting thread for empty queue
			queueTrigger.notifyOne();
			//unlock pool - task will not interact with it
			UnlockScope<FastMutex> _(lk);
			//run task
			fn->run();
		} else {
			//finishFlag is true or timeout
			//decrease count of threads
			--threadCount;
			//exit cycle
			return;
		}

	} while (true);
}



ThreadPool::ThreadPool()
	:maxThreads(std::thread::hardware_concurrency())
	,maxQueue(1)
	,idleTimeout(1000)
	,queueTimeout(0)
	,maxYieldRecursion(4)
	,dispatchOnWait(false)
	,threadStart(nullptr)
	,threadStop(nullptr)
	,finalStop(nullptr)
{
}

DispatchFn ThreadPool::start() {
	PPool pool = new ThreadPoolImpl(*this);
	return pool->createControl();
}

RefCntPtr<AbstractDispatchedFunction> ThreadPool::clearQueueCmd(nullptr);


}
