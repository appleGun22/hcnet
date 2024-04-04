#include "Common.hpp"

class Clienter;

class Clienter : public net::Client<Clienter> {
public:
	Clienter() noexcept {}

	gef::sparse_array<client_info> clients;
	std::mutex mutex_mutate_clients;

	client_info my_cinfo;

public:

	void on_error(net::error_info const& err) noexcept {
		display_error(err);
	}

	void on_close_connection(gef::option<net::error_info const&> err) noexcept {

		err.inspect(&display_error);

		println("Error / Host disconnected.");

		clients.clear();
	}

	static gef::option<gef::unique_ref<net::any_msg>> connection_result_builder(net::header_server_TCP const& h) noexcept {
		switch (static_cast<msg_t::connection_request::Type>(h.msg_type)) {
		case msg_t::connection_request::new_host:
			return gef::unique_ref<net::msg<host_info>>::make( h.size );
		default:
			return gef::nullopt;
		}
	}

	bool connection_result(gef::unique_ref<PacketTCPserver> p) noexcept {

		return p->m.map_or_else(
			[&](gef::unique_ref<net::any_msg>& m) {
				if (p->h.msg_type == msg_t::connection_request::new_host) {
					auto& hinfo = m->as<host_info>().inner;

					hinfo.deserialize(clients);

					std::string names_list{};

					clients.for_each(
						[&](client_info& ci, size_t&) {
							names_list += fmt::format(" - ({})", ci.name);
						});

					println("\nSession is ready! users: \n{}\n~~~~~~~~~", names_list);

					return true;
				}
				else {
					println("Host sent an unknown packet as a connection result");

					return false;
				}
			},
			[&]() {
				switch (p->h.msg_type) {
				case msg_t::connection_request::duplicate_name: {
					println("Name is already in use.");
					return false;
				}
				case msg_t::connection_request::server_full: {
					println("Server is full, connection denied.");
					return false;
				}
				}

				return false;
			});
	}

	static gef::option<gef::unique_ref<net::any_msg>> builder_TCP(net::header_server_TCP const& h) noexcept {
		switch (static_cast<msg_t::event>(h.msg_type)) {
		case msg_t::new_client:
			return gef::unique_ref<net::msg<client_info>>::make( h.size );
		case msg_t::chat_msg:
			return gef::unique_ref<net::msg<chat_msg>>::make( h.size );
		default:
			return gef::nullopt;
		}
	}

	void new_packet_TCP(gef::unique_ref<PacketTCPserver> p) noexcept {

		p->m.map_or_else(
			[&](gef::unique_ref<net::any_msg>& m) {
				switch (p->h.msg_type) {
				case msg_t::new_client: {
					client_info& cinfo = m->as<client_info>().inner;

					{
						std::scoped_lock lock{ mutex_mutate_clients };
						cinfo = clients.emplace_at(p->h.from_id, std::move(cinfo));
					}

					println("({}) joined the session.", cinfo.name);
					return;
				}
				case msg_t::chat_msg: {
					chat_msg const& msg = m->as<chat_msg>().inner;

					println("[{}](TCP): {}", clients[p->h.from_id].name, msg.text);
					return;
				}
				}
			},
			[&]() { // header only
				switch (p->h.msg_type) {
				case msg_t::client_disconnect: {
					println("({}) left the session.", clients[p->h.from_id].name);

					{
						std::scoped_lock lock{ mutex_mutate_clients };
						clients.erase_at(p->h.from_id);
					}

					return;
				}
				}
			});
	}

	static gef::unique_ref<net::any_msg> builder_UDP(size_t size) noexcept {
		return gef::unique_ref<net::msg<chat_msg>>::make(size);
	}

	bool new_packet_UDP(gef::unique_ref<PacketUDPserver> p) noexcept {

		switch (p->h.msg_type) {
		case msg_t::chat_msg: {
			chat_msg const& msg = p->m->as<chat_msg>().inner;

			println("[{}](UDP): {}", clients[p->h.from_id].name, msg.text);

			return true;
		}
		}

		return false;
	}
	
};


static void setup_client(Clienter& client) noexcept {
	std::string answer;

	do {
		print("Name: ");
		std::getline(std::cin, answer);
	} while (answer.size() > MAX_NAME_SIZE);

	client.my_cinfo.name = answer;

	gef::option<std::string> ip;

	do {
		print("Coin: ");
		std::getline(std::cin, answer);

		if (answer == "loc") {
			answer = "007f7f7e";
		}

		ip.replace(melt_coin(answer));
	} while (ip.is_null());

	auto m = gef::unique_ref<net::msg_ref<client_info>>::make( client.my_cinfo );

	auto p = gef::unique_ref<Clienter::PacketTCP>::make( std::move(m) );

	client.Start(ip.value_unchecked(), PORT, std::move(p));
}

int main() {

	Clienter client{};

	setup_client(client);

	println("Waiting for connection...\n");

	bool mode_tcp = true;

	for (;;) {
		auto m = gef::unique_ref<net::msg<chat_msg>>::make();
		chat_msg& msg = m->inner;

		std::getline(std::cin, msg.text);

		if (not client.is_connected()) {
			break;
		}

		if (std::all_of(msg.text.begin(), msg.text.end(), isspace)) { // if only whitespaces
			continue;
		}

		if (msg.text == "/mode tcp") {
			mode_tcp = true;
		}
		else if (msg.text == "/mode udp") {
			mode_tcp = false;
		}
		else {
			if (mode_tcp) {
				auto p = gef::unique_ref<Clienter::PacketTCP>::make(std::move(m));

				client.Send(std::move(p));
			}
			else {
				auto p = gef::unique_ref<Clienter::PacketUDP>::make(std::move(m));

				client.Send(std::move(p));
			}
		}
	}

	RETURN_ON_KEY_PRESS;
}