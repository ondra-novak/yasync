#pragma once

#include "alertfn.h"

namespace yasync {

	class ThreadCounter;


	class WaitTicket {
	public:


		WaitTicket() 
			:alertFn(AlertFn::currentThread())
			,counter(nullptr)
			,next(nullptr)
			,alerted(false) {}

		WaitTicket(const AlertFn &fn)
			:alertFn(fn)
			, counter(nullptr)
			, next(nullptr)
			, alerted(false) {}


		operator bool() const throw() {
			return alerted;
		};
		bool operator !() const throw() {
			return !alerted;
		}

		bool wait() {
			while (!alerted) halt();
		}

		bool wait(const Timeout &tm) {
			while (!alerted) {
				if (sleep(tm)) return false;
			}
			return true;
		}

		~WaitTicket() {
			if (!alerted && removeFn) {
				removeFn(this, removeContext);
			}
		}


	protected:
		AlertFn alertFn;
		WaitTicket *next;
		bool alerted;
		void *removeContext;
		void(*removeFn)(WaitTicket *itm, void *);

		friend class ThreadCounterBase;

		template<typename T, typename Fn>
		void setAutoRemove(T *object) {

			class Hlp {
			public:
				static void callIt(WaitTicket *itm, void *context) {
					T *p = reinterpret_cast<T *>(context);
					p->remove(itm);
				}
			};
			removeContext = object;
			removeFn = &Hlp::callIt;
		}

	};

	class ThreadCounterBase {
	public:
		
		ThreadCounterBase(bool stackMode = false)
			:stackMode(stackMode),root(nullptr),front(nullptr) {}
		
		template<typename Caller>
		bool add(WaitTicket &ticket, Caller *caller) {
			if (ticket.removeFn)
				return false;

			if (root) {
				if (stackMode) {
					ticket.next = root;
					root = &ticket;
				}
				else {
					front->next = &ticket;
					front = ticket;
				}
			}
			else {
				front = root = &ticket;
			}
			ticket.setAutoRemove(caller);
			return true;
		}


		bool add(WaitTicket &ticket) {
			add(ticket, this);
		}

		bool remove(WaitTicket &ticket) {
			WaitTicket **n = &root;
			while (*n != nullptr && *n != &ticket) {
				n = &((*n)->next);
			}
			if (*n != nullptr_t) {
				*n = ticket.next;
				ticket.removeFn = 0;
				return true;
			}
			else {
				return false;
			}
		}

		
	protected:
		WaitTicket *root;
		WaitTicket *front;
		bool stackMode;



	};




	

}