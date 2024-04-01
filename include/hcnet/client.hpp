#pragma once

#include "canyon.hpp"
#include "socket.hpp"

namespace net {

template <class Clienter>
class Client {
public:

	using PacketTCP = packet_tcp<header_client_TCP>;
	using PacketUDP = packet_udp<header_client_UDP>;

	using PacketTCPserver = packet_tcp<header_server_TCP>;
	using PacketUDPserver = packet_udp<header_server_UDP>;

	Client() noexcept :
		connected(false),
		tcp_socket(*this, m_context),
		udp_socket(*this, m_context)
	{}

	~Client() noexcept {
		Stop();
	}

private:

	friend SocketTCP<Client, gef::unique_ref, header_client_TCP, header_server_TCP>;
	friend SocketUDP<Client, gef::unique_ref, header_client_UDP, header_server_UDP>;

	asio::io_context m_context;
	std::thread m_self_thread;

	SocketTCP<Client, gef::unique_ref, header_client_TCP, header_server_TCP> tcp_socket;
	SocketUDP<Client, gef::unique_ref, header_client_UDP, header_server_UDP> udp_socket;

	bool connected;

public:

	void Start(std::string const& host_ip, const u16 port, gef::unique_ref<PacketTCP> cinfo) noexcept {

		tcp::endpoint endpoint{ asio::ip::make_address(host_ip), port };

		tcp_socket.socket.async_connect(endpoint,
			[this, cinfo = std::move(cinfo)](asio::error_code ec) mutable {
				if (ec) {
					access_clienter().on_error({ net::net_error::failed_to_connect, ec });
					return;
				}

				auto const& local_endpoint = tcp_socket.socket.local_endpoint();
				auto const& remote_endpoint = tcp_socket.socket.remote_endpoint();

				ec = udp_socket.OpenBindConnect(
					udp::endpoint(local_endpoint.address(), local_endpoint.port()),
					udp::endpoint(remote_endpoint.address(), remote_endpoint.port())
				);

				if (ec) {
					access_clienter().on_error({ net::net_error::failed_to_connect, ec });
					return;
				}

				CinfoWrite(std::move(cinfo));
			});


		// actually start
		m_self_thread = std::thread(
			[this]() {
				asio::error_code ec;
				m_context.run(ec);

				if (ec) {
					access_clienter().on_error({ net_error::failed_to_run_io_context, ec });
				}
			});
	}

	// Stops the client.
	// Call Start() to restart the client.
	void Stop() noexcept {
		if (not m_context.stopped()) { m_context.stop(); }

		if (m_self_thread.joinable()) { m_self_thread.join(); }
	}

	constexpr bool is_connected() const noexcept {
		return connected;
	}

	constexpr void Send(gef::unique_ref<PacketTCP> p) noexcept {
		tcp_socket.Send(std::move(p));
	}

	constexpr void Send(gef::unique_ref<PacketUDP> p) noexcept {
		udp_socket.Send(std::move(p));
	}

private:

	constexpr gef::option<gef::unique_ref<any_msg>> builder_TCP(header_server_TCP const& h) noexcept {
		return access_clienter().builder_TCP(h);
	}

	constexpr gef::unique_ref<any_msg> builder_UDP(size_t size) noexcept {
		return access_clienter().builder_UDP(size);
	}

	constexpr void NewPacketTCP(gef::unique_ref<PacketTCPserver>&& p) noexcept {
		access_clienter().new_packet_TCP(std::forward<decltype(p)>(p));
	}

	constexpr bool NewPacketUDP(gef::unique_ref<PacketUDPserver>&& p) noexcept {
		return access_clienter().new_packet_UDP(std::forward<decltype(p)>(p));
	}

	void Close(error_info const& err) noexcept {

		if (tcp_socket.socket.is_open()) {
			tcp_socket.socket.shutdown(tcp::socket::shutdown_both);
			tcp_socket.socket.close();

			udp_socket.socket.close(); // udp can just be closed

			connected = false;

			access_clienter().on_close_connection(
				err.ec.and_then<error_info const&>( // ?? fails to deduce `U` (option<U>)
					[&](auto const& ec) -> gef::option<error_info const&> {
						return ec == asio::error::eof
							? gef::nullopt
							: gef::option<error_info const&>{ err };
					}));
		}
	}

private:

	void CinfoWrite(gef::unique_ref<PacketTCP> cinfo) noexcept {
		auto bufs = cinfo->const_buf_seq();

		tcp_socket.socket.async_send(bufs,
			[this, cinfo = std::move(cinfo)](asio::error_code ec, size_t) {
				if (ec) {
					Close({ net_error::failed_to_write, ec });
					return;
				}

				HinfoReadHeader();
			});
	}

	void HinfoReadHeader() noexcept {
		auto p = gef::unique_ref<PacketTCPserver>::make();

		auto buf = header_to<mut_buf>(p->h);

		tcp_socket.socket.async_receive(buf,
			[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close({ net_error::failed_to_read, ec });
					return;
				}

				p->m.replace(access_clienter().connection_result_builder(p->h))
					.map_or_else(
						[&](gef::unique_ref<any_msg>& m) {
							HinfoReadBody(std::move(p), m.get());
						},
						[&]() {
							Close({ net_error::unknown_msg_type, gef::nullopt });
						});
			});
	}

	void HinfoReadBody(gef::unique_ref<PacketTCPserver> p, any_msg& m) noexcept {

		auto bufs = m.mut_buf_seq();

		tcp_socket.socket.async_receive(bufs,
			[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close({ net_error::failed_to_read, ec });
					return;
				}

				if (not access_clienter().connection_result(std::move(p))) {
					return;
				}

				connected = true;

				tcp_socket.Start();
				udp_socket.Start();
			});
	}

private:

	constexpr Clienter& access_clienter() noexcept {
		return static_cast<Clienter&>(*this);
	}
};

}