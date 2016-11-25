/*
 * thread.cpp
 *
 *  Created on: 25. 11. 2016
 *      Author: ondra
 */


#include "thread.h"

namespace yasync {

class NewThreadDispatch: public AbstractDispatcher {
public:

	NewThreadDispatch() {
		addRef();
	}

	virtual bool dispatch(const Fn &fn) throw() {
		thread >> [fn](){
			fn->run();
		};
	}

};


DispatchFn newThread() {
	static NewThreadDispatch dispatch;
	return DispatchFn(static_cast<AbstractDispatcher *>(&dispatch));
}

}


