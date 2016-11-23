/*
 * sandman.cpp
 *
 *  Created on: 23. 11. 2016
 *      Author: ondra
 */
#include "sandman.h"

namespace yasync {

yasync::SandMan::SandMan():reason(0),alerted(false) {
}

void SandMan::wakeUp(const std::uintptr_t* reason) throw () {
	std::lock_guard<std::mutex> _(mutx);
	alerted = true;
	if (reason) this->reason = *reason;
	condVar.notify_all();
}

bool SandMan::sleep(const Timeout& tm, std::uintptr_t* reason) {
	std::unique_lock<std::mutex> um(mutx);
	while (!alerted) {
		if (tm == nullptr) condVar.wait(um);
		else {
			Timeout::Clock tp = tm;
			std::cv_status status = condVar.wait_until(um, tp);
			if (status == std::cv_status::timeout) {
				return true;
			}
		}
	}
	if (reason) {
		*reason = this->reason;
		this->reason = 0;
	}
	alerted = false;
	return false;
}

static thread_local RefCntPtr<SandMan> curSandman;

static RefCntPtr<SandMan> getCurrentSandman()  {
	if (curSandman == nullptr) {
		return curSandman = new SandMan;
	} else {
		return curSandman;
	}
}


bool sleep(const Timeout &tm, std::uintptr_t *reason)  {
	return getCurrentSandman()->sleep(tm,reason);
}

AlertFn AlertFn::currentThread() {
	RefCntPtr<AbstractAlertFunction> x = RefCntPtr<AbstractAlertFunction>::staticCast(getCurrentSandman());
	return AlertFn(x);
}

}


