#pragma once

#include "canyon.hpp"
#include "readerwriterqueue/readerwriterqueue.h"

namespace net {

	// Manager      - class that owns (and manages) the socket
	// PacketHolder - the class that manages out-going packets lifetime
	template <typename Manager, template<typename T> class PacketHolder, typename HeaderOut, typename HeaderIn>
	class SocketTCP {
	public:

		using PacketOut = packet_tcp<HeaderOut>;
		using PacketIn = packet_tcp<HeaderIn>;

		// unbound socket, needs to be bounded
		SocketTCP(Manager& manager, asio::io_context& ctx) noexcept :
			manager(manager),
			global_ctx(ctx),
			socket(ctx)
		{}

		// bind the socket to a specific port and address, that is specified in the moved asio socket
		SocketTCP(Manager& manager, asio::io_context& ctx, tcp::socket&& s) noexcept :
			manager(manager),
			global_ctx(ctx),
			socket(std::move(s))
		{}

		~SocketTCP() noexcept {
			out_queue.emplace(PacketHolder<PacketOut>{ nullptr }); // 'Wake up' the wait_dequeue()
		}

		constexpr void Start() noexcept {
			ReadHeader();

			std::thread{ &SocketTCP::Write, this }.detach();
		}

		constexpr void Send(PacketHolder<PacketOut> p) noexcept {
			out_queue.emplace(std::move(p));
		}

	private:

		void ReadHeader() noexcept {

			auto p = gef::unique_ref<PacketIn>::make();

			auto buf = header_to<mut_buf>(p->h);

			socket.async_receive(buf,
				[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
					if (ec) {
						manager.Close({ net_error::failed_to_read, ec });
						return;
					}

					if (p->h.size == 0) {
						ContinueAndNotify(std::move(p));
					}
					else {
						p->m.replace(manager.builder_TCP(p->h))
							.map_or_else(
								[&](gef::unique_ref<any_msg>& m) {
									ReadBody(std::move(p), m.get());
								},
								[&]() {
									manager.Close({ net_error::unknown_msg_type, gef::nullopt });
								});
					}
				});
		}

		void ReadBody(gef::unique_ref<PacketIn> p, any_msg& m) noexcept {

			socket.async_receive(m.mut_buf_seq(),
				[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
					if (ec) {
						manager.Close({ net_error::failed_to_read, ec });
						return;
					}

					ContinueAndNotify(std::move(p));
				});
		}

		void Write() noexcept {
			PacketHolder<PacketOut> p{ nullptr };

			out_queue.wait_dequeue(p);

			if (not manager.connected) {
				return;
			}

			auto bufs = p->const_buf_seq();

			socket.async_send(bufs,
				[this, p = std::move(p)](asio::error_code ec, size_t) {
					if (ec) {
						manager.Close({ net_error::failed_to_write, ec });
						return;
					}
				});

			Write();
		}

		constexpr void ContinueAndNotify(gef::unique_ref<PacketIn>&& p) noexcept {
			ReadHeader();
			manager.NewPacketTCP(std::forward<decltype(p)>(p));
		}

	private:
		Manager& manager;
		asio::io_context& global_ctx;

		moodycamel::BlockingReaderWriterQueue<PacketHolder<PacketOut>> out_queue;
	public:
		tcp::socket socket;
	};

	// Manager      - class that owns (and manages) the socket
	// PacketHolder - the class that manages out-going packets lifetime
	template <typename Manager, template<typename T> class PacketHolder, typename HeaderOut, typename HeaderIn>
	class SocketUDP {
	public:

		using PacketOut = packet_udp<HeaderOut>;
		using PacketIn = packet_udp<HeaderIn>;

		// unbound socket, needs to be bounded
		SocketUDP(Manager& manager, asio::io_context& ctx) noexcept :
			manager(manager),
			global_ctx(ctx),
			socket(ctx)
		{}

		~SocketUDP() noexcept {
			out_queue.emplace(PacketHolder<PacketOut>{ nullptr }); // 'Wake up' the wait_dequeue()
		}

		constexpr asio::error_code OpenBindConnect(udp::endpoint&& local_endpoint, udp::endpoint&& remote_endpoint) noexcept {
			asio::error_code ec;

			socket.open(udp::v4(), ec);

			if (ec) {
				return ec;
			}

			socket.bind(local_endpoint, ec);

			if (ec) {
				return ec;
			}

			socket.connect(remote_endpoint, ec);

			return ec;
		}

		constexpr void Start() noexcept {
			Read();

			std::thread{ &SocketUDP::Write, this }.detach();
		}

		constexpr void Send(PacketHolder<PacketOut> p) noexcept {
			out_queue.emplace(std::move(p));
		}

	private:

		void Read() noexcept {

			socket.async_wait(udp::socket::wait_read,
				[this](asio::error_code ec) {
					if (ec) {
						manager.Close({ net_error::failed_to_read, ec });
						return;
					}

					size_t size = socket.available();

					auto p = gef::unique_ref<PacketIn>::make( std::move(manager.builder_UDP(size)) );

					auto bufs = p->mut_buf_seq();

					socket.receive(bufs, 0, ec);

					if (ec) {
						manager.Close({ net_error::failed_to_read, ec });
						return;
					}

					if (not ContinueAndNotify(std::move(p))) {
						manager.Close({ net_error::unknown_msg_type, gef::nullopt });
					}
				});
		}

		void Write() noexcept {
			PacketHolder<PacketOut> p{ nullptr };

			out_queue.wait_dequeue(p);

			if (not manager.connected) {
				return;
			}

			auto bufs = p->const_buf_seq();

			socket.async_send(bufs,
				[this, p = std::move(p)](asio::error_code ec, size_t) {
					if (ec) {
						manager.Close({ net_error::failed_to_write, ec });
						return;
					}
				});

			Write();
		}

		constexpr bool ContinueAndNotify(gef::unique_ref<PacketIn>&& p) noexcept {
			Read();
			return manager.NewPacketUDP(std::forward<decltype(p)>(p));
		}

	private:
		Manager& manager;
		asio::io_context& global_ctx;

		moodycamel::BlockingReaderWriterQueue<PacketHolder<PacketOut>> out_queue;
	public:
		udp::socket socket;
	};
}