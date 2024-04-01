#pragma once

#include "canyon.hpp"
#include "socket.hpp"

namespace net {

template <class Hoster>
class Host;

template <class Hoster>
class Wire {
private:

	using self_t = Host<Hoster>::WIRE;

	friend Host<Hoster>;
	friend SocketTCP<self_t, std::shared_ptr, header_server_TCP, header_client_TCP>;
	friend SocketUDP<self_t, std::shared_ptr, header_server_UDP, header_client_UDP>;


	std::jthread self_thread;

	SocketTCP<self_t, std::shared_ptr, header_server_TCP, header_client_TCP> tcp_socket;
	SocketUDP<self_t, std::shared_ptr, header_server_UDP, header_client_UDP> udp_socket;

	i16 m_id{ -1 };

	bool connected;

	static Hoster* running_host;

public:

	using PacketTCP = Host<Hoster>::PacketTCP;
	using PacketUDP = Host<Hoster>::PacketUDP;

	using PacketTCPclient = Host<Hoster>::PacketTCPclient;
	using PacketUDPclient = Host<Hoster>::PacketUDPclient;

	struct allowed {
		gef::unique_ref<PacketTCP> hinfo;
		gef::unique_ref<PacketTCP> cinfo;
		i16 id;
	};

	struct not_allowed {
		gef::unique_ref<PacketTCP> reason;
	};

	Wire(asio::io_context& ctx, tcp::socket&& s) noexcept :
		connected(false),
		tcp_socket(*this, ctx, std::move(s)),
		udp_socket(*this, ctx)
	{}

	~Wire() noexcept {}

private:

	static void Init(asio::io_context& ctx, tcp::socket&& s) noexcept {
		auto new_wire = gef::unique_ref<self_t>::make( ctx, std::move(s) );

		auto const& local_endpoint = new_wire->tcp_socket.socket.local_endpoint();
		auto const& remote_endpoint = new_wire->tcp_socket.socket.remote_endpoint();

		auto ec = new_wire->udp_socket.OpenBindConnect(
			udp::endpoint(local_endpoint.address(), local_endpoint.port()),
			udp::endpoint(remote_endpoint.address(), remote_endpoint.port())
		);

		if (ec) {
			new_wire->running_host->on_error({ net_error::failed_to_connect, ec });
			return;
		}

		auto& new_wire_ref = new_wire.get();

		new_wire_ref.CinfoReadHeader(std::move(new_wire));
	}

	void CinfoReadHeader(gef::unique_ref<self_t> lifetime) noexcept {
		auto p = gef::unique_ref<PacketTCPclient>::make();

		auto buf = header_to<mut_buf>(p->h);

		tcp_socket.socket.async_receive(buf,
			[this, p = std::move(p), lifetime = std::move(lifetime)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close({ net_error::failed_to_read, ec });
					return;
				}

				p->m.replace(builder_TCP(p->h))
					.map_or_else(
						[&](gef::unique_ref<any_msg>& m) {
							CinfoReadBody(std::move(p), m.get(), std::move(lifetime));
						},
						[&]() {
							Close({ net_error::unknown_msg_type, gef::nullopt });
						});
			});
	}

