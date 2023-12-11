#pragma once

#include <vector>
#include <atomic>

class SpinLock {
	std::atomic_flag flag;
public:
	void lock() {
		while (flag.test_and_set(std::memory_order_acquire)) {
			while (flag.test(std::memory_order_relaxed));
		}
	}

	void unlock() {
		flag.clear(std::memory_order_release);
	}
};

// one vector to store all the potential data, another vector to store the active data
// * is thread safe
template <class val>
class net_exclusive_dual_vector {
public:

	net_exclusive_dual_vector(const int size) : max_size(size) {
		data_vec.reserve(max_size);
		alive_vec.reserve(max_size);
	}

	~net_exclusive_dual_vector() {
		for (auto& p : alive_vec) {
			delete *p;
		}
	}

	const int max_size;

	std::atomic<int> _size{ 0 };

	using pointer      = val*;
	using data_pointer = pointer*;

	std::vector<pointer>       data_vec; // storage for the potential elements
	std::vector<data_pointer> alive_vec; // iterators of elements that are active (their location in the data_vec)

	SpinLock arrModifyLock;

public:

	inline void kill(data_pointer iter) {
		delete *iter;
		*iter = nullptr;
	}

	inline int insert_unchecked(pointer const new_p) {
		const int size = _size.load();

		std::lock_guard lock{ arrModifyLock };

		for (auto& p : data_vec) {
			if (p == nullptr) {
				p = new_p;

				alive_vec.push_back(&p);

				_size.store(size + 1);
				return static_cast<int>(&p - data_vec.data());
			}
		}

		// if all potential elements are active, add to the back
		data_vec.push_back(new_p);
		alive_vec.push_back(&data_vec.back());

		_size.store(size + 1);
		return size;
	}

	inline void initiate_delete() {

		std::lock_guard lock{ arrModifyLock };

		std::erase_if(alive_vec,
			[](const auto& p) -> bool {
				return *p == nullptr;
			});

		_size.store(static_cast<int>(alive_vec.size()));
	}

	inline bool is_full() const { return _size == max_size; }

	inline data_pointer* begin() { return alive_vec.data(); }

	inline data_pointer* end() { return alive_vec.data() + _size; }
};