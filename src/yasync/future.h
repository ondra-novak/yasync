#pragma once


#include <exception>
#include "refcnt.h"
#include "fastmutex.h"
#include "lockScope.h"
#include "dispatcher.h"

namespace yasync {

	class CanceledPromise : public std::exception {
	public:
		const char *what() const throw() {
			return "Promise has been canceled";
		}
	};


	template<typename T> class Promise;
	template<typename T> class Future;
	template<typename T, typename U> class DispatchedFuture;
	template<> class Promise<void>;
	template<> class Future<void>;
	class Checkpoint;

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


///Empty class Void is used instead void as regular type. If you create Promise<void> it is converted to Promise<Void>
class Void {};

namespace _hlp {
	template<typename OrigType, typename Type>
	struct FutureHandlerReturn { typedef Future<Type> T;};
	template<typename OrigType, typename Type>
	struct FutureHandlerReturn<OrigType, Future<Type> > { typedef Future<Type> T;};
	template<typename OrigType>
	struct FutureHandlerReturn<OrigType, std::exception_ptr> { typedef Future<OrigType> T;};
	template<typename OrigType>
	struct FutureHandlerReturn<OrigType, void> { typedef Future<OrigType> T;};



	template<typename RetVal>
	struct CallHlp {
		template<typename Fn, typename Arg>
		RetVal call(Fn &fn, const Arg &arg) {
			return fn(arg);
		}
		template<typename Fn, typename Arg>
		RetVal call(Fn &fn) {
			return fn();
		}
	};
	template<>
	struct CallHlp<Void> {
		template<typename Fn, typename Arg>
		Void call(Fn &fn, const Arg &arg) {
			fn(arg);
			return Void();
		}
		template<typename Fn>
		Void call(Fn &fn) {
			fn();
			return Void();
		}
	};
}

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
	friend class Promise<void>;


	class Internal {
	public:
		Internal()
			: firstObserver(nullptr)
			, pcnt(0)
			, fcnt(0)
			, state(unresolved)
		{}

