#pragma once


// STANDARD

#include <string>
#include <concepts>
#include <expected>

// EXTERNAL
#define ASIO_STANDALONE
//#define ASIO_NO_EXCEPTIONS

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#include "asio.hpp"
#include "gef.hpp"
#include "concurrentqueue/blockingconcurrentqueue.h"

// lib
#include "error.hpp"

using namespace asio::ip;

namespace net {

	// mutable buffer, can modify pointed data
	using mut_buf = asio::mutable_buffer;

	// constant buffer, cannot modify pointed data
	using const_buf = asio::const_buffer;

	template <typename T>
	struct msg;

	template <typename T>
	using msg_ref = msg<T const&>;

	template <class T>
	struct vectorize_msg {};



	struct header_client_TCP {
		inline static constexpr size_t header_size = 6;

		u32 size;
		i16 msg_type;
	};

	struct header_server_TCP {
		inline static constexpr size_t header_size = 8;

		u32 size;
		i16 msg_type;
		i16 from_id;
	};

	struct header_client_UDP {
		inline static constexpr size_t header_size = 2;

		i16 msg_type;
	};

	struct header_server_UDP {
		inline static constexpr size_t header_size = 4;

		i16 msg_type;
		i16 from_id;
	};

	template <typename Buf, typename Header>
		requires (std::same_as<Buf, mut_buf> || std::same_as<Buf, const_buf>)
	static Buf header_to(Header& h) noexcept {
		return { &h, Header::header_size };
	}

	// Creates a vector of buffers
	// 
	// * A sequence of const_buf's includes the header buffer as its first buffer, because that's what you initially read, the header
	// and sets the header size (accumulates all the sizes of the vectorized buffers)
	// 
	// * A sequence of mut_buf's just forwards the vectorized buffers `...Buffers`
	template <bool IncludeHeaderBuf, typename Buf, std::same_as<Buf> ...Buffers>
		requires (std::same_as<Buf, mut_buf> || std::same_as<Buf, const_buf>)
	constexpr std::vector<Buf> build_custom_buf_seq(Buffers ...buf_seq) noexcept {
		if constexpr (IncludeHeaderBuf) {
			return { Buf{}, std::forward<Buf>(buf_seq)... };
		}
		else {
			return { std::forward<Buf>(buf_seq)... };
		}
	}

	class any_msg {
	public:
		virtual ~any_msg() noexcept {};

		virtual std::vector<const_buf> const_buf_seq(i16&) const noexcept = 0;

		virtual std::vector<mut_buf> mut_buf_seq() noexcept = 0;

		virtual std::vector<mut_buf> mut_buf_seq_with_header(i16&) noexcept = 0;

		// cast to specific msg instance
		template <typename U, typename M = msg<U>, typename Self>
			requires(std::derived_from<M, any_msg>)
		constexpr auto& as(this Self& self) noexcept {
			if constexpr (std::is_const_v<Self>) {
				return static_cast<M const&>(self);
			}
			else {
				return static_cast<M&>(self);
			}
		}
	};

	enum class protocol : i8 {
		tcp,
		udp
	};

	template <typename Header>
	struct packet_tcp {
	public:

		packet_tcp(packet_tcp&) = delete;
		packet_tcp(packet_tcp const&) = delete;
		packet_tcp(packet_tcp&&) = default;

		packet_tcp() noexcept {}

		packet_tcp(i16 msg_t) noexcept :
			h(Header{ .msg_type{msg_t} }), m(gef::nullopt)
		{}

		packet_tcp(gef::unique_ref<any_msg> m) noexcept :
			m(std::move(m))
		{}

		~packet_tcp() noexcept {}

		constexpr bool is_header_only() const noexcept {
			return m.is_null(); // no message content
		}

		std::vector<const_buf> const_buf_seq() noexcept {
			return m.map_or_else(
				[&](gef::unique_ref<any_msg> const& m) -> std::vector<const_buf> {
					auto vec = m->const_buf_seq(h.msg_type);

					h.size = static_cast<u32>(std::ranges::fold_left(vec.begin() + 1, vec.end(), 0, [](size_t&& sum, const_buf& next) { return sum + next.size(); }));

					new (&vec[0]) const_buf(header_to<const_buf>(h));

					return vec;
				},
				[&]() -> std::vector<const_buf> {
					return { header_to<const_buf>(h) };
				}
			);
		}

	public:
		gef::option<gef::unique_ref<any_msg>> m;
		Header h;
	};

	template <typename Header>
	struct packet_udp {
	public:

		packet_udp(packet_udp&) = delete;
		packet_udp(packet_udp const&) = delete;
		packet_udp(packet_udp&&) = default;

		packet_udp() noexcept {}

		packet_udp(gef::unique_ref<any_msg> m) noexcept :
			m(std::move(m))
		{}

		~packet_udp() noexcept {}

		std::vector<const_buf> const_buf_seq() noexcept {
			auto vec = m->const_buf_seq(h.msg_type);

			new (&vec[0]) const_buf(header_to<const_buf>(h));

			return vec;
		}

		std::vector<mut_buf> mut_buf_seq() noexcept {
			auto vec = m->mut_buf_seq_with_header(h.msg_type);

			new (&vec[0]) mut_buf(header_to<mut_buf>(h));

			return vec;
		}

	public:
		gef::unique_ref<any_msg> m;
		Header h;
	};
}