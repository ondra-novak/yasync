#pragma once

#include "future.h"

namespace yasync {

	enum _XEndChain {
		endChain
	};

	namespace _hlp {

		template<typename T>
		class SafeDispatchFutureValue {
		public:
			SafeDispatchFutureValue(const T &value, const Promise<T> &promise) :value(value), promise(promise), processed(false) {}
			SafeDispatchFutureValue(T &&x) :value(std::move(x.value)), promise(std::move(x.promise)), processed(x.processed) {
				x.processed = true;
			}
			~SafeDispatchFutureValue() {
				try {
					if (!processed) {
						throw CanceledPromise();
					}
				}
				catch (...) {
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
			SafeDispatchFutureException(const std::exception_ptr &except, const Promise<T> &promise) :except(except), promise(promise), processed(false) {}
			SafeDispatchFutureException(T &&x) :except(std::move(x.except)), promise(std::move(x.promise)), processed(x.processed) {
				x.processed = true;
			}
			~SafeDispatchFutureException() {
				try {
					if (!processed) {
						throw CanceledPromise();
					}
				}
				catch (...) {
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
			DispatchObserver(const Promise<T> &promise, const DispatchFn &dispatcher) :promise(promise), dispatcher(dispatcher) {

			}
			virtual void operator()(const T &value) throw() {
				dispatcher >> SafeDispatchFutureValue<T>(value, promise);
				delete this;

			}
			virtual void operator()(const std::exception_ptr &exception) throw() {
				dispatcher >> SafeDispatchFutureException<T>(exception, promise);
				delete this;

			}
		protected:
			Promise<T> promise;
			DispatchFn dispatcher;

		};

	}

	///Intermediate object result of chaining the future with a dispatcher
	/** This intermediate object, it should not be stored in a variable. It is created when you chain
	*  a future with a dispatcher. It allows to chain additional functions to the future. The destructor
	*  of this object finally routes the chain through the dispatcher and connects it to the original
	*  future. This causes, that even resolved future will call the chain through the dispatcher. This prevents
	*  to various race conditions
	*/


	template<typename Initial, typename Final = Future<Initial> >
	class DispatchedFuture {

	protected:
		class Observer : public AbstractPromiseObserver<Initial>
		{
		public:
			Observer(const Promise<Initial> &target, const DispatchFn &dispatcher)
				:target(target), dispatcher(dispatcher) {}

			virtual void operator()(const Initial &value) throw() {

				Promise<Initial> f(target);
				Initial v(value);
				dispatcher >> [f, v] { f.setValue(v); };
				delete this;

			}
			virtual void operator()(const std::exception_ptr &exception) throw() {
				Promise<Initial> p(target);
				std::exception_ptr e(exception);
				dispatcher >> [p, e] { p.setException(e); };
				delete this;
			}

			Promise<Initial> target;
			Future<Initial> source;
			DispatchFn dispatcher;
		};

		template<typename X>
		void initEndChain(const X &) {
			//empty - cannot init endChain with argument of differen type
		}

		void initEndChain(const Final &x) {
			endChain = x;
		}

	public:

		typedef typename Final::Type Type;
		typedef typename Final::PromiseT PromiseT;


		DispatchedFuture(const Future<Initial> &connectTo, const DispatchFn &dispatcher)
			:connectTo(connectTo), endChain(nullptr), dispatcher(dispatcher) 
		{
			initEndChain(firstItem);
		}

		template<typename Z>
		DispatchedFuture(const DispatchedFuture<Initial, Z> &cur, const Final &endChain)
			:connectTo(cur.connectTo)
			, firstItem(cur.firstItem)
			, endChain(endChain)
			, dispatcher(cur.dispatcher)
		{}

		~DispatchedFuture() {
			connect();
		}


		void connect() const {
			if (!firstItem.hasPromise()) {
				connectTo.addObserver(new Observer( firstItem.getPromise(), dispatcher));
			}
		}

		operator Final() {
			connect();
			return endChain;
		}

//		static Final fakeFinalVal;

		template<typename X> using FutX = decltype(Final() >> (*(X *)nullptr));

		template<typename Fn>
		DispatchedFuture<Initial, FutX<Fn> > operator >> (const Fn &fn)   {

			typedef DispatchedFuture<Initial, FutX<Fn> > Ret;
			return Ret(std::move(*this), endChain >> fn);
		}


		bool hasPromise() const { 
			return endChain.hasPromise();
		}

		bool isResolved() const { 
			return endChain.isResolved();
		}

		const Type *tryGetValue() const { 
			return endChain.tryGetValue();
		}

		std::exception_ptr getException() const { 
			return endChain.getException();
		}

		void addObserver(AbstractPromiseObserver<Type> *obs) const {
			return endChain.addObserver(obs);
		}

		bool removeObserver(AbstractPromiseObserver<Type> *obs) const {
			return endChain.removeObserver(obs);
		}

		void wait() const {
			connect();
			return endChain.wait();
		}

		bool wait(const Timeout &tm) const {
			connect();
			return endChain.wait();
		}

		const Type &getValue() const {
			connect();
			return endChain.getValue();
		}

		const Type &get() const {
			connect();
			return endChain.get();
		}

		operator const Type &() const {
			return get();
		}

		DispatchedFuture isolate() const {
			return DispatchedFuture(*this, endChain.isolate());
		}

		operator Final() const {
			connect();
			return endChain;
		}

		Final operator >> (_XEndChain) const {
			connect();
			return endChain;
		}


	protected:
		template<typename X, typename Y> friend class DispatchedFuture;

		Future<Initial> connectTo;
		Future<Initial> firstItem;
		Final endChain;
		DispatchFn dispatcher;


	};

#if 0
	template<typename Src, typename Trg>
	class DispatchedFuture {
	public:
		DispatchedFuture(const Future<Src> connectTo, const DispatchFn &dispatcher)
			:connectTo(connectTo), dispatcher(dispatcher), connected(false) {}
		~DispatchedFuture() {
			if (!connected) connect();
		}
		DispatchedFuture(DispatchedFuture &&other)
			:connectTo(std::move(other.connectTo)), dispatcher(std::move(other.dispatcher)), connected(other.connected) {
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
#endif
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
	class DispatchedFunction {
	public:
		typedef typename _hlp::FutureHandlerReturn<Void, RetV >::T RetT;

		static RetT dispatch(AbstractDispatcher *disp, const Fn &fn) {
			RetT f;
			auto p = f.getPromise();
			auto fnx = [fn,p] {
				try {
					p.setValue(fn());
				}
				catch (...) {
					p.setException(std::current_exception());
				}
			};
			disp->dispatch(new DispatchedFunction<decltype(fnx),void>(fnx));
			return f;
		}
	};


	template<typename T>
	DispatchedFuture<T> operator >> (const Future<T> &future, _XNewThread) {
		return future >> DispatchFn::newThread();
	}

	template<typename T>
	DispatchedFuture<T> operator >> (const Future<T> &future, _XThisThread) {
		return future >> DispatchFn::thisThread();
	}


}
