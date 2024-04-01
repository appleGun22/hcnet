#include "TempForTest.hpp"
#include "hcnet/upnp.hpp"

class Hoster;

using HOST = net::Host<Hoster>;

class Hoster : public HOST {
public:
	using HostInfo = host_info;

	Hoster(const u16 port, const i32 max_connections)
		: HOST(port, max_connections, this), clients(max_connections + 1), host_id(max_connections)
	{}

	dual_vector<client_info> clients;

	const i32 host_id;

public:

	void on_error(const net::error_info& err) const {
		using enum net::net_error;

		const std::string desc = 
		[&]() -> std::string {
			switch (err.what) {
				case unknown_msg_type: {
					return "";
				}
				case failed_connection_request: {
					return "A request to establish connection has failed.";
				}
				case failed_to_run_io_context: {
					return "";
				}
			}
		}();

		println("(system) ERROR: {}\n(EC {}: {})", desc, err.ec.value(), err.ec.message());
	}

	void on_client_dis(const i32 id, const tl::optional<net::error_info>& err) {
		if (err.has_value()) {
			on_error(err.value());
		}

		println("(system) [{}] disconnected.", clients[id].name);

		auto h = std::make_unique<net::header>(0, msg_t::net_client_disconnect, id);

		Send(std::make_unique<net::PACKET>(std::move(h), nullptr));

		clients.erase(id);
	}

	bool new_client(const net::IS_NET_MSG* new_cinfo) {
		const auto& cinfo = *static_cast<const net::msg<client_info>*>(new_cinfo);

		bool name_duplicate =
			clients.for_each_if(
			[&](const auto& ci) {
				return ci.name == cinfo->name;
			});

		if (!name_duplicate) {
			return true;
		}

		println("(system) A client has tried to join the session with an existing name. ({})", cinfo->name);
		return false;
	}

	std::unique_ptr<net::IS_NET_MSG> builder(const net::header& h) const {
		switch (h.msg_type) {
			case msg_t::net_new_client:
				return std::make_unique<net::msg<client_info>>(h.size);
			case msg_t::chat_msg:
				return std::make_unique<net::msg<chat_msg>>(h.size);
			default:
				return nullptr;
		}
	}

	// currently new_msg in both host and client, may be called simultaneously
	void new_msg(std::unique_ptr<net::PACKET> p, HOST::WIRE& w) {
		auto& h = *p->h;

		switch (h.msg_type) {
			case msg_t::net_new_client: {
				client_info& m = **static_cast<net::msg<client_info>*>(p->m.get());

				const auto& cinfo = clients.insert(w.id(), std::move(m));
				println("({}) joined the session.", cinfo.name);

				p->m = std::make_unique<net::msg_ref<client_info>>(cinfo);
				h.from_id = w.id();

				Send(std::move(p), &w);
				break;
			}
			case msg_t::chat_msg: {
				const chat_msg& m = **static_cast<net::msg<chat_msg>*>(p->m.get());

				println("[{}]: {}", clients[w.id()].name, m.text);

				h.from_id = w.id();

				Send(std::move(p), &w);
				break;
			}
		}
	}
};

static std::error_code setup_upnp() {
	net::Upnp upnp("9180", "9180", APP_NAME);

	bool configure_wifi{ 0 };
	str answer;

	println("!!! CONFIGURE UPNP TO USE UDP TOO !!!");
	print("[system] Configure WIFI? (y/n): ");
	std::getline(std::cin, answer);

	configure_wifi = answer[0] == 'y';

	if (!configure_wifi) {
		print("[system] Get coins (IP addresses)? (y/n): ");
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

	print("\n==========\n");
	println("[upnp] LAN(local) ip:   {}", upnp.LAN_address);
	println("[upnp] LAN(local) Coin: {}", forge_coin(upnp.LAN_address));
	println("[upnp] WAN(public) ip:   {}", upnp.WAN_address);
	println("[upnp] WAN(public) Coin: {}", forge_coin(upnp.WAN_address));
	print("\n==========\n");

	return {};
}

int main() {

	{
		std::error_code ec = setup_upnp();

		if (ec) {
			println("[upnp] Error: {}", ec.message());
			RETURN_ON_KEY_PRESS;
		}
	}

	auto host = Hoster(PORT, MAX_CONNECTIONS);

	{
		str answer;

		do {
			print("\n[system] Name: ");
			std::getline(std::cin, answer);
		} while (answer.size() > MAX_NAME_SIZE);

		host.clients.insert(host.host_id, std::move(answer));
	}

	host.Start();

	println("[system] Host is ready.\n");

	while (1) {
		auto m = std::make_unique<net::msg<chat_msg>>();

		std::getline(std::cin, (*m)->text);

		if (std::all_of((*m)->text.begin(), (*m)->text.end(), isspace)) { // if only whitespaces
			continue;
		}

		auto h = std::make_unique<net::header>((*m)->text_size(), msg_t::chat_msg, host.host_id);
		host.Send(std::make_unique<net::PACKET>(std::move(h), std::move(m)));
	}
}