	void CinfoReadBody(gef::unique_ref<PacketTCPclient> p, any_msg& m, gef::unique_ref<self_t> lifetime) noexcept {

		auto bufs = m.mut_buf_seq();

		tcp_socket.socket.async_receive(bufs,
			[this, &m, p = std::move(p), lifetime = std::move(lifetime)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close({ net_error::failed_to_read, ec });
					return;
				}

				std::expected<allowed, not_allowed>
					wire_allowed = running_host->new_client(m);

				if (wire_allowed.has_value()) {
					HinfoWrite(std::move(*wire_allowed), std::move(lifetime));
				}
				else {
					auto bufs = wire_allowed.error().reason->const_buf_seq();

					// send error_packet, and self destruct this wire
					tcp_socket.socket.async_send(bufs,
						[this, reason = std::move(wire_allowed.error().reason), lifetime = std::move(lifetime)](asio::error_code ec, size_t)
						{});
				}
			});
	}

	void HinfoWrite(allowed wire_allowed, gef::unique_ref<self_t> lifetime) noexcept {

		auto bufs = wire_allowed.hinfo->const_buf_seq();

		tcp_socket.socket.async_send(bufs,
			[this, wire_allowed = std::move(wire_allowed), lifetime = std::move(lifetime)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close({ net_error::failed_to_write, ec });
					return;
				}

				m_id = wire_allowed.id;

				running_host->wires.lock(
					[&](auto& vec) {
						vec.emplace_back(std::move(lifetime));
					});

				running_host->Send(std::move(wire_allowed.cinfo), m_id);

				connected = true;

				tcp_socket.Start();
				udp_socket.Start();
			});
	}

	constexpr gef::option<gef::unique_ref<any_msg>> builder_TCP(header_client_TCP const& h) noexcept {
		return running_host->builder_TCP(h);
	}

	constexpr gef::unique_ref<any_msg> builder_UDP(size_t size) noexcept {
		return running_host->builder_UDP(size);
	}

	constexpr void NewPacketTCP(gef::unique_ref<PacketTCPclient>&& p) noexcept {
		running_host->new_packet_TCP(std::forward<decltype(p)>(p), m_id);
	}

	constexpr bool NewPacketUDP(gef::unique_ref<PacketUDPclient>&& p) noexcept {
		return running_host->new_packet_UDP(std::forward<decltype(p)>(p), m_id);
	}

	void Close(error_info const& err) noexcept {

		if (tcp_socket.socket.is_open()) {
			tcp_socket.socket.shutdown(tcp::socket::shutdown_both);
			tcp_socket.socket.close();

			udp_socket.socket.close(); // udp can just be closed

			connected = false;

			running_host->on_close_connection(
				m_id,
				err.ec.and_then<error_info const&>( // ?? fails to deduce `U` (option<U>)
					[&](auto const& ec) -> gef::option<error_info const&> {
						return ec == asio::error::eof
							? gef::nullopt
							: gef::option<error_info const&>{ err };
					}));
		}
	}

public:

	constexpr i16 id() const noexcept {
		return m_id;
	}
};

template <class Hoster>
Hoster* Wire<Hoster>::running_host;

template <class Hoster>
class Host {
public:

	using PacketTCP = packet_tcp<header_server_TCP>;
	using PacketUDP = packet_udp<header_server_UDP>;

	using PacketTCPclient = packet_tcp<header_client_TCP>;
	using PacketUDPclient = packet_udp<header_client_UDP>;

	using WIRE = Wire<Hoster>;

	Host(const u16 port, const i16 host_id, Hoster* derived_from) noexcept :
		m_acceptor(m_context, tcp::endpoint(tcp::v4(), port)),
		m_host_id(host_id),
		running(false)
	{
		WIRE::running_host = derived_from;
	}

	~Host() noexcept {
		Stop();
	}

private:

	friend WIRE;

	asio::io_context m_context;
	tcp::acceptor m_acceptor;
	std::thread m_self_thread;

	moodycamel::BlockingConcurrentQueue<gef::unique_ref<PacketTCP>> out_queue_tcp;
	moodycamel::BlockingConcurrentQueue<gef::unique_ref<PacketUDP>> out_queue_udp;

	gef::mutex<std::vector<gef::unique_ref<WIRE>>> wires;

	i16 m_host_id;

	bool running;

public:
	/// Starts / Restarts the host.
	void Start() noexcept {
		AcceptConnections();

		m_self_thread = std::thread(
			[this]() {

				running = true;

				std::thread{ &Host::DequeueTCP, this }.detach();
				std::thread{ &Host::DequeueUDP, this }.detach();

				asio::error_code ec;
				m_context.run(ec);

				if (ec) {
					access_hoster().on_error({ net_error::failed_to_run_io_context, ec });
				}

				running = false;

				// 'Wake up' the wait_dequeue()'s
				out_queue_tcp.enqueue(gef::unique_ref<PacketTCP>{ nullptr });
				out_queue_udp.enqueue(gef::unique_ref<PacketUDP>{ nullptr });
			});
	}

