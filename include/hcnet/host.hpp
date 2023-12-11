#pragma once

#include "canyon.hpp"

namespace net {

template <class UserHost>
class Host;

template <class UserHost>
class Wire {
public:
	using self_t = Wire<UserHost>;

	Wire(tcp::socket&& s) : socket(std::move(s)) {}

	void ReadClientInfo() {
		std::unique_ptr<self_t> self_life(this);

		auto p = std::make_unique<PACKET>();

		auto Pp = p.get();
		asio::async_read(socket, Pp->h->to_mut_buf(),
			[this, p = std::move(p), self_life = std::move(self_life)](asio::error_code ec, size_t s) mutable {
				if (ec) {
					Close(nullptr);
					return;
				}
				
				p->m = std::move(host_class->builder(*p->h));

				if (p->m == nullptr) {
					Close(nullptr);
					return;
				}

				auto Pp = p.get();
				asio::async_read(socket, Pp->mut_buf_seq(),
					[this, p = std::move(p), self_life = std::move(self_life)](asio::error_code ec, size_t) mutable {
						if (!ec && host_class->filter_client_info(p->m.get())) {
							WriteHostInfo(std::move(self_life), std::move(p));
						}
						else {
							Close(nullptr);
						}
					});
			});
	}

	void WriteHostInfo(std::unique_ptr<self_t> self_life, std::unique_ptr<PACKET> cinfo_packet) {

		auto hinfo = std::make_unique<typename UserHost::HostInfo>();

		auto Phinfo = hinfo.get();
		asio::async_write(socket, Phinfo->custom_const_buf(*host_class),
			[this, hinfo = std::move(hinfo), cinfo_packet = std::move(cinfo_packet), self_life = std::move(self_life)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close(nullptr);
					return;
				}

				m_id = host_class->wires.insert_unchecked(self_life.release());

				host_class->new_msg(std::move(cinfo_packet), *this);

				ReadHeader();
			});
	}

	void ReadHeader() {
		auto p = std::make_unique<PACKET>();

		auto Pp = p.get();
		asio::async_read(socket, Pp->h->to_mut_buf(),
			[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close(&ec);
					return;
				}
				
				if (p->h->size == 0) {
					host_class->new_msg(std::move(p), *this);

					ReadHeader();
				}
				else {
					p->m = std::move(host_class->builder(*p->h));

					if (p->m == nullptr) {
						const auto ec = _lib::make_ec_net(net_error::unknown_msg_type);
						Close(&ec);
						return;
					}

					ReadBody(std::move(p));
				}
			});
	}

	void ReadBody(std::unique_ptr<PACKET> p) {
		auto Pp = p.get();
		asio::async_read(socket, Pp->m->custom_mut_buf(),
			[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
				if (ec) {
					Close(&ec);
					return;
				}
				
				ReadHeader();
				host_class->new_msg(std::move(p), *this);
			});
	}

	void Send(std::shared_ptr<PACKET> _msg) {
		asio::post(socket.get_executor(),
			[this, _msg = std::move(_msg)]() {
				bool was_empty = write_queue.empty();
				write_queue.emplace(std::move(_msg));

				if (was_empty) {
					Write();
				}
			});
	}

	void Write() {
		const std::shared_ptr<PACKET>& _msg = write_queue.front();

		auto f =
		[this](asio::error_code ec, size_t) {
			if (ec) {
				Close(&ec);
			}
			else {
				if (!write_queue.pop_check_empty()) {
					Write();
				}
			}
		};

		if (_msg->m == nullptr) { // empty message, only header will be sent
			asio::async_write(socket, _msg->h->to_const_buf(), f);
		}
		else {
			asio::async_write(socket, _msg->const_buf_seq(), f);
		}
	}

	void Close(const asio::error_code* ec) noexcept {
		if (socket.is_open()) {// maybe use shutdown() and check for asio::error::eof(?) in async read/write
			socket.close();

			if (ec != nullptr) {
				host_class->on_client_dis(id(), *ec);
			}
		}
	}

	inline bool socket_is_open() const {
		return socket.is_open();
	}

