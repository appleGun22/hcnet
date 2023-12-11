#pragma once

#include <vector>
#include <shared_mutex>

/*
	[ capacity = max possible size ]
	[ size     = current elements that are alive ]

	Time complexity for `dual_vector` (one vector for data and one for alive). Is thread safe.
		iterating - O(size) - sh_mutex
		get       - O(1)
		insert    - O(1)    - uq_mutex
		erase     - O(size) - uq_mutex
		clear     - O(size) - uq_mutex

	Time complexity for `optional_array` (the other approach to implement this class). !Current implementation is not thread safe.
		iterating - O(capacity)
		get       - O(1)
		insert    - O(1)
		erase     - O(1)
		clear     - O(capacity)
*/

template <typename T>
class dual_vector {
public:
	dual_vector() {}

	dual_vector(const i64 size) {
		resize(size);
	}

private:
	std::vector<T>   data_vec; // storage for the potential elements
	std::vector<T*>	alive_vec; // location(pointers) to elements that are active

	std::shared_mutex alive_mutex;

public:

	//! This function may be called only when the vector is unused, if not, UB
	constexpr void resize(const i64 new_capacity) noexcept {
		data_vec.resize(new_capacity);
		alive_vec.reserve(new_capacity);
	}

	template <typename any_T>
	constexpr T& insert(const i64 indx, any_T&& new_obj) noexcept {
		std::unique_lock lock{ alive_mutex };

		data_vec[indx] = std::forward<any_T>(new_obj);

		alive_vec.push_back(&data_vec[indx]);

		return data_vec[indx];
	}

	constexpr const T& operator[](const i64 indx) const noexcept {
		return data_vec[indx];
	}

	constexpr T& mutate(const i64 indx) noexcept {
		return data_vec[indx];
	}

	constexpr void erase(const i64 indx) noexcept {
		std::unique_lock lock{ alive_mutex };

		auto erased_it = alive_vec.erase(std::remove(alive_vec.begin(), alive_vec.end(), (data_vec.begin() + indx)._Ptr));
		(*erased_it)->~T();
	}

	// Invalidates all data
	constexpr void clear() noexcept {
		std::unique_lock lock{ alive_mutex };

		for (T*& obj : alive_vec) {
			obj->~T();
		}

		alive_vec.clear();
	}

	constexpr void for_each(auto&& f) noexcept {
		std::shared_lock lock{ alive_mutex };

		const auto end = alive_vec.end();

		for (auto it = alive_vec.begin(); it != end; it++) {
			f(**it);
		}
	}

	constexpr bool for_each_if(auto&& f) noexcept {
		std::shared_lock lock{ alive_mutex };

		const auto end = alive_vec.end();

		for (auto it = alive_vec.begin(); it != end; it++) {
			if (f(**it))
				return true;
		}

		return false;
	}

	constexpr size_t capacity() const noexcept {
		return data_vec.capacity();
	}

	constexpr size_t size() noexcept {
		std::shared_lock lock{ alive_mutex };
		return alive_vec.size();
	}

	constexpr const T* data_begin() const noexcept {
		return data_vec.data();
	}
};

template <typename T>
class optional_array {
public:
	optional_array() {}

	optional_array(const i64 size) {
		resize(size);
	}

private:
	i64 _capacity{ 0 };

	std::vector<std::optional<T>> data;

public:

	constexpr void resize(const i64 new_capacity) noexcept {
		data.resize(new_capacity);
		_capacity = new_capacity;
	}

	template <typename any_T>
	constexpr T& insert(const i64 indx, any_T&& new_obj) noexcept {
		data[indx] = std::forward<any_T>(new_obj);
		return data[indx];
	}

	constexpr const T& operator[](const i64 indx) const noexcept {
		return data[indx].value();
	}

	constexpr T& mutate(const i64 indx) noexcept {
		return data[indx].value();
	}

	constexpr void erase(const i64 indx) noexcept {
		data[indx].reset();
	}

	constexpr void clear() noexcept {
		for (auto& obj : data) {
			obj.reset();
		}
	}

	constexpr void for_each(auto&& f) const noexcept {
		const auto end = data.end();

		for (auto it = data.begin(); it != end; it++) {
			if (it->has_value())
				f(*it);
		}
	}

	constexpr bool for_each_if(auto&& f) const noexcept {
		const auto end = data.end();

		for (auto it = data.begin(); it != end; it++) {
			if (it->has_value() && f(*it))
				return true;
		}

		return false;
	}

	constexpr i64 capacity() const noexcept {
		return _capacity;
	}

	constexpr auto begin() const noexcept {
		return data.data();
	}
};