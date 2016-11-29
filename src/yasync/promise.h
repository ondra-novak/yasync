#pragma once


#include <exception>
#include "refcnt.h"
#include "fastmutex.h"
#include "lockScope.h"

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

	class Internal {
	public:
		Internal()
			: firstObserver(nullptr)
			, pcnt(0)
			, fcnt(0)
			, state(unresolved)
		{}

		void resolve(const T &v) throw() {
			LockScope<FastMutex> _(lk);
			if (state != unresolved) return;
			state = resolving;
			if (fcnt > pcnt) { //store result only if there are future variables
				try {
					value = std::unique_ptr<T>(new T(v));
				}
				catch (...) {
					exception = std::current_exception();
				}
			}
			while (firstObserver) {
				AbstractPromiseObserver<T> *x = firstObserver;
				firstObserver = firstObserver->next;
				UnlockScope<FastMutex> _(lk);
				(*x)(v);
			}
			state = resolved;
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
			if (state == resolved) {
				UnlockScope<FastMutex> _(lk);
				if (exception != nullptr) (*obs)(exception);
				else (*obs)(*value);
				return;
			}
			AbstractPromiseObserver<T> **x = &firstObserver;
			while (*x != nullptr) {
				x = &(*x)->next;
			}
			*x = obs;
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

		std::exception_ptr getException() const {
			return exception;
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

		bool hasPromise() const {
			LockScope<FastMutex> _(lk);
			return pcnt > 0 || state != unresolved;
		}


	protected:
		mutable FastMutex lk;
		std::unique_ptr<T> value;
		std::exception_ptr exception;
		AbstractPromiseObserver<T> *firstObserver;
		
		unsigned int pcnt, fcnt;
		State state;

	};

	public:


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
	returns nullptr. If future has been resolved by exception, 
	it also returns nullptr */
	const T *tryGetValue() const {	return value->getValue();}
	
	///Retreives exception pointer
	/**
	 @note Future must be resolved by exception. Otherwise, function returns nullptr
	 */
	std::exception_ptr getException() const { return value->getException(); }

	///Adds custom observer
	/**
	Observers are more general then function handlers. If you need some
	special action, you can create and add observer to the future. The future
	doesn't own the pointer, however you can't destroy it, 
	until it is removed. Each observer can be added to only one future at
	time. Adding the observer to multiple futures causes undefined behaviour.

	The future doesn't own the observer, so you have to destroy it by self
	once it is no longer needed. However, because every future is 
	always resolved some way, and it is resolved just once, you can
	destroy the observer after resolution, because once the future is
	resolved, all observers are removed automatically.	
	*/
	void addObserver(AbstractPromiseObserver<T> *obs) {
		value->addObserver(obs);
	}
	///Removes observer before the future is resolved
	/**
	@param obs observer to remove. Pointer is subject of comparison
	@retval true removed
	@retval false not found,or already resolved
	*/
	bool removeObserver(AbstractPromiseObserver<T> *obs) {
		value->removeObserver(obs);
	}

	///Wait for resolving
	/**
	 Function waits for infinite period until future is resolved 
	 */
	void wait() { 
		class Obs : public AbstractPromiseObserver<T> {
		public:
			Obs(const AlertFn &alert) :alert(alert) {}
			virtual void operator()(const T &) throw() {
				alert();
			}
			virtual void operator()(const std::exception_ptr &) throw() {
				alert();
			}
			AlertFn alert;
		};
		if (!isResolved()) {
			Obs obs(AlertFn::currentThread());
			addObserver(&obs);
			while (!isResolved()) halt() ();
		}
	}

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

