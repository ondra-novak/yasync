#pragma once

namespace yasync {


	class Deque {
	public:

		void push_back(const AlertFn &fn) {

		}
		void remove(const AlertFn &fn);
		AlertFn pop_front();
		AlertFn pop_back();
		bool empty();

	protected:
		std::deque<AlertFn> items;
	};


	

}