public:
	inline const i32 id() const {
		return m_id;
	}

private:
	tcp::socket socket;
	i32 m_id{ -1 };

	tsQueueMini<std::shared_ptr<PACKET>> write_queue;

public:
	static UserHost* host_class;
};

template <class UserHost>
UserHost* Wire<UserHost>::host_class;

template <class UserHost>
class Host {
public:

	using WIRE = Wire<UserHost>;
	using MSGEXT = msg_ext<WIRE>;

	Host(const u16 port, const i32 max_clients, UserHost& derived_from) :
		m_acceptor(m_context, tcp::endpoint(tcp::v4(), port)),
		wires(max_clients)
	{
		WIRE::host_class = &derived_from;
	}

	~Host() {
		Stop();
	}

private:
	asio::io_context m_context;
	tcp::acceptor m_acceptor;
	std::thread m_self_thread;

	tsQueueMini<MSGEXT> m_outQ;

public:
	net_exclusive_dual_vector<WIRE> wires;

public:
	/// Starts / Restarts the host.
	inline void Start() noexcept {
		AcceptConnections();

		m_self_thread = std::thread([this]() { m_context.run(); });
	}

	/// Stops host.
	/// Call Start() when you wish to restart the host.
	inline void Stop() noexcept {
		if (!m_context.stopped()) { m_context.stop(); }

		if (m_self_thread.joinable()) { m_self_thread.join(); }
	}

	inline void Send(std::unique_ptr<header> h, std::unique_ptr<IS_NET_MSG> m, WIRE* skip_client = nullptr) noexcept {
		bool was_empty = m_outQ.empty();
		m_outQ.emplace(std::make_unique<PACKET>(std::move(h), std::move(m)), skip_client);

		if (was_empty) {
			WriteToClients();
		}
	}

	inline void Send(std::unique_ptr<PACKET> p, WIRE* skip_client = nullptr) noexcept {
		bool was_empty = m_outQ.empty();
		m_outQ.emplace(std::move(p), skip_client);

		if (was_empty) {
			WriteToClients();
		}
	}

private:

	void WriteToClients() noexcept {

		do {

			auto& packet = m_outQ.front();

			bool dead_clients_exist = false;

			std::shared_ptr<PACKET> p_sh(std::move(packet.p));

			// call end only one time, to ignore new clients that may connect while distributing the current message
			const auto end = wires.end();

			for (auto iter = wires.begin(); iter != end; ++iter) {
				const auto& w = **iter;

				if (w == packet.from_wire) {
					continue;
				}

				if (w->socket_is_open()) {
					w->Send(p_sh);
				}
				else {
					dead_clients_exist = true;
					wires.kill(*iter);
				}
			}

			if (dead_clients_exist) {
				wires.initiate_delete();
			}

		} while (!m_outQ.pop_check_empty());
	}

	/// Start accepting new connections.
	void AcceptConnections() {

		m_acceptor.async_accept([this](asio::error_code ec, tcp::socket temp_sock) {
			if (ec) {
				static_cast<UserHost&>(*this).on_failed_connection_request(ec);
			}
			else {
				if (!wires.is_full()) {
					(new WIRE{ std::move(temp_sock) })->ReadClientInfo();
				}
				else {

					auto socket_heap = std::make_unique<tcp::socket>( std::move(temp_sock) );

					auto h = std::make_unique<header>(0, net_enum::net_server_full);

					auto Ph = h.get();
					auto Psocket_heap = socket_heap.get();
					
					asio::async_write(*Psocket_heap, Ph->to_const_buf(),
						[h = std::move(h), sh = std::move(socket_heap)](asio::error_code ec, size_t) {});
				}
			}

			AcceptConnections(); // continue to accept

			});
	}
};

}