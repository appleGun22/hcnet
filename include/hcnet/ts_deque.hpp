#pragma once

#include "canyon.hpp"

namespace net {

	// minimal thread safe deque implementation, just for the library needs
	template <class OBJ>
	class tsQueueMini {
	private:
		std::queue<OBJ> q;
		std::mutex m_mutex;
		std::condition_variable m_cv;

	public:

		OBJ& front() noexcept {
			std::lock_guard lock{ m_mutex };
			return q.front();
		}

		bool pop_check_empty() noexcept {
			std::lock_guard lock{ m_mutex };
			q.pop();

			return q.empty();
		}

		void emplace(auto&&... a) noexcept {
			std::lock_guard lock{ m_mutex };
			q.emplace(std::forward<decltype(a)>(a)...);

			m_cv.notify_one();
		}

		bool empty() noexcept {
			std::lock_guard lock{ m_mutex };
			return q.empty();
		}

		/*
		const OBJ& wait_front() noexcept {
			return conditional_pop_wait_front<false>();
		}

		const OBJ& pop_wait_front() noexcept {
			return conditional_pop_wait_front<true>();
		}

		template <bool pop>
		const OBJ& conditional_pop_wait_front() noexcept {
			std::unique_lock ulock{ m_mutex };

			if constexpr (pop) {
				q.pop();
			}

			if (!q.empty()) { // check first
				return q.front();
			}

			m_cv.wait(ulock, [&q = q]() { return !q.empty(); }); // then wait

			return q.front();
		}
		*/
	};
}