#pragma once

#include <cstdint>
#include <chrono>

namespace yasync {

class Timeout {
public:

	typedef std::chrono::steady_clock::time_point Clock;

	///Currently expires
	Timeout() :pt(std::chrono::steady_clock::now()), neverExpires(false) {}
	///Never expires
	Timeout(std::nullptr_t) :neverExpires(true) {}
	///Expire at specified time
	Timeout(const Clock &clock) : pt(clock), neverExpires(false) {}
	///Expires after specified miliseconds
	Timeout(std::uintptr_t ms) : pt(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)), neverExpires(false) {}
	///Expires after specified duration
	template<typename Rep, typename Period>
	Timeout(const std::chrono::duration<Rep, Period> &dur)
		: pt(std::chrono::steady_clock::now() + dur) {}


	static Timeout infinity;
	static Timeout now() {return Timeout();}

	///returns time when expires.
	/** However, if timeout is set to "never expires" return value is unspecified */
	operator Clock() const {
		return pt;
	}

	///compare two timeouts
	bool operator>(const Timeout &tm) const { return compare(tm) > 0; }
	///compare two timeouts
	bool operator<(const Timeout &tm) const { return compare(tm) < 0; };
	///compare two timeouts
	bool operator==(const Timeout &tm) const { return compare(tm) == 0; };
	///compare two timeouts
	bool operator!=(const Timeout &tm) const { return compare(tm) != 0; };
	///compare two timeouts
	bool operator>=(const Timeout &tm) const { return compare(tm) >= 0; };
	///compare two timeouts
	bool operator<=(const Timeout &tm) const { return compare(tm) <= 0; };

	///returns true if not expired
	bool operator!() const {
		return operator>=(Timeout());
	}
	///returns true expired
	operator bool() const {
		return operator<(Timeout());
	}

	

protected:
	Clock pt;
	bool neverExpires;

	int compare(const Timeout &tm) const {
		if (neverExpires) {
			if (tm.neverExpires) return 0;
			else return 1;
		}
		else if (tm.neverExpires) {
			return -1;
		}
		else {
			if (pt < tm.pt) return -1;
			else if (pt > tm.pt) return 1;
			return 0;
		}
	}


};


}
