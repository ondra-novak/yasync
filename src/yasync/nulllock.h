#pragma once

namespace yasync {

///Allows to disable locking if used instead a mutex as template argument
class NullLock {
public:
	void lock() throw() {}
	void unlock() throw() {}
	bool tryLock() throw() {return true;}
};

///Contains reference to a NullLock.
extern NullLock nullLock;


}
