#pragma once


// STANDARD

#include <string>
#include <queue>
#include <iostream>
#include <concepts>

// EXTERNAL
#define ASIO_STANDALONE
//#define ASIO_NO_EXCEPTIONS

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#include <asio.hpp>

// lib
#include "better_types.hpp"
#include "error.hpp"
#include "ts_deque.hpp"
#include "mut_order_array.hpp"

using namespace asio::ip;

using str = std::string;
using cstr = const char*;

#define NET_TO_CLIENT_MESSAGES \
	net_client_disconnect, \
	net_server_full, \
	net_new_client, \
	net_unknown_msg_type

namespace net {

	struct net_enum {
		enum Type : i32 {
			NET_TO_CLIENT_MESSAGES
		};
	};

	// mutable buffer, can modify pointed data
	using mut_buf = asio::mutable_buffer;

	// constant buffer, cannot modify pointed data
	using const_buf = asio::const_buffer;


	// *? get rid of virtuals (IS_NET_MSG)

	// Every class that may be sent, should derive from `IS_NET_MSG`
	class IS_NET_MSG {
	public:
		virtual ~IS_NET_MSG() {};

		virtual std::vector<const_buf> custom_const_buf() const = 0;

		virtual std::vector<mut_buf> custom_mut_buf() const = 0;
	};


	template <typename Buf, std::same_as<Buf> ...Buffers>
	constexpr std::vector<Buf> build_custom_buf_seq(Buffers ...buf_seq) noexcept {
		if constexpr (std::is_same_v<Buf, const_buf>) {
			return { const_buf{}, std::forward<Buf>(buf_seq)... }; // empty buf in the beginning, to be replaced with header buf
		}
		else if (std::is_same_v<Buf, mut_buf>) {
			return { std::forward<Buf>(buf_seq)... };
		}
		else {
			static_assert(std::is_same_v<Buf, mut_buf> || std::is_same_v<Buf, const_buf>, "buffer sequence is required to be either mut_buf or const_buf");
		}
	}

	template <class T>
	struct vectorize_msg {};


	struct header {
		header() {}
		// `size` of 0 means header only message
		header(u64 size, i32 msg_type, i32 from_id = 0) : size(size), msg_type(msg_type), from_id(from_id) {}

		u64 size;     // size variable
		i32 msg_type; // user defined type of the incoming message
		i32 from_id; // incoming message from uid

		const_buf to_const_buf() const {
			return const_buf(this, sizeof(*this));
		}

		mut_buf to_mut_buf() {
			return mut_buf(this, sizeof(*this));
		}
	};

	struct PACKET {

		//! `h` must not be a nullptr.
		//! `m` can be a nullptr, if it is, only `h` will be sent.
		PACKET(std::unique_ptr<header> h, std::unique_ptr<IS_NET_MSG> m) : h(std::move(h)), m(std::move(m)) {
			
		}

		// an empty packet (yet to be defined)
		PACKET() : h(new header), m(nullptr) {

		}

		~PACKET() {}

		inline auto const_buf_seq() const {
			auto vec = m->custom_const_buf();
			vec.front() = h->to_const_buf();

			return vec;
		}

		inline auto mut_buf_seq() const {
			return m->custom_mut_buf();
		}

		std::unique_ptr<header> h;
		std::unique_ptr<IS_NET_MSG> m;
	};

	template <class Wire>
	struct msg_ext {
		msg_ext(std::unique_ptr<PACKET> p, Wire* w) : p(std::move(p)), from_wire(w) {}

		std::unique_ptr<PACKET> p;
		Wire* from_wire;
	};
}