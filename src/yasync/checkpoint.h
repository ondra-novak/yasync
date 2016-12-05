#pragma once

#include "alertfn.h"

namespace yasync {


	///Checkpoint is alert receiver and forwarder. It caught the alert and stores the reason
	/**
		The checkpoint can be created with or without arguments. If created without arguments,
		it forwards alert to the current thread. Otherwise it can forward alert anywhere.

		Checkpoints are useful to distinguish alerts forwarded to the single thread. The
		checkpoint also remembers the reason, so it cannot be overwrited by other alerts
		if they are forwarded through other checkpoints

		Checkpoints are always created in heap. Copying the variable causes that single
		checkpoint is shared. Releasing the last refererence destroys checkpoint  
		However, if the checkpoint is registered somewhere, releasing all user
		copies will not destroy the checkpoint. This protects alert process to be forwarded
		into already destroyed checkpoint. Even if the original thread is destroyed, the checkpoint
		is still able to catch the alert

		Checkpoint inherits AlertFn. It acts as alert function. 
		It can be used everywhere where AlertFn is expected.
		You can convert Checkpoint to AlertFn, however you will loose access to internal state
		of the checkpoint. Fortunately, it is still available through any other copy 
		of the same checkpoint

		

	*/

	class Checkpoint: public AlertFn {

		class AlertMonitor : public AbstractAlertFunction {
		public:
			AlertMonitor(const AlertFn &fwd) :fwd(fwd),signaled(false), reason(0) {}

			virtual void wakeUp(const std::uintptr_t *reason = nullptr) throw() {
				signaled = true;
				if (reason) {
					this->reason = *reason;
					fwd(*reason);
				}
				else {
					fwd();
				}
			}

			bool isSignaled() const { return signaled; }
			std::uintptr_t getReason() const { return reason; }
			void reset() {
				signaled = false;
				reason = 0;
			}


		protected:
			AlertFn fwd;
			bool signaled;
			std::uintptr_t reason;
		};

		AlertMonitor &get() {
			return static_cast<AlertMonitor &>(*obj);
		}
		const AlertMonitor &get() const {
			return static_cast<const AlertMonitor &>(*obj);
		}

	public:
		///Create checkpoint.
		/** Checkpoint will catch your alert and forward it to the current thread  */
		Checkpoint() :AlertFn(new AlertMonitor(AlertFn::thisThread())) {}
		///Create checkpoint and specify where to forward the alert
		/** Checkpoint will catch your alert and forward it to specified alert function 
		@param fn alert function where forward the alert
		*/
		Checkpoint(const AlertFn &fn) :AlertFn(new AlertMonitor(fn)) {}		

		
		///test whether checkpoint is not yet signaled
		bool operator !() const { return !get().isSignaled(); }
		///test whether checkpoint is signaled
		operator bool() const { return get().isSignaled(); }

		///Retrieve reason of signaled checkpoint
		std::uintptr_t getReason() const { return get().getReason(); }
		///Reset internal state, so the checkpoint can be reused
		void reset() { get().reset(); }

		using AlertFn::operator();

		///Stop thread until the checkpoint becomes signaled
		void wait();

		///Stop thread until the checkpoint becomes signaled or specified timeout expires
		/**
		@param tm timeout specification
		@retval true signaled
		@retval false timeout
		*/
		bool wait(const Timeout &tm);

		///Dispatch functions while thread waiting for alert
		void dispatch();

		///Dispatch functions while thread waiting for alert, you can specify a timeout
		/**
		@param tm timeout specification
		@retval true signaled
		@retval false timeout
		*/
		bool dispatch(const Timeout &tm);



	};




}