	/// Stops host.
	/// Call Start() when you wish to restart the host.
	void Stop() noexcept {
		if (not m_context.stopped()) { m_context.stop(); }

		if (m_self_thread.joinable()) { m_self_thread.join(); }
	}

	constexpr i16 host_id() const noexcept {
		return m_host_id;
	}

	constexpr bool is_running() const noexcept {
		return running;
	}

	void Send(gef::unique_ref<PacketTCP> p, const i16 skip_client) noexcept {

		p->h.from_id = skip_client;

		out_queue_tcp.enqueue(std::move(p));
	}

	void Send(gef::unique_ref<PacketUDP> p, const i16 skip_client) noexcept {

		p->h.from_id = skip_client;

		out_queue_udp.enqueue(std::move(p));
	}

private:

	void DequeueTCP() noexcept {

		gef::unique_ref<PacketTCP> p{ nullptr };

		out_queue_tcp.wait_dequeue(p);

		if (not running) {
			return;
		}

		std::shared_ptr shared_packet{ std::move(p._Ptr) };

		bool clear_dead_wires = false;

		wires.shared_lock(
			[&](auto& vec) {

				if (shared_packet->h.from_id == m_host_id) { // host isn't a wire, no need to check id
					for (gef::unique_ref<WIRE> const& wire : vec) {

						if (wire->tcp_socket.socket.is_open()) {
							wire->tcp_socket.Send(shared_packet);
						}
						else {
							clear_dead_wires = true;
						}
					}
				}
				else {
					for (gef::unique_ref<WIRE> const& wire : vec) {

						if (wire->id() == shared_packet->h.from_id) { // don't send back to the sender
							continue;
						}

						if (wire->tcp_socket.socket.is_open()) {
							wire->tcp_socket.Send(shared_packet);
						}
						else {
							clear_dead_wires = true;
						}
					}
				}
			});

		if (clear_dead_wires) {
			std::thread(
				[&]() {
					wires.lock(
						[](auto& vec) {
							std::erase_if(vec,
								[](gef::unique_ref<WIRE> const& w) {
									return w->tcp_socket.socket.is_open();
								});
						});
				}).detach();
		}

		DequeueTCP();
	}

	void DequeueUDP() noexcept {
	
		gef::unique_ref<PacketUDP> p{ nullptr };

		out_queue_udp.wait_dequeue(p);

		if (not running) {
			return;
		}

		std::shared_ptr shared_packet{ std::move(p._Ptr) };

		bool clear_dead_wires = false;

		wires.shared_lock(
			[&](auto& vec) {

				if (shared_packet->h.from_id == m_host_id) { // host isn't a wire, no need to check id
					for (gef::unique_ref<WIRE> const& wire : vec) {
						wire->udp_socket.Send(shared_packet);
					}
				}
				else {
					for (gef::unique_ref<WIRE> const& wire : vec) {

						if (wire->id() == shared_packet->h.from_id) { // don't send back to the sender
							continue;
						}

						wire->udp_socket.Send(shared_packet);
					}
				}
			});

		DequeueUDP();
	}

	/// Start accepting new connections.
	void AcceptConnections() noexcept {

		m_acceptor.async_accept(
			[this](asio::error_code ec, tcp::socket temp_sock) {
				if (ec) {
					access_hoster().on_error({ net_error::failed_to_connect, ec });
				}
				else {
					WIRE::Init(m_context, std::move(temp_sock));
				}

				AcceptConnections();
			});
	}

private:

	constexpr Hoster& access_hoster() noexcept {
		return static_cast<Hoster&>(*this);
	}
};

}