#pragma once

#include "canyon.hpp"
#include <type_traits>

namespace net {

	namespace detail {
		template <class>
		inline constexpr bool is_const_reference_v = false;

		template <class T>
		inline constexpr bool is_const_reference_v<T const&> = true;

		template <typename T>
		concept is_lvalue_or_const_ref = std::is_same_v<std::remove_cvref_t<T>, T> || is_const_reference_v<T>;
		
		template <typename T>
		concept vectorize_msg_is_specified =
			requires(T const& t) {
				{ vectorize_msg<T>::template vectorize<true, const_buf>(t) } -> std::same_as<std::vector<const_buf>>;
			}
			&&
			sizeof(vectorize_msg<T>::identifier) == sizeof(i16);
	}

	/// A message that takes ownership of `inner`, if isn't a msg_ref
	template <typename T>
	struct msg : public any_msg {

		using pure_T = std::remove_cvref_t<T>;

		static_assert(detail::is_lvalue_or_const_ref<T>);
		static_assert(detail::vectorize_msg_is_specified<pure_T>, "`vectorize_msg` is wrongly/not specified for this msg type, or, 'msg.h' is not included at the bottom of the file that specifies `vectorize_msg`s");

		template <typename ...Args>
			requires(std::constructible_from<T, Args...>)
		msg(Args&&... args) noexcept : inner(std::forward<Args>(args)...) {}

		virtual std::vector<const_buf> const_buf_seq(i16& msg_type) const noexcept {
			msg_type = vectorize_msg<pure_T>::identifier;

			return vectorize_msg<pure_T>::template vectorize<true, const_buf>(inner);
		}

		virtual std::vector<mut_buf> mut_buf_seq() noexcept {
			return vectorize_msg<pure_T>::template vectorize<false, mut_buf>(inner);
		}

		virtual std::vector<mut_buf> mut_buf_seq_with_header(i16& msg_type) noexcept {
			msg_type = vectorize_msg<pure_T>::identifier;

			return vectorize_msg<pure_T>::template vectorize<true, mut_buf>(inner);
		}

	public:
		T inner;
	};
}