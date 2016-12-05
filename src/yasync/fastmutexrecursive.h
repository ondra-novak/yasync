#pragma once

#include "fastmutex.h"
namespace yasync {


	///Recursive lock
	/** Recursive lock tracks ownership. You can lock and unlock recursive
	in single thread without deadlock. If you need transfer ownership, you
	need to do it explicitly */

	class FastMutexRecursive : private FastMutex {
	public:
		typedef AlertFn ThreadRef;
		FastMutexRecursive() :ownerThread(nullptr),recursiveCount(0) {}

		///Lock FastLock 
		/** Function also takes care on recursive locking which causes deadlock on standard FastLock.
		If lock is already owned by the current tread, function doesn't block. Instead it counts recursions */
		void lock() {
			//fast way how to lock unlocked lock
			//it doesn't need to retrieve current thread instance
			//when queue is empty - no owner - use tryLock
			//(acqure barrier here to get actual value
			if (!tryLock()) {
				//otherwise - use standard way
				lockSlow();

				ownerThread = ThreadRef::thisThread();
				//set recursive count to 1
				recursiveCount.store(1, std::memory_order_release);
			}

		}

		///function is used when recursive locking is issued
		/**
		* Lock that allows recursive locking should define this function. Non-recursive lock will fail to compile
		*/
		void lockR() {
			lock();
		}

		///Try to lock the fastlock
		/** Function also takes care on recursive locking which causes deadlock on standard FastLock.
		If lock is already owned by the current tread, function doesn't return false. Instead it counts recursions and returns true

		@retval true locked and owned
		@retval false not locked and not owned
		*/

		bool tryLock() {
			ThreadRef cid = ThreadRef::thisThread();
			//try lock 
			if (FastMutex::tryLock()) {
				//if success, set setup recursive count

				ownerThread = ThreadRef::thisThread();
				//set recursive count to 1
				recursiveCount.store(1, std::memory_order_release);

				//return success
				return true;
			}

			//check whether we are already owning the fast lock
			if (ownerThread == cid) {
				//YES, increase recursive count
				++recursiveCount;

				return true;
			}
			//return success
			return false;
		}

		///Unlock the fastlock
		/**
		Function also takes care on recursive locking. Count of unlocks must match to the count of locks (including
		successes tryLocks). After these count matches, lock is released
		*/
		void unlock() {
			ThreadRef cid = ThreadRef::thisThread();
			//try lock 

			if (ownerThread == cid && recursiveCount.load(std::memory_order_acquire) > 0) {

				if (--recursiveCount == 0) {

					ownerThread = ThreadRef(nullptr);

					FastMutex::unlock();

				}

			}

		}

		///Unlocks the lock saving the recursion count
		/** This function unlocks the lock regardless on how many times the lock has been locked. Function
		saves this number to be used later with lockRestoreRecursion()

		@retval count of recursions
		*/

		unsigned int unlockSaveRecusion() {
			ThreadRef cid = ThreadRef::thisThread();
			if (cid != ownerThread) return 0;

			unsigned int ret = recursiveCount.load(std::memory_order_acquire);
			recursiveCount.store(0, std::memory_order_release);
			FastMutex::unlock();
			return ret;
		}

		///Locks the lock and restores recursion count
		/**
		@param count saved by unlockSaveRecursion()
		@param tryLock set true to use tryLock(), otherwise lock() is used
		@retval true lock successful
		@retval false lock failed. When tryLock=true, function is unable to perform locking, because
		the lock is owned (regardless on who is owner). When tryLock=false the lock is already owned by this
		thread, so function cannot restore recursion counter.
		*/

		bool lockRestoreRecursion(unsigned int count, bool tryLock) {
			if (count == 0) return true;
			//if tryLock enabled
			if (tryLock) {
				//call original tryLock - it returns false, when lock is owned
				if (!FastMutex::tryLock()) return false;
			}
			else {
				//try to lock
				lock();
				//test whether recursive count is 1
				if (recursiveCount != 1) {
					//no, lock has been already locked before, we unable to restore recursion count
					unlock();
					//return false
					return false;
				}
			}
			//restore recursion count
			recursiveCount.store(count,std::memory_order_release);
			//success
			return true;
		}

		///Changes ownership of the lock
		/**
		@param ref new thread reference. To receive thread reference,
		you can use ThreadRef::currentThread() 
		@retval true success (this thread is no longer owner)
		@retval false failure (this thread doesn't own the lock)
		*/
		bool setOwner(ThreadRef ref) {
			ThreadRef cid = ThreadRef::thisThread();
			if (ownerThread == cid) {
				ownerThread = ref;
				return true;
			}
			else {
				return false;
			}

		}

	protected:
		ThreadRef ownerThread;
		std::atomic<unsigned int> recursiveCount;


	};

}