		template<typename X>
		void notifyObserversLk(const X &val) {
			while (firstObserver) {
				AbstractPromiseObserver<T> *x = firstObserver;
				firstObserver = firstObserver->next;
				UnlockScope<FastMutex> _(lk);
				(*x)(val);
			}
		}

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
				notifyObserversLk(*value);
			} else {
				notifyObserversLk(v);
			}
			state = resolved;
		}

		void resolve(T &&v) throw() {
			LockScope<FastMutex> _(lk);
			if (state != unresolved) return;
			state = resolving;
			if (fcnt > pcnt) { //store result only if there are future variables
				try {
					value = std::unique_ptr<T>(new T(std::move(v)));
				}
				catch (...) {
					exception = std::current_exception();
				}
				notifyObserversLk(*value);
			} else {
				notifyObserversLk(v);
			}
			state = resolved;
		}


		void resolve(const std::exception_ptr &e) throw() {
			LockScope<FastMutex> _(lk);
			if (state != unresolved) return;
			state = resolving;
			exception = e;
			notifyObserversLk(exception);
			state = resolved;
		}

		void addObserver(AbstractPromiseObserver<T> *obs) const throw() {
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

		bool callObserver(AbstractPromiseObserver<T> *obs) const throw() {
			LockScope<FastMutex> _(lk);
			if (state == resolved) {
				UnlockScope<FastMutex> _(lk);
				if (exception != nullptr) (*obs)(exception);
				else (*obs)(*value);
				return true;
			}
			return false;
		}

		bool addObserverIfPending(AbstractPromiseObserver<T> *obs) const throw() {
			LockScope<FastMutex> _(lk);
			if (state == resolved) {
				return false;
			}
			AbstractPromiseObserver<T> **x = &firstObserver;
			while (*x != nullptr) {
				x = &(*x)->next;
			}
			*x = obs;
			return true;
		}

		bool removeObserver(AbstractPromiseObserver<T> *obs) const throw() {
			LockScope<FastMutex> _(lk);
			AbstractPromiseObserver<T> **x = &firstObserver;
			while (*x != nullptr) {
				if (*x == obs) {
					*x = obs->next;
					return true;
				}
				x = &(*x)->next;
			}
			return false;
		}

		void addRefPromise() throw() {
			LockScope<FastMutex> _(lk);
			fcnt++;
			pcnt++;
		}

		bool releasePromise() throw() {
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

		void cancel(const std::exception_ptr &e) {
			AbstractPromiseObserver<T> *obs, *item;
			{
				LockScope<FastMutex> _(lk);
				obs = firstObserver;
				firstObserver = nullptr;
				state = resolved;
				exception = e;
			}
			while (obs != nullptr) {
				item = obs;
				obs = obs->next;
				item->operator()(e);
			}
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
		AbstractPromiseObserver<T> mutable *firstObserver;
		
		unsigned int pcnt, fcnt;
		State state;

	};

	public:
		typedef T Type;
		typedef Promise<T> PromiseT;


	///Creates future variable
	/** Function creates future including of allocation of internal structure.
	If you need to just reserve variable for an assignment, use Future(nullptr)
	which will prevent extra uncesesary allocation*/
	Future() :value(new Internal) {}
	///Creates Future variable without allocating internal structure
	/** You can use such variable for assignment only */
	Future(std::nullptr_t) {}

	///Creates already resolved future
	/** If one need a shortcut to convert value to future value, this is faster way. It constructs future,
	 * which has already a value
	 * @param val value of the future
	 */
	explicit Future(const T &val):value(new Internal) {
		getPromise().setValue(val);
	}
	///Creates already resolved future
	/** If one need a shortcut to convert value to future value, this is faster way. It constructs future,
	 * which has already a value
	 * @param val value of the future
	 */
	explicit Future(T &&val):value(new Internal) {
		getPromise().setValue(std::move(val));
	}


	///Converts exception to Future which is resolved by this exception
	template<typename X>
	static Future exception(const X &val) {
		Future f;
		f.getPromise().setException(val);
		return f;
	}

	///Retrieve promise 
	/** You should store the result for resolving. Destroying the promise
	object without resolving causes resolvion of the promise with 
	an exception of type CanceledPromise */
	Promise<T> getPromise() const;
	
	///Determines, whether the future object is connected with a promise object
	/**
	  @retval true Yes, there is a pending or resolved.
	  @retval false No, there is no pending promise object. The future
	  variable is either constructed with nullptr or the function
	  getPromise wasn't called yet. 
	  */

	bool hasPromise() const {return value != nullptr && value->hasPromise();}

	///Pending future is future which is currently waiting for completion
	/**
	  Such future must be initialized, has promise and unresolved
	  @retval true future is pending (i.e. waiting for result)
	  @retval false future is in other state (resolved or has not promise or is null)

	  For pending futures it is worth to add observer. Because the status can
	  change anytime during adding the observer, it still doesn't garantee that the
	  observer will be called in the source thread.
	 */
	bool isPending() const {
		return value->hasPromise() && value->getState() != resolved;
	}

	///Determines, whether the future is resolved
	/** The future is resolved when the value is available after all
	observers are processed 
	
	@retval true future is resolved
	@retval false future is not resolved. So it can be either pending or null 
	or the function getPromise was not called yet
	*/
	bool isResolved() const { return value->hasPromise() && value->getState() == resolved; }

	///Retrieve value when future is resolved
	/** @retval valid pointer to value (which is valid until the last future
	object is destroyed. If the future is not resolved yet, the function 
	returns nullptr. If future has been resolved by exception, 
	it also returns nullptr */
	const T *tryGetValue() const {	return value->getValue();}
	
	///Retreives exception pointer
	/**
	 @note Future must be resolved by exception. Otherwise, the function returns nullptr
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
	void addObserver(AbstractPromiseObserver<T> *obs) const {
		value->addObserver(obs);
	}

	///adds observer if future is pending.
	/**
	This garantee, that observer will not called in current thread. Note that
	observer can be rejected if status changes during the process
	@param obs observer to add
	@retval true added
	@retval false the future is already resolved
	*/
	bool addObserverIfPending(AbstractPromiseObserver<T> *obs) const {
		return value->addObserverIfPending(obs);
	}

	///Calls observer if future is resolved
	/**
	Observer will be called only when future is resolved, otherwise, function fails 
	with false status
	@retval true observer called
	@retval false future is not resolved
	*/
	bool callObserver(AbstractPromiseObserver<T> *obs) const {
		return value->callObserver(obs);
	}

	///Removes observer before the future is resolved
	/**
	@param obs observer to remove. Pointer is subject of comparison
	@retval true removed
	@retval false not found,or already resolved
	*/
	bool removeObserver(AbstractPromiseObserver<T> *obs) const {
		return value->removeObserver(obs);
	}

	///This observer generates alert when the Future is resolved. Value is ignored, you have to retrieve it from the Future object	
	class AlertObserver: public AbstractPromiseObserver<T> {
	public:
		AlertObserver(const AlertFn &alert) :alert(alert),alerted(false) {}
		virtual void operator()(const T &) throw() {
			alerted = true;
			alert();
		}
		virtual void operator()(const std::exception_ptr &) throw() {
			alerted = true;
			alert();
		}
		AlertFn alert;
		bool alerted;
	};

	///Wait for resolving
	/**
	 Function waits for infinite period until future is resolved 
	 */
	void wait() const {
		if (!isResolved()) {
			AlertObserver obs(AlertFn::thisThread());
			addObserver(&obs);
			while (!obs.alerted) {
				halt();
			}
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
	bool wait(const Timeout &tm) const {
		if (!isResolved()) {
			AlertObserver obs(AlertFn::thisThread());
			addObserver(&obs);
			while (!obs.alerted)
				if (sleep(tm)) {
					removeObserver(&obs);
					return false;
				}
		}
		return true;
	}


	///Retrieve value
	/** Retrieves value of the future. If the future is not resolved yet, the function blocks until
	 * the future  is resolved. If the future is resolved with exception, the exception is thrown now.
	 *
	 * @return value of the future
	 */
	const T &get() const {
		wait();
		std::exception_ptr e = value->getException();
		if (e != nullptr) {
			std::rethrow_exception(e);
		} else {
			const T *v = value->getValue();
			if (v != nullptr) {
				return *v;
			} else {
				throw CanceledPromise();
			}
		}
	}

	const T &getValue() const {
		return get();
	}

	///Convert the future to ordinary value. The operator may block if the future is not ready yet.
	/** To control the waiting, use Future::wait() before you receive the value */
	operator const T &() const {
		return get();
	}

	///Return new future which is resolved by result of current future in 1:1 transformation
	/** Isolated future is independent to original future, it has own observer list and
	own resolution state. This is important if used along with function cancel()
	*/
	Future<T> isolate() const {
		Future<T> res;
		Promise<T> pres = res.getPromise();
		pres.setValue(*this);
		return res;
	}

	///Cancels future waiting
	/**
	Allows to future to cancel waiting. Canceled future becomes resolved with the exception
	CanceledFuture. 

	There is difference between Future<T>::cancel() and Promise<T>::setException. The function
	cancel() performs operation instantly, even if calling the observers may take some time.
	Observers are instantly removed and the future is set to resolved, so it is
	impossible that owner will call setValue and interrupt the cancelation process.

	*/
	void cancel() {
		try {
			throw CanceledPromise();
		}
		catch (...) {
			value->cancel(std::current_exception());
		}
	}

	///Futures are equal if they are shared from the same source
	bool operator==(const Future &other) const {
		return value == other.value;
	}
	///Futures are equal if they are shared from the same source
	bool operator!=(const Future &other) const {
		return value == other.value;
	}

protected:

	RefCntPtr<Internal> value;




};





template<typename T>
class Promise {

	class Internal: public Future<T>::Internal {
	public:

		void addRef() {Future<T>::Internal::addRefPromise();}
		bool release() {return Future<T>::Internal::releasePromise();}
	};

public:

	Promise() :value(nullptr) {}

	Promise(RefCntPtr<typename Future<T>::Internal> value):value(RefCntPtr<Internal>::staticCast(value)) {}

	void setValue(const T &v) const {
		value->resolve(v);
	}

	void setValue(T &&v) const {
		value->resolve(std::move(v));

	}

	void setValue(const Future<T> &v) const {
		Promise<T> me = *this;
		class Observer: public AbstractPromiseObserver<T> {
		public:
			Promise<T> me;
			Observer(const Promise<T> &me):me(me) {}
			virtual void operator()(const T &value) throw() {
				me.setValue(value);
				delete this;
			}
			virtual void operator()(const std::exception_ptr &exception) throw() {
				me.setException(exception);
				delete this;
			}
		};
		v.addObserver(new Observer(me));
	}

	
	///Sets exception to specified exception ptr
	void setException(const std::exception_ptr &p) const {
		value->resolve(p);
	}

	///Sets exception using current exception
	void setException() const {
		value->resolve(std::current_exception());
	}

	///Sets exception to specified object
	template<typename X>
	void setException(const X &exp) const throw() {
		try {
			throw exp;
		}
		catch (...) {
			value->resolve(std::current_exception());
		}
	}

	bool operator==(const Promise<T> &other) const {return value == other.value;}
	bool operator!=(const Promise<T> &other) const {return value != other.value;}

protected:
	RefCntPtr<Internal> value;


};

namespace _hlp {

template<typename T, typename FnRet, typename Fn>
class ChainValueObserver:public AbstractPromiseObserver<T> {
public:

	typedef typename _hlp::FutureHandlerReturn<T,FnRet>::T ChRet;

	ChainValueObserver(const typename ChRet::PromiseT &promise, const Fn &fn)
		:promise(promise),fn(fn) {}

	virtual void operator()(const T &value) throw() {
		try {
			promise.setValue(fn(value));
		} catch (...) {
			promise.setException(std::current_exception());
		}
		delete this;
	}
	virtual void operator()(const std::exception_ptr &exception) throw() {
		promise.setException(exception);
		delete this;
	}


	static ChRet makeChain(const Future<T> &future, const Fn &fn) {
		ChRet ret;
		future.addObserver(new ChainValueObserver(ret.getPromise(), fn));
		return std::move(ret);
	}

protected:
	typename ChRet::PromiseT promise;
	Fn fn;
};

template<typename T, typename Fn>
class ChainValueObserver<T,void,Fn>:public AbstractPromiseObserver<T> {
public:
	ChainValueObserver(const Fn &fn):fn(fn) {}

	virtual void operator()(const T &value) throw() {
		fn(value);
		delete this;
	}
	virtual void operator()(const std::exception_ptr &) throw() {
		delete this;
	}


	static const Future<T> &makeChain(const Future<T> &future, const Fn &fn) {
		future.addObserver(new ChainValueObserver(fn));
		return future;
	}

protected:
	Fn fn;
};

template<typename T, typename FnRet, typename Fn>
class ChainExceptionObserver:public AbstractPromiseObserver<T> {
public:

	typedef typename _hlp::FutureHandlerReturn<T,FnRet>::T ChRet;

	ChainExceptionObserver(const typename ChRet::PromiseT &promise, const Fn &fn)
		:promise(promise),fn(fn) {}

	virtual void operator()(const T &value) throw() {
		try {
			promise.setValue(value);
		} catch (...) {
			this->operator ()(std::current_exception());
		}
		delete this;
	}
	virtual void operator()(const std::exception_ptr &exception) throw() {
		try {
			promise.setValue(fn(exception));
		} catch (...) {
			promise.setException(std::current_exception());
		}
		delete this;
	}


	static ChRet makeChain(const Future<T> &future, const Fn &fn) {
		ChRet ret;
		future.addObserver(new ChainExceptionObserver(ret.getPromise(), fn));
		return std::move(ret);
	}

protected:
	typename ChRet::PromiseT promise;
	Fn fn;
};

template<typename T, typename Fn>
class ChainExceptionObserver<T,void,Fn>:public AbstractPromiseObserver<T> {
public:
	ChainExceptionObserver(const Fn &fn):fn(fn) {}

	virtual void operator()(const T &) throw() {
		delete this;
	}
	virtual void operator()(const std::exception_ptr &ptr) throw() {
		fn(ptr);
		delete this;
	}

	static const Future<T> &makeChain(const Future<T> &future, const Fn &fn) {
		future.addObserver(new ChainExceptionObserver(fn));
		return future;
	}

protected:
	Fn fn;
};

template<typename T, typename FnRet, typename Fn>
class ChainAnythingObserver:public AbstractPromiseObserver<T> {
public:

	typedef typename _hlp::FutureHandlerReturn<T,FnRet>::T ChRet;

	ChainAnythingObserver(const typename ChRet::PromiseT &promise, const Fn &fn)
		:promise(promise),fn(fn) {}

	virtual void operator()(const T &) throw() {
		try {
			promise.setValue(fn());
		} catch (...) {
			this->operator ()(std::current_exception());
		}
		delete this;
	}
	virtual void operator()(const std::exception_ptr &exception) throw() {
		promise.setException(exception);
		delete this;
	}

	static ChRet makeChain(const Future<T> &future, const Fn &fn) {
		ChRet ret;
		future.addObserver(new ChainAnythingObserver(ret.getPromise(), fn));
		return std::move(ret);
	}

protected:
	typename ChRet::PromiseT promise;
	Fn fn;
};

template<typename T, typename Fn>
class ChainAnythingObserver<T,void,Fn>:public AbstractPromiseObserver<T> {
public:
	ChainAnythingObserver(const Fn &fn):fn(fn) {}

	virtual void operator()(const T &) throw() {
		fn();
		delete this;
	}
	virtual void operator()(const std::exception_ptr &ptr) throw() {
		delete this;
	}

	static const Future<T> &makeChain(const Future<T> &future, const Fn &fn) {
		future.addObserver(new ChainAnythingObserver(fn));
		return future;
	}

protected:
	Fn fn;
};

}


///Define function which is called when the future is resolved
/**
	* @param future future which is going to be resolved
	* @param fn function, which accepts the argument T (type of the future). The function can return value of
	*  the same type, other type, void or Future of any type. The function can also throw an exception, which
	*  is caught and used to resolve the returned Future
	*
	* @return Depends on return value of the function. It is generally Future<X> where X is type of return value of the
	* function fn. In case, that fn has no return value, result is Future<T>, because the function doesn't produces
	* the result, source result is used instead.
	*
	* @note function is not called when the future is resolved by an exception
	*/
template<typename T, typename Fn>
auto operator >> (const Future<T> &future, const Fn &fn) -> typename _hlp::FutureHandlerReturn<T, typename std::result_of<Fn(T)>::type >::T {
	typedef typename std::result_of<Fn(T)>::type FnRet;
	return _hlp::ChainValueObserver<T, FnRet, Fn>::makeChain(future, fn);
}
///Define exception handler, which is called when the future is resolved using an exception.
/**
 * @param future future which is going to be resolved
 * @param fn function, which accepts the argument T (type of the future). The function can return value of
 *  the same type, other type, void or Future of any type. The function can also throw an exception, which
 *  is caught and used to resolve the returned Future
 *
 * @return Depends on return value of the function. It is generally Future<X> where X is type of return value of the
 * function fn. In case, that fn has no return value, result is Future<T>, because the function doesn't produces
 * the result, source result is used instead.
 *
 * 	 * @note function is not called when the future is resolved by a value
 *
 */

template<typename T, typename Fn>
auto operator >> (const Future<T> &future, const Fn &fn) -> typename _hlp::FutureHandlerReturn<T, typename std::result_of<Fn(std::exception_ptr)>::type >::T {
	typedef typename std::result_of<Fn(std::exception_ptr)>::type  FnRet;
	return _hlp::ChainExceptionObserver<T, FnRet, Fn>::makeChain(future,fn);
}

///Define a function without arguments, which is called when future is resolved
/**
 *
 * @param future future which is going to be resolved
 * @param fn function without arguments, but it is allowed to return a value. If the function returns void,
 * then original value is passed to the next handler. Otherwise the returned value is passed to the next handler.
 * This variation is equivalent to have function with argument which is ignored however, using lambdas
 * without arguments is much convenient.
 *
 * @return Depends on return value of the function. It is generally Future<X> where X is type of return value of the
 * function fn. In case, that fn has no return value, result is Future<T>, because the function doesn't produces
 * the result, source result is used instead.
 */
template<typename T, typename Fn>
auto operator >> (const Future<T> &future, const Fn &fn) -> typename _hlp::FutureHandlerReturn<T, typename std::result_of<Fn()>::type >::T {
	typedef typename std::result_of<Fn()>::type  FnRet;
	return _hlp::ChainAnythingObserver<T, FnRet, Fn>::makeChain(future,fn);
}

template<typename T>
Future<T> operator >> (const Future<T> &future, const AlertFn &fn) {
	return _hlp::ChainAnythingObserver<T, void, AlertFn>::makeChain(future, fn);
}

template<typename T>
Future<T> operator >> (const Future<T> &future, const Checkpoint &fn) {
	return _hlp::ChainAnythingObserver<T, void, Checkpoint>::makeChain(future, fn);
}


template<typename T>
inline Promise<T> Future<T>::getPromise() const {
	return Promise<T>(value);
}

///Handles operator >> with return value through the future		
template<typename Fn, typename RetV>
struct RunThreadFn {
	typedef Future<RetV> ReturnType;
	static Future<RetV> runThread(const Fn &fn) {
		Future<RetV> f;
		Promise<RetV> p = f.getPromise();
		std::thread t([p, fn] {
			try {
				p.setValue(fn());
			}
			catch (...) {
				p.setException(std::current_exception());
			}
		});
		t.detach();
		return f;
	}
};


}

