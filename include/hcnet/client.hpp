#pragma once

#include "canyon.hpp"

namespace net {

	template <class UserClient>
	class Client {
	public:

		Client() : socket(m_context) {}

		~Client() {
			Stop();
		}

	private:
		asio::io_context m_context;
		tcp::socket socket;
		std::thread m_self_thread;

		tsQueueMini<std::unique_ptr<PACKET>> m_outQ;

	public:

		void Start(const str& host_ip, const u16 port, std::unique_ptr<PACKET> clientInfo) {
			tcp::resolver resolver(m_context);
			tcp::resolver::results_type endpoints = resolver.resolve(host_ip, std::to_string(port));

			asio::async_connect(socket, endpoints,
				[this, clientInfo = std::move(clientInfo)](asio::error_code ec, tcp::endpoint endpoint) mutable {
					if (ec) {
						Cut_connection(ec);
						return;
					}

					WriteClientInfo(std::move(clientInfo));
				});

			m_self_thread = std::thread([this]() { m_context.run(); });
		}

		// Stops the client.
		// Call Start() to restart the client.
		inline void Stop() noexcept {
			if (!m_context.stopped()) { m_context.stop(); }

			if (m_self_thread.joinable()) { m_self_thread.join(); }
		}

		inline void Send(std::unique_ptr<header> h, std::unique_ptr<IS_NET_MSG> m) {
			if (socket.is_open()) {
				bool was_empty = m_outQ.empty();
				m_outQ.emplace(std::make_unique<PACKET>(std::move(h), std::move(m)));

				if (was_empty) {
					Write();
				}
			}
		}

	private:

		void Cut_connection(const asio::error_code& ec) noexcept {
			if (socket.is_open()) {// maybe use shutdown() and check for asio::error::eof(?) in async read/write
				socket.close();
				static_cast<UserClient&>(*this).on_dis(ec);
			}
		}

		void WriteClientInfo(std::unique_ptr<PACKET> clientInfo) {
			auto ci = clientInfo.get();
			asio::async_write(socket, ci->const_buf_seq(),
				[this, clientInfo = std::move(clientInfo)](asio::error_code ec, size_t) {
					if (ec) {
						Cut_connection(ec);
						return;
					}

					ReadHostInfo();
				});
		}

		void ReadHostInfo() {
			auto h = std::make_unique<header>();
			auto Ph = h.get();

			asio::async_read(socket, Ph->to_mut_buf(),
				[this, h = std::move(h)](asio::error_code ec, size_t) {
					if (ec) {
						Cut_connection(ec);
						return;
					}
					
					auto hinfo = std::make_unique<typename UserClient::HostInfo>();
					auto Phinfo = hinfo.get();

					asio::async_read(socket, Phinfo->custom_mut_buf(h->size),
						[this, hinfo = std::move(hinfo)](asio::error_code ec, size_t) mutable {
							if (ec) {
								Cut_connection(ec);
								return;
							}

							static_cast<UserClient&>(*this).handle_host_info(std::move(hinfo));
							ReadHeader();
						});
				});
		}

		void ReadHeader() {
			auto p = std::make_unique<PACKET>();
			auto Pp = p.get();

			asio::async_read(socket, Pp->h->to_mut_buf(),
				[this, p = std::move(p)](asio::error_code ec, size_t) mutable {
					if (ec) {
						Cut_connection(ec);
						return;
					}
					
					if (p->h->size == 0) {
						static_cast<UserClient&>(*this).new_msg(std::move(p));

						ReadHeader();
					}
					else {
						p->m = std::move(static_cast<UserClient&>(*this).builder(*p->h));

						if (p->m == nullptr) {
							Cut_connection(_lib::make_ec_net(net_error::unknown_msg_type));
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
						Cut_connection(ec);
						return;
					}
					
					ReadHeader();
					static_cast<UserClient&>(*this).new_msg(std::move(p));
				});
		}

		void Write() {
			const std::unique_ptr<PACKET>& _msg = m_outQ.front();

			auto f =
			[this](asio::error_code ec, size_t) {
				if (ec) {
					Cut_connection(ec);
					return;
				}

				if (!m_outQ.pop_check_empty()) {
					Write();
				}
			};

			if (_msg->m == nullptr) { // empty message, only header will be sent
				asio::async_write(socket, _msg->h->to_const_buf(), f);
			}
			else {
				asio::async_write(socket, _msg->const_buf_seq(), f);
			}
		}
	};

}