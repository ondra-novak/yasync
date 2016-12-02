#pragma once

#include "future.h"

namespace yasync {

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
		typedef Future<RetV> RetT;

		static RetT dispatch(AbstractDispatcher *disp, const Fn &fn) {
			Future<RetV> f;
			Promise<RetV> p = f.getPromise();
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



}
