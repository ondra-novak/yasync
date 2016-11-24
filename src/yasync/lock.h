#pragma once

#include "alertfn.h"
#include "timeout.h"
namespace yasync {

	///FastLock can be used to implement very fast locking mechanism
	/** FastLock is implemented in user-space using inter-locked operations
	* and simple queue of threads. It doesn't support asynchronous locking and
	* timeouted waiting. Once thread starts waiting, it cannot be interrupted
	* until object is unlocked.
	*
	* FastLock takes only 8 (16) bytes and doesn't take any system resources
	* when it is idle.
	*
	* FastLock is designed to lock piece of code where state of data
	* can be inconsistent for a while. Because it is implemented in user-space
	* it doesn't enter to the kernel until it is really necessary (in conflicts)
	*
	* FastLock uses Thread's sleep/wakeUp feature and requires Thread object
	* associated with the current thread, otherwise it fails.
	*/
	class Lock {

	protected:
		class Slot;
		typedef std::atomic<Slot *> PSlot;

		class Slot {
		public:
			Slot(const AlertFn &n) :next(nullptr), notify(n) {}

			PSlot next;
			AlertFn notify;
		};

	public:


		Lock() :queue(nullptr), owner(nullptr) {}

		///Lock the object
		/**
		* When object is unlocked, function immediately return setting
		* current thread as new owner
		*
		* When object is locked, function will wait until other thread
		* unlocks the object.
		*
		* You cannot set timeout due simplicity of the object
		*/
		void lock() {
			//fast way how to lock unlocked lock
			//it doesn't need to retrieve current thread instance
			//when queue is empty - no owner - use tryLock
			//(acqure barrier here to get actual value
			if (queue.load(std::memory_order_acquire) != nullptr || !tryLock()) {
				//otherwise - use standard way
				lockSlow();
			}

		}

		///Unlocks the object
		/**Because lock is not bound to thread-id, it is valid operation
		* to unlock object in other thread. Ensure, that you know what you
		* doing. Also ensure, that you not trying to unlock already unlocked
		* object.
		*/
		void unlock() {
			//read owner
			Slot *o = owner.load(std::memory_order_acquire);
			//set owner to 0 - it should not contain invalid value after unlock
			owner.store(nullptr, std::memory_order_release);
			//try to unlock as there is no threads in queue (release barrier)
			Slot *topQ = o;					
			//in case, that other thread waiting, topQ will be different than owner
			if (!queue.compare_exchange_strong(topQ, nullptr)) {
				//p = queue iterator
				Slot *p = topQ;
				//find tail of the queue - previous object will be new owner
				while (p->next != o) p = p->next;
				//store target thread, setting new owner can make Slot disappear
				AlertFn alertFn = p->notify;
				//write owner to notify next thread - target thread has ownership now!
				owner.store(p,std::memory_order_release);
				//notify new owner - it may sleep (release barrier here)
				alertFn();
			}
		}

		///Tries to lock object without waiting
		/** In case, that lock is not successful, function fails returning
		* false.
		* @retval true successfully locked
		* @retval false lock is already locked
		*/
		bool tryLock() {
			///Create fake alert - we don't plan to use it.
			AlertFn fakeAlert(nullptr);
			//register empty slot - for stack address only
			Slot s(fakeAlert);
			//try to put slot to the top of queue if queue is empty
			Slot *tmp = nullptr;
			//queue were empty
			if (queue.compare_exchange_strong(tmp, &s)) {
				//set owner
				owner.store(&s,std::memory_order_release);
				//lock success
				return true;
			}
			else {
				//queue was not empty
				//failure
				return false;
			}

		}

		///Allows asynchronous acquire of the lock
		/** Object defines section where program can perform any action during waiting for ownership.
		*
		* The ownership can be granted any-time the program running inside of the section. You
		* can for example release an another lock and implement effective way to unlock-and-lock
		* atomically.
		*
		* @note you cannot leave section without acquiring the lock. State of
		* object is checked in the destructor and may block if the ownership of the lock
		* is not yet granted. This is done even if the destructor is called through the
		* exception's stack unwind.
		*/
		class Async {
			Lock &lk;
			Slot sl;
		public:
			Async(Lock &lk) :lk(lk), sl(AlertFn::currentThread()) {
				lk.addToQueue(&sl);
			}
			~Async() {
				while (lk.owner.load(std::memory_order_acquire) != &sl) halt();
			}
		};



	protected:

		///top of queue
		PSlot queue;
		///address of owner and also end of queue (end of queue is not NULL!)
		PSlot owner;

		///registers slot to the queue
		/**
		* @param slot slot to register
		* @retval true slot has been registered - do not destroy slot until
		*   it is notified. Also do not call unlock() before notification
		* @retval false queue has been empty, registration is not needed,
		* 	 ownership is granted, you can destroy slot without harm. If you
		* 	 no longer need to own lock, don't forget to call unlock()
		*/
		bool addToQueue(Slot *slot) {
			//get top of queue
			Slot *tmp = queue.load(std::memory_order_acquire);
			do {
				//set next pointer to current top
				slot->next = tmp;
				//repeat trade in case of failure
			} while (!queue.compare_exchange_strong(tmp, slot));

			return tmp != 0;
		}

		void lockSlow()
		{
			//setup waiting slot
			Slot s(AlertFn::currentThread());
			//try to add to queue
			if (addToQueue(&s)) {
				//if added, repeatedly test notifier until it is notified
				//cycle is required to catch and ignore unwanted notifications
				//from other events - full barier in pauseThread()
				while (owner.load(std::memory_order_acquire) != &s) halt();
			}
			else {
				//mark down owner
				owner.store(&s,std::memory_order_release);
				//object is locked, temporary variables are no longer needed
			}
		}

	};

}


