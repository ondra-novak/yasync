#include "checkpoint.h"
#include "dispatcher.h"

namespace yasync {



	void Checkpoint::wait()
	{
		while (!get().isSignaled()) {
			halt();
		}
	}

	bool Checkpoint::wait(const Timeout & tm)
	{
		while (!get().isSignaled()) {
			if (sleep(tm)) return false;
		}
		return true;

	}

	void Checkpoint::dispatch()
	{
		while (!get().isSignaled()) {
			haltAndDispatch();
		}
	}

	bool Checkpoint::dispatch(const Timeout & tm)
	{
		while (!get().isSignaled()) {
			if (sleepAndDispatch(tm)) return false;
		}
		return true;
	}

}