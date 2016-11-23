#pragma once

#include <cstdint>
#include <chrono>

namespace yasync {

class Timeout {
public:

	typedef std::chrono::steady_clock::time_point Clock;

	///Currently expires
	Timeout();
	///Never expires
	Timeout(std::nullptr_t x);
	///Expire at specified time
	Timeout(const Clock &clock);
	///Expires after specified miliseconds
	Timeout(std::uintptr_t ms);
	///Expires after specified duration
	template<typename Rep, typename Period>
	Timeout(const std::chrono::duration<Rep,Period> &dur);


	///returns time when expires.
	/** However, if timeout is set to "never expires" return value is unspecified */
	operator Clock() const {
		return pt;
	}

	///compare two timeouts
	bool operator>(const Timeout &tm) const;
	///compare two timeouts
	bool operator<(const Timeout &tm) const;
	///compare two timeouts
	bool operator==(const Timeout &tm) const;
	///compare two timeouts
	bool operator!=(const Timeout &tm) const;
	///compare two timeouts
	bool operator>=(const Timeout &tm) const;
	///compare two timeouts
	bool operator<=(const Timeout &tm) const;

	///returns true if not expired
	bool operator!() const;
	///returns true expired
	operator bool() const;

protected:
	Clock pt;
	bool neverExpires;

	int compare(const Timeout &tm) const;


};


inline Timeout::Timeout():pt(std::chrono::steady_clock::now()),neverExpires(false) {}
inline Timeout::Timeout(std::nullptr_t x):neverExpires(true) {}
inline Timeout::Timeout(const Clock& clock):pt(clock),neverExpires(false) {}
inline Timeout::Timeout(std::uintptr_t ms):pt(std::chrono::steady_clock::now()+std::chrono::milliseconds(ms)),neverExpires(false) {}

template<typename Rep, typename Period>
inline Timeout::Timeout(const std::chrono::duration<Rep, Period>& dur)
:pt(std::chrono::steady_clock::now()+dur),neverExpires(false) {}

inline bool Timeout::operator >(const Timeout& tm) const {
	if (neverExpires) {
		if (tm.neverExpires) return false;
		return true;

}

inline bool Timeout::operator <(const Timeout& tm) const {
}

inline bool Timeout::operator ==(const Timeout& tm) const {
}

inline bool Timeout::operator !=(const Timeout& tm) const {
}

inline bool Timeout::operator >=(const Timeout& tm) const {
}

inline bool Timeout::operator <=(const Timeout& tm) const {
}

inline bool Timeout::operator !() const {
}

inline Timeout::operator bool() const {
}




}
