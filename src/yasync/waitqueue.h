#pragma once

#include "alertfn.h"

namespace yasync {


	///Implementation of thread queue used internaly in many synchronization objects
	/**
	 *
	 * @note The class is not MT safe. You have to wrap all functions by locks.
	 *
	 *
	 * @tparam Impl class which implements other parts of synchronization object. The
	 * class must inherit this template. The Impl class must have two following functions:
	 * onSubscribe and onSignoff. Both functions contains Ticket& as argument. It expects,
	 * that the Impl class acquires a lock and calls add or remove depend on internal
	 * logic.
	 */
	template<typename Impl>
	class WaitQueue {
	public:


		///Every thread is going to wait in the queue must acquire a Ticket
		/** @see ticket() */
		class Ticket {
		public:
			Ticket(WaitQueue &q, const AlertFn &alertFn)
				:alertFn(alertFn)
				,queue(q)
				,alerted(false)
				,removed(false)
				,next(nullptr) {
				q.subscribe(*this);
			}

			~Ticket() {
				if (!alerted && !removed)
					queue.signoff(*this);
			}

			///Ask ticket whether is alerted
			/**
			 * @retval true not alerted yet
			 * @retval false alerted
			 */
			bool operator!() const {return !alerted;}
			///Ask ticket whether is alerted
			/**
			 * @retval false not alerted yet
			 * @retval true alerted
			 */
			operator bool() const {return alerted;}
			bool operator==(bool x) const {return alerted == x;}
			bool operator!=(bool x) const {return alerted != x;}

		protected:
			friend class WaitQueue<Impl>;
			AlertFn alertFn;
			WaitQueue &queue;
			bool alerted;
			bool removed;
			Ticket *next;
		};

		///Create ticket
		Ticket ticket() {
			return Ticket(*this,AlertFn::currentThread());
		}

		///Create ticket with custrom alert function
		Ticket ticket(const AlertFn &fn) {
			return Ticket(*this,fn);
		}

		///Wait in the queue
		/** Creates ticket and waits in the queue */
		void wait() {
			auto t = static_cast<Impl &>(*this).ticket();
			while (!t) halt();
		}

		///Wait in the queue
		/** Creates ticket and waits in the queue for specified timeout*/
		bool wait(const Timeout &tm) {
			auto t = static_cast<Impl &>(*this).ticket();
			while (!t) {
				if (sleep(tm)) return false;
			}
			return true;
		}

	protected:

		enum QueueMode {
			lifo,
			fifo
		};

		///Initialize the queue
		WaitQueue(QueueMode mode):mode(mode),top(nullptr),bottom(nullptr) {}

		///Function is called by Ticket to subscribe
		/**
		 * @param t ticket
		 * @note function doesn't handle subscribe. Implementation must call add() to
		 * subscribe ticket to the queue
		 */
		void subscribe(Ticket &t) {
			static_cast<Impl *>(this)->onSubscribe(t);
		}
		///Function is called by Ticket to subscribe
		/**
		 * @param t ticket
		 * @note function doesn't handle signoff. Implementation must call add() to
		 * subscribe ticket to the queue
		 */
		void signoff(Ticket &t) {
			static_cast<Impl *>(this)->onSignoff(t);
		}

		///Adds ticket to the queue
		bool add(Ticket &t) {
			if (top == nullptr) {
				top = bottom = &t;
			} else if (mode == lifo) {
				t.next = top;
				top = &t;
			} else {
				bottom->next = &t;
				bottom = &t;
			}
			return true;
		}

		///Removes ticket from the queue
		/** The function reject removing already alerted or removed ticket
		 * Removing already alerted ticket is valid operation, because a race condition
		 * can happen, when ticket is alerted after it scheduled for removing
		 *
		 * @param t
		 * @return
		 */
		bool remove(Ticket &t) {
			if (!t.alerted && !t.removed) {
				Ticket *x = top;
				if (x == &t) {
					top = t.next;
					t.next = nullptr;
					if (top == nullptr) bottom = nullptr;
				}
				 else {
					Ticket *z;
					while (x != nullptr && x != &t) {
						z = x;
						x = x->next;
					}
					if (x == nullptr)
						return false;
				}
				t.removed = true;
			}
			return true;
		}


		///Alert one ticket (top)
		/**
		 * @retval true alerted at least one
		 * @retval false no tickets in queue
		 */
		bool alertOne() {
			Ticket *t = top;
			if (t) {
				top = t->next;

				if (top == nullptr)
					bottom = nullptr;

				alert(*t);
				return true;
			}
			return false;
		}

		///Alert all tickets
		/**
		 * @retval true alerted as least one
		 * @retval false no tickets in queue
		 */
		bool alertAll() {
			if (alertOne()) {
				bool b = true;
				while (b) {
					b = alertOne();
				}
				return true;
			} else {
				return false;
			}
		}

		///Alert ticket
		/** For situation when the logic need to alert ticket which is not yet in the queue,
		 * for example while state of the logic doesn't need to queue at the moment.
		 * @param t
		 */
		void alert(Ticket &t) {
			t.next = nullptr;
			AlertFn alertFn = t.alertFn;
			t.alerted = true;
			alertFn();
		}


	protected:
		QueueMode mode;
		Ticket *top;
		Ticket *bottom;

	};


	

}
