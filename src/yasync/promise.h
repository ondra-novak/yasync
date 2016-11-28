#pragma once


#include <exception>
#include "refcnt.h"
#include "fastmutex.h"
#include "lockScope.h"
#include "waitqueue.h"

namespace yasync {

	class CanceledPromise : public std::exception {
	public:
		const char *what() const throw() {
			return "Promise has been canceled";
		}
	};


	template<typename T> class Promise;
	template<typename T> class Future;

///observes result of promise and performs some action with it
template<typename T>
class AbstractPromiseObserver {
public:

	AbstractPromiseObserver() :next(0) {}
	virtual void operator()(const T &value) throw() = 0;
	virtual void operator()(const std::exception_ptr &exception) throw() = 0;
	virtual ~AbstractPromiseObserver() {}
protected:
	AbstractPromiseObserver *next;
	friend class Future<T>;
};


template<typename T> class Future {

	enum State {
		///Promise is still unresolved
		unresolved,
		///Promise is currently resolving
		/** In this state, the promise object is executing observers,
		however, the value of the promise still cannot be retrieved */
		resolving,
		///Promise is resolved.
		resolved
	};

	friend class Promise<T>;

	class Internal: public WaitQueue<Internal> {
	public:
		Internal()
			: WaitQueue<Internal>(WaitQueue<Internal>::fifo)
			, firstObserver(nullptr)
			, pcnt(0)
			, fcnt(0)
			, state(unresolved)
		{}

		void resolve(const T &v) throw() {
			LockScope<FastMutex> _(lk);
			if (state != unresolved) return;
			state = resolving;
			while (firstObserver) {
				AbstractPromiseObserver<T> *x = firstObserver;
				firstObserver = firstObserver->next;
				UnlockScope<FastMutex> _(lk);
				(*x)(v);
			}
			state = resolved;
			if (fcnt > pcnt) { //store result only if there are future variables
				try {					
					value = std::unique_ptr<T>(new T(v));
				}
				catch (...) {
					exception = std::current_exception();
				}
				this->alertAll();
			}
		}

		void resolve(const std::exception_ptr &e) throw() {
			LockScope<FastMutex> _(lk);
			if (state != unresolved) return;
			state = resolving;
			while (firstObserver) {
				AbstractPromiseObserver<T> *x = firstObserver;
				firstObserver = firstObserver->next;
				UnlockScope<FastMutex> _(lk);
				(*x)(e);
			}
			exception = e;
			state = resolved;
		}

		void addObserver(AbstractPromiseObserver<T> *obs) throw() {
			LockScope<FastMutex> _(lk);
			AbstractPromiseObserver<T> **x = &firstObserver;
			while (*x != nullptr) {
				x = &(*x)->next;
			}
			*x = obs;
			this->alertAll();
		}

		void removeObserver(AbstractPromiseObserver<T> *obs) throw() {
			LockScope<FastMutex> _(lk);
			AbstractPromiseObserver<T> **x = &firstObserver;
			while (*x != nullptr) {
				if (*x == obs) {
					*x = obs->next;
					return;
				}
				x = &(*x)->next;
			}
		}

		void addRefPromise() throw() {
			LockScope<FastMutex> _(lk);
			fcnt++;
			pcnt++;
		}

		bool removeRefPromise() throw() {
			LockScope<FastMutex> _(lk);
			pcnt--;
			if (pcnt == 0) {
				try {
					throw CanceledPromise();
				}
				catch (...) {
					UnlockScope<FastMutex> _(lk);
					resolve(std::current_exception());
				}
			}
			fcnt--;
			return fcnt == 0;
		}

		const T *getValue() const {
			return value.get();
		}

		State getState() const {
			return state;
		}

		void addRef() {
			LockScope<FastMutex> _(lk);
			fcnt++;

		}
		bool release() {
			LockScope<FastMutex> _(lk);
			fcnt--;
			return fcnt == 0;
		}

		void onSignoff(Ticket &t) {
			LockScope<FastMutex> _(lk);
			this->add(t);
		}

		void onSubscribe(Ticket &t) {
			LockScope<FastMutex> _(lk);
			this->remove(t);
		}

		bool hasPromise() const {
			LockScope<FastMutex> _(lk);
			return pcnt > 0 || state != unresolved;
		}


	protected:
		FastMutex lk;
		std::unique_ptr<T> value;
		std::exception_ptr exception;
		AbstractPromiseObserver<T> *firstObserver;
		
		unsigned int pcnt, fcnt;
		State state;

	};

	public:

		typedef typename WaitQueue<Internal>::Ticket Ticket;

	///Creates future variable
	/** Function creates future including of allocation of internal structure.
	If you need to just reserve variable for an assignment, use Future(nullptr)
	which will prevent extra uncesesary allocation*/
	Future() :value(new Internal) {}
	///Creates Future variable without allocating internal structure
	/** You can use such variable for assignment only */
	Future(std::nullptr_t) {}

	///Retrieve promise 
	/** You should store the result for resolving. Destroying the promise
	object without resolving causes resolvion of the promise with 
	an exception of type CanceledPromise */
	Promise<T> getPromise();
	
	///Determines, whether the future object is connected with a promise object
	/**
	  @retval true Yes, there is a pending promise object, so the caller
	  can safety wait on resolution. Function also returns true in case that
	  the future is already resolved
	  @retval false No, there is no pending promise object. The future
	  variable is either constructed with nullptr or the function
	  getPromise wasn't called yet. 
	  */

	bool hasPromise() const {return value->}

	///Determines, whether the future is resolved
	/** The future is resolved when the value is available after all
	observers are processed */
	bool isResolved() const { return value->getState() == resolved; }

	///Retrieve value when future is resolved
	/** @retval valid pointer to value (which is valid until the last future
	object is destroyed. If the future is not resolved yet, the function 
	returns nullptr */
	const T *tryGetValue() const {	return value->getValue();}
	
	///Create ticket for asynchronous waiting
	/** More information about tickets see WaitQueue */
	Ticket ticket() const { return value->ticket(); }

	///Wait for resolving
	/**
	 Function waits for infinite period until future is resolved 
	 */
	void wait() { value->wait(); }

	///Timeouted wait for resolving
	/**
	Function waits for specified timeout until future is resolved or timeout
	expires which one comes the first

	@param specifies timeout
	@retval true resolved
	@retval false timeout
	*/
	bool wait(const Timeout &tm) { return value->wait(tm); }




protected:

	RefCntPtr<Internal> value;



};


template<typename T>
class Promise {



};

}

