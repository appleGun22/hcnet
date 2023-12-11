#pragma once

#include "canyon.hpp"
#include <type_traits>

namespace net {

	namespace _lib {
		template <class>
		inline constexpr bool is_const_reference_v = false;

		template <class T>
		inline constexpr bool is_const_reference_v<const T&> = true;

		template <typename T>
		concept is_lvalue_or_const_ref = std::is_same_v<std::remove_cvref_t<T>, T> || is_const_reference_v<T>;

		template <typename T>
		concept vectorize_msg_is_specified = requires(const T t) {
			{ vectorize_msg<T>{}.template operator() < const_buf > (t) } -> std::same_as<std::vector<const_buf>>;
		};
	}

	/// A message that takes ownership of `base`
	template <typename T> requires(_lib::is_lvalue_or_const_ref<T>)
	struct msg : public IS_NET_MSG {

		using pure_T = std::remove_cvref_t<T>;

		static_assert(_lib::vectorize_msg_is_specified<pure_T>, "`vectorize_msg` is wrongly/not specified for this msg type, or, 'msg.h' is not included at the bottom of the file that specifies `vectorize_msg`s");

		template <typename ...Args>
		msg(Args&&... args) : base(std::forward<Args>(args)...) {}

		T base;

		std::vector<const_buf> custom_const_buf() const {
			return custom_buf_seq<const_buf>();
		}

		std::vector<mut_buf> custom_mut_buf() const {
			return custom_buf_seq<mut_buf>();
		}

		template <typename Buf>
		std::vector<Buf> custom_buf_seq() const {
			return vectorize_msg<pure_T>{}.template operator()<Buf>(base);
		}
	};

	/// A message that holds a const reference to `base`, does not own it
	template <typename T>
	using msg_ref = msg<const T&>;
}