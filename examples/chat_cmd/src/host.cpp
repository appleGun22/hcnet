#include "Common.hpp"
#include "hcnet/upnp.hpp"

class Hoster;

using HOST = net::Host<Hoster>;

class Hoster : public HOST {
public:
	Hoster(const u16 port, const i16 max_connections) :
		HOST(port, max_connections, this),
		clients(max_connections + 1)
	{}

	gef::sparse_array<client_info> clients;
	std::shared_mutex mutex_mutate_clients;

public:

	void on_error(net::error_info const& err) noexcept {
		display_error(err);
	}

	void on_close_connection(const i16 from_id, gef::option<net::error_info const&> err) noexcept {

		err.inspect(&display_error);
		println("({}) disconnected.", clients[from_id].name);

		auto p = gef::unique_ref<PacketTCP>::make( msg_t::client_disconnect );
		Send(std::move(p), from_id);

		{
			std::scoped_lock lock{ mutex_mutate_clients };
			clients.erase_at(from_id);
		}
	}

	auto new_client(net::any_msg& new_cinfo) noexcept -> std::expected<WIRE::allowed, WIRE::not_allowed> {
		std::scoped_lock lock{ mutex_mutate_clients };
		
		return clients.next_empty_index().map_or_else(
			[&](size_t& index) -> std::expected<WIRE::allowed, WIRE::not_allowed> {

				auto& cinfo = new_cinfo.as<client_info>().inner;

				gef::option<client_info&> client_with_same_name = clients.first_if(
					[&](auto const& ci) {
						return ci.name == cinfo.name;
					});

				if (client_with_same_name.has_value()) {
					println("A client tried to join the session with an existing name. ({})", cinfo.name);

					return std::unexpected<WIRE::not_allowed>(
							gef::unique_ref<PacketTCP>::make(
								msg_t::connection_request::duplicate_name
							));
				}

				auto hinfo_msg = gef::unique_ref<net::msg<host_info>>::make();

				// serialize first
				hinfo_msg->inner.serialize(clients);

				// then add client
				cinfo = clients.emplace_at(index, std::move(cinfo));
				println("({}) joined the session.", cinfo.name);

				return WIRE::allowed{
					gef::unique_ref<PacketTCP>::make(
						std::move(hinfo_msg)
					),
					gef::unique_ref<PacketTCP>::make(
						gef::unique_ref<net::msg_ref<client_info>>::make( cinfo )
					),
					static_cast<i16>(index)
				};
			},
			[]() -> std::expected<WIRE::allowed, WIRE::not_allowed> {
				return std::unexpected<WIRE::not_allowed>(
						gef::unique_ref<PacketTCP>::make(
							msg_t::connection_request::server_full
						));
			});
	}

	static gef::option<gef::unique_ref<net::any_msg>> builder_TCP(net::header_client_TCP const& h) noexcept {
		switch (static_cast<msg_t::event>(h.msg_type)) {
		case msg_t::new_client:
			return gef::unique_ref<net::msg<client_info>>::make( h.size );
		case msg_t::chat_msg:
			return gef::unique_ref<net::msg<chat_msg>>::make( h.size );
		default:
			return gef::nullopt;
		}
	}

	void new_packet_TCP(gef::unique_ref<PacketTCPclient> p, const i16 from_id) noexcept {

		p->m.map_or_else(
			[&](gef::unique_ref<net::any_msg>& m) {
				switch (p->h.msg_type) {
				case msg_t::chat_msg: {
					chat_msg const& msg = m->as<chat_msg>().inner;

					println("[{}](TCP): {}", clients[from_id].name, msg.text);

					Send(gef::unique_ref<PacketTCP>::make(std::move(m)), from_id);
					return;
				}
				}
			},
			[&]() {
				return;
			});
	}

	static gef::unique_ref<net::any_msg> builder_UDP(size_t size) noexcept {
		return gef::unique_ref<net::msg<chat_msg>>::make( size );
	}

	bool new_packet_UDP(gef::unique_ref<PacketUDPclient> p, const i16 from_id) noexcept {

		switch (p->h.msg_type) {
		case msg_t::chat_msg: {
			chat_msg const& msg = p->m->as<chat_msg>().inner;

			println("[{}](UDP): {}", clients[from_id].name, msg.text);

			Send(gef::unique_ref<PacketUDP>::make(std::move(p->m)), from_id);
			return true;
		}
		}

		return false;
	}
};

static std::error_code setup_upnp() noexcept {
	net::Upnp upnp(PORT, PORT, APP_NAME);

	std::string answer;

	println("localhost Coin: {}", forge_coin("127.0.0.1"));

	print("Configure WIFI? (y/n): ");
	std::getline(std::cin, answer);

	bool configure_wifi{ answer[0] == 'y' };

	if (not (answer[0] == 'y')) {
		print("Get coins (IP addresses)? (y/n): ");
		std::getline(std::cin, answer);

		if (answer[0] != 'y') {
			return {};
		}
	}

	std::error_code ec;

	ec = upnp.Discover();

	if (ec) {
		return ec;
	}

	ec = upnp.Get_valid_IGD();

	if (ec) {
		return ec;
	}

	if (configure_wifi) {
		ec = upnp.Add_port_mapping();

		if (ec) {
			return ec;
		}
	}

	ec = upnp.Pull_wan_address();

	if (ec) {
		return ec;
	}

	println("\n==========");
	println("[upnp] LAN(local) ip: {} | Coin: {}", upnp.LAN_address, forge_coin(upnp.LAN_address));
	println("[upnp] WAN(public) ip: {} | Coin: {}", upnp.WAN_address, forge_coin(upnp.WAN_address));
	println("==========");

	return {};
}

static bool setup_host(Hoster& host) noexcept {
	std::error_code ec = setup_upnp();

	if (ec) {
		println("[upnp] Error: {}", ec.message());
		return false;
	}

	std::string name;

	do {
		print("\nName: ");
		std::getline(std::cin, name);
	} while (name.size() > MAX_NAME_SIZE);

	host.clients.emplace_at(host.host_id(), std::move(name));

	return true;
}

int main() {

	Hoster host(PORT, MAX_CONNECTIONS);

	if (not setup_host(host)) {
		RETURN_ON_KEY_PRESS;
	}

	host.Start();

	println("Waiting for connections...\n");

	bool mode_tcp = true;

	for (;;) {
		auto m = gef::unique_ref<net::msg<chat_msg>>::make();
		chat_msg& msg = m->inner;

		std::getline(std::cin, msg.text);

		if (not host.is_running()) {
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
				auto p = gef::unique_ref<HOST::PacketTCP>::make(std::move(m));

				host.Send(std::move(p), host.host_id());
			}
			else {
				auto p = gef::unique_ref<HOST::PacketUDP>::make(std::move(m));

				host.Send(std::move(p), host.host_id());
			}
		}
	}

	RETURN_ON_KEY_PRESS;
}