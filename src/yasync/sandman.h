#pragma once

#include <condition_variable>
#include "timeout.h"

#include "alertfn.h"
namespace yasync {

	class SandMan: public AbstractAlertFunction {
	public:


		SandMan();
		virtual void wakeUp(const std::uintptr_t *reason = nullptr) throw();
		virtual bool sleep(const Timeout &tm, std::uintptr_t *reason = nullptr) ;
		virtual std::uintptr_t halt();

	protected:
		std::mutex mutx;
		std::condition_variable condVar;
		std::uintptr_t reason;
		bool alerted;

	};

}
