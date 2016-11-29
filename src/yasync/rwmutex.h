#pragma once

#include "waitqueue.h"
#include "fastmutex.h"
#include "lockScope.h"

namespace yasync {

	class RWMutex;

	class SharedMutex {
	public:

		void lock();
		bool lock(const Timeout &tm);
		void unlock();
		bool tryLock();

	protected:
		SharedMutex() {}
	};

	class ExclusiveMutex {
	public:

		void lock();
		bool lock(const Timeout &tm);
		void unlock();
		bool tryLock();

	protected:
		ExclusiveMutex() {}
	};



	class RWMutex: public WaitQueue<RWMutex>, public SharedMutex, public ExclusiveMutex {
	public:
		typedef WaitQueue<RWMutex> Super;

		class Ticket : public Super::Ticket {
		public:
			Ticket(WaitQueue &q, const AlertFn &alertFn, bool shared)
				:Super::Ticket(q, alertFn), shared(shared) {}


			const bool shared;
		};

		///Create ticket
		Ticket ticket() {
			return Ticket(*this, AlertFn::currentThread(), false);
		}

		///Create ticket with custrom alert function
		Ticket ticket(const AlertFn &fn) {
			return Ticket(*this, fn, false);
		}

		///Create ticket
		Ticket ticketShared() {
			return Ticket(*this, AlertFn::currentThread(), true);
		}

		///Create ticket with custrom alert function
		Ticket ticketShared(const AlertFn &fn) {
			return Ticket(*this, fn, true);
		}

		///Wait in the queue
		/** Creates ticket and waits in the queue */
		void waitShared() {
			auto t = ticketShared();
			while (!t) halt();
		}

		///Wait in the queue
		/** Creates ticket and waits in the queue for specified timeout*/
		bool waitShared(const Timeout &tm) {
			auto t = ticketShared();
			while (!t) {
				if (sleep(tm)) return false;
			}
			return true;
		}

		~RWMutex() {
			lk.lock();
		}

		RWMutex() :Super(Super::fifo), readers(0) {}

		void lock() {wait();}
		bool lock(const Timeout &tm) { return wait(tm);}
		void lockShared() { waitShared(); }
		bool lockShared(const Timeout &tm) { return waitShared(tm); }
		bool tryLock() {
			LockScope<FastMutex> _(lk);
			if (readers == 0) {
				readers = -1;
				return true;
			}
			else {
				return false;
			}
		}
		bool tryLockShared() {
			LockScope<FastMutex> _(lk);
			if (readers >= 0) {
				++readers;
				return true;
			}
			else {
				return false;
			}
		}
		void unlock() {
			if (readers < 0) {
				LockScope<FastMutex> _(lk);
				readers = 0;
				alertThreads();
			}
		}
		void unlockShared() {
			if (readers > 0) {
				LockScope<FastMutex> _(lk);
				--readers;
				if (readers == 0) {
					alertThreads();
				}
			}
		}
	protected:
		FastMutex lk;
		int readers;
		
		void alertThreads() {
			bool rep;
			do {
				Ticket *t = static_cast<Ticket *>(top);
				if (t && t->shared) {
					++readers;
					alertOne();
					rep = true;
				}
				else if (readers == 0) {
					readers = -1;
					alertOne();
					rep = false;
				}
				else {
					rep = false;
				}
			} while (rep);
		}

		void onSubscribe(Super::Ticket &t) {
			Ticket &tt = static_cast<Ticket &>(t);
			LockScope<FastMutex> _(lk);
			if (tt.shared) {
				if (readers >= 0) {
					++readers;
					alert(t);
				}
				else {
					add(t);
				}
			}
			else {
				if (readers == 0) {
					readers = -1;
					alert(t);
				}
				else {
					add(t);
				}
			}		
		}

		void onSignoff(Super::Ticket &t) {
			Ticket &tt = static_cast<Ticket &>(t);
			{
				LockScope<FastMutex> _(lk);
				remove(tt);
			}
			if (tt) {
				if (tt.shared) {
					unlockShared();
				}
				else {
					unlock();
				}
			} 
		}
		friend class WaitQueue<RWMutex>;
	};

	inline void SharedMutex::lock() {return static_cast<RWMutex *>(this)->lockShared();}
	inline bool SharedMutex::lock(const Timeout &tm) { return static_cast<RWMutex *>(this)->lockShared(tm); }
	inline bool SharedMutex::tryLock() { return static_cast<RWMutex *>(this)->tryLockShared(); }
	inline void SharedMutex::unlock() { return static_cast<RWMutex *>(this)->unlockShared(); }
	inline void ExclusiveMutex::lock() { return static_cast<RWMutex *>(this)->lock(); }
	inline bool ExclusiveMutex::lock(const Timeout &tm) { return static_cast<RWMutex *>(this)->lock(tm); }
	inline bool ExclusiveMutex::tryLock() { return static_cast<RWMutex *>(this)->tryLock(); }
	inline void ExclusiveMutex::unlock() { return static_cast<RWMutex *>(this)->unlock(); }

}