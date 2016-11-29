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
	template<typename T> class DispatchedFuture;
	template<> class Promise<void>;
	template<> class Future<void>;

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

		void removeObserver(AbstractPromiseObserver<T> *obs) const throw() {
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

	bool hasPromise() const {return value->hasPromise();}

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
	///Removes observer before the future is resolved
	/**
	@param obs observer to remove. Pointer is subject of comparison
	@retval true removed
	@retval false not found,or already resolved
	*/
	bool removeObserver(AbstractPromiseObserver<T> *obs) const {
		value->removeObserver(obs);
	}

	///This observer generates alert when the Future is resolved. Value is ignored, you have to retrieve it from the Future object
	class AlertObserver: public AbstractPromiseObserver<T> {
	public:
		AlertObserver(const AlertFn &alert) :alert(alert) {}
		virtual void operator()(const T &) throw() {
			alert();
		}
		virtual void operator()(const std::exception_ptr &) throw() {
			alert();
		}
		AlertFn alert;
	};

	///Wait for resolving
	/**
	 Function waits for infinite period until future is resolved 
	 */
	void wait() const {
		if (!isResolved()) {
			AlertObserver obs(AlertFn::currentThread());
			addObserver(&obs);
			while (!isResolved()) {
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
			AlertObserver obs(AlertFn::currentThread());
			addObserver(&obs);
			while (!isResolved())
				if (!sleep(tm)) {
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

		};



	}

	void setException(const std::exception_ptr &p) const {

	}


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
		promise.setValue(exception);
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

template<typename T>
class SafeDispatchFutureValue {
public:
	SafeDispatchFutureValue(const T &value, const Promise<T> &promise):value(value),promise(promise),processed(false) {}
	SafeDispatchFutureValue(T &&x):value(std::move(x.value)),promise(std::move(x.promise)),processed(x.processed) {
		x.processed = true;
	}
	~SafeDispatchFutureValue() {
		try {
			if (!processed) {
				throw CanceledPromise();
			}
		} catch (...) {
			promise.setException(std::current_exception());
		}
	}
	void operator()() const {
		promise.setValue(value);
	}

protected:
	T value;
	mutable Promise<T> promise;
	bool processed;
};

template<typename T>
class SafeDispatchFutureException {
public:
	SafeDispatchFutureException(const std::exception_ptr &except, const Promise<T> &promise):except(except),promise(promise),processed(false) {}
	SafeDispatchFutureException(T &&x):except(std::move(x.except)),promise(std::move(x.promise)),processed(x.processed) {
		x.processed = true;
	}
	~SafeDispatchFutureException() {
		try {
			if (!processed) {
				throw CanceledPromise();
			}
		} catch (...) {
			promise.setException(std::current_exception());
		}
	}
	void operator()() const {
		promise.setException(except);
	}

protected:
	std::exception_ptr except;
	mutable Promise<T> promise;
	bool processed;
};

template<typename T>
class DispatchObserver : public AbstractPromiseObserver<T> {
public:
	DispatchObserver(const Promise<T> &promise, const DispatchFn &dispatcher):promise(promise),dispatcher(dispatcher) {

	}
	virtual void operator()(const T &value) throw() {
		dispatcher >> SafeDispatchFutureValue<T>(value,promise);
		delete this;

	}
	virtual void operator()(const std::exception_ptr &exception) throw() {
		dispatcher >> SafeDispatchFutureException<T>(exception,promise);
		delete this;

	}
protected:
	Promise<T> promise;
	DispatchFn dispatcher;

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
auto operator >> (const Future<T> &future, const Fn &fn) -> typename _hlp::FutureHandlerReturn<T,decltype((*(Fn *)nullptr)(*(T *)nullptr))>::T {
	typedef decltype((*(Fn *)nullptr)(*(T *)nullptr)) FnRet;
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
auto operator >> (const Future<T> &future, const Fn &fn) -> typename _hlp::FutureHandlerReturn<T, decltype((*(Fn *)nullptr)(*(std::exception_ptr *)nullptr))>::T {
	typedef decltype((*(Fn *)nullptr)(*(std::exception_ptr *)nullptr)) FnRet;
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
auto operator >> (const Future<T> &future, const Fn &fn) -> typename _hlp::FutureHandlerReturn<T, decltype((*(Fn *)nullptr)())>::T {
	typedef decltype((*(Fn *)nullptr)()) FnRet;
	return _hlp::ChainAnythingObserver<T, FnRet, Fn>::makeChain(future,fn);
}

///Intermediate object result of chaining the future with a dispatcher
/** This intermediate object, it should not be stored in a variable. It is created when you chain
 *  a future with a dispatcher. It allows to chain additional functions to the future. The destructor
 *  of this object finally routes the chain through the dispatcher and connects it to the original
 *  future. This causes, that even resolved future will call the chain through the dispatcher. This prevents
 *  to various race conditions
 */
template<typename T>
class DispatchedFuture: public Future<T> {
public:
	DispatchedFuture(const Future<T> connectTo, const DispatchFn &dispatcher)
		:connectTo(connectTo),dispatcher(dispatcher),connected(false) {}
	~DispatchedFuture() {
		if (!connected) connect();
	}
	DispatchedFuture(DispatchedFuture &&other)
		:connectTo(std::move(other.connectTo)),dispatcher(std::move(other.dispatcher)),connected(other.connected) {
		other.connected = true;
	}

	void connect() {
		if (!connected) {
			connectTo.addObserver(new _hlp::DispatchObserver<T>(Future<T>::getPromise(), dispatcher));
			connected = true;
		}
	}

	Promise<T> getPromise() = delete;

protected:
	Future<T> connectTo;
	DispatchFn dispatcher;
	bool connected;

};

template<typename T>
inline Promise<T> Future<T>::getPromise() {
	return Promise<T>(value);
}



///Create future which is resolved in other thread (through the dispatcher)
/**
 * @param dispatcher dispatcher used to route processing into other thread
 * @return An intermediate object DispatchedFuture which helps to create chain that have to be processed
 * in other thread. The chain cannot be executed during the connection (this can happen for ordinary chain), it
 * is connected to the future once the DispatchedFuture is destroyed. The object DispatchedFuture is
 * inherited from the Future.
 */
template<typename T>
DispatchedFuture<T> operator >> (const Future<T> &fut, const DispatchFn &dispatcher) {
	return DispatchedFuture<T>(fut, dispatcher);
}

template<typename Fn, typename RetV>
struct RunThreadFn {
	typedef Future<RetV> ReturnType;
	static Future<RetV> runThread(const Fn &fn) {
		Future<RetV> f;
		Promise<RetV> p = f.getPromise();
		std::thread t([p,fn]{p.setValue(fn());});
		t.detach();
		return f;
	}
};


}

