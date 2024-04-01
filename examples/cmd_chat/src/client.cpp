#include "TempForTest.hpp"

class Clienter;

using CLIENT = net::Client<Clienter>;

class Clienter : public CLIENT {
public:
	using HostInfo = host_info;

	dual_vector<client_info> clients;

	client_info my_cinfo;

public:
	void on_any_error(const std::error_code& ec) {
		println("[system] ERROR({}[{}]): {}", ec.category().name(), ec.value(), ec.message());
	}

	void on_dis(const std::error_code& ec) {
		println("[system] Error / Host disconnected.");
		on_any_error(ec);
		clients.clear();
	}

	void handle_host_info(std::unique_ptr<host_info> hinfo) {
		hinfo->load_data(*this);

		println("~~~\n[system] Session is ready! users:");

		clients.for_each(
			[](const auto& ci) {
				println("[{}]", ci.name);
			});

		println("~~~\n");
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

	void new_msg(std::unique_ptr<net::PACKET> p) {
		const auto& h = *p->h;

		switch (h.msg_type) {
		case msg_t::chat_msg: {
			const chat_msg& m = **static_cast<net::msg<chat_msg>*>(p->m.get());

			println("[{}]: {}", clients[h.from_id].name, m.text);
			break;
		}
		case msg_t::net_new_client: {
			client_info& m = **static_cast<net::msg<client_info>*>(p->m.get());

			const auto& cinfo = clients.insert(h.from_id, std::move(m));
			println("({}) joined the session.", cinfo.name);

			break;
		}
		case msg_t::net_client_disconnect: {
			println("({}) left the session.", clients[h.from_id].name);

			clients.erase(h.from_id);
			break;
		}
		case msg_t::net_server_full: {
			println("Server is full, connection denied.");
			break;
		}

		}
	}
	
};

void setup(Clienter& client) {
	str ip;

	do {
		print("[system] Coin: ");
		std::getline(std::cin, ip);

		ip = melt_coin(ip);
	} while (ip == "");

	str name;

	do {
		print("[system] Name: ");
		std::getline(std::cin, name);
	} while (name.size() > MAX_NAME_SIZE);

	client.my_cinfo.name = name;

	auto m = std::make_unique<net::msg_ref<client_info>>(client.my_cinfo);
	auto h = std::make_unique<net::header>((*m)->size(), msg_t::net_new_client);
	auto p = std::make_unique<net::PACKET>(std::move(h), std::move(m));

	client.Start(ip, PORT, std::move(p));
}

int main() {

	Clienter client;

	setup(client);

	println("[system] Client is ready.\n");

	while (1) {
		auto m = std::make_unique<net::msg<chat_msg>>();

		std::getline(std::cin, (*m)->text);

		if (std::all_of((*m)->text.begin(), (*m)->text.end(), isspace)) { // if only whitespaces
			continue;
		}

		auto h = std::make_unique<net::header>((*m)->text_size(), msg_t::chat_msg);
		client.Send(std::make_unique<net::PACKET>(std::move(h), std::move(m)));
	}
}