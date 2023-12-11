#pragma once

#include "hcnet/host.hpp"
#include "hcnet/client.hpp"
#include "hcnet/dual_vector.hpp"

#include "fmt/core.h"
#include "fmt/format.h"

#define RETURN_ON_KEY_PRESS \
	std::ignore = std::getchar(); \
	return 1;

using fmt::println;
using fmt::print;

constexpr auto APP_NAME = "Tester app";
constexpr u16 PORT = 9180;
constexpr i32 MAX_CONNECTIONS = 10;

constexpr u32 djb2_hash(const str& s) {
	u32 hash = 5381;

	for (size_t i = 0; i < s.length(); i++)
		hash = ((hash << 5) + hash) + (u32)s[i];

	return hash;
}

constexpr u32 UNIQUE_APP_ID = djb2_hash(APP_NAME);

str forge_coin(const str& ipstr) {
	str coin = "";
	std::istringstream ssip(ipstr);

	if (ipstr.find('.') != str::npos) { // IPv4
		for (str segment; std::getline(ssip, segment, '.'); ) {
			coin += fmt::format("{:02x}", std::stoi(segment) ^ 0x7F);
		}
	}
	else if (ipstr.find(':') != str::npos) { // IPv6
		for (str segment; std::getline(ssip, segment, ':'); ) {
			coin += fmt::format("{:04x}", std::stoi(segment, nullptr, 16) ^ 0x7FFF);
		}
	}

	return coin;
}

str melt_coin(const str& s) {
	if (s.find_first_not_of("0123456789abcdefABCDEF", 2) != str::npos) {
		return str{};
	}

	str melt = "";

	if (s.size() == 8) { // IPv4
		for (int i = 0; i < 8; i += 2) {
			melt += fmt::format("{:d}.", std::stoi(s.substr(i, 2), nullptr, 16) ^ 0x7F);
		}
	}
	else if (s.size() == 32) { // IPv6
		for (int i = 0; i < 4 * 8; i += 4) {
			melt += fmt::format("{:x}:", std::stoi(s.substr(i, 4), nullptr, 16) ^ 0x7FFF);
		}
	}
	else {
		return melt;
	}

	melt.pop_back();

	return melt;
}

class byte_buf {
public:
	~byte_buf() {
		if (data != nullptr) {
			delete[] data;
		}
	}

	inline void reserve(size_t init_bytes) {
		if (data != nullptr) {
			delete[] data;
		}

		data = new i8[init_bytes];
	}

	inline void copy_back(const void* from, const size_t size) {
		std::memcpy(data + rw_size, from, size);
		rw_size += size;
	}

	template <typename T, typename ...Args>
	inline void construct_back(Args ...args) {
		new (data + rw_size) T{ std::forward<Args>(args)... };
		rw_size += sizeof(T);
	}

	inline void load_front(void* to, const size_t size) {
		std::memcpy(to, data + rw_size, size);
		rw_size += size;
	}

	template <typename T>
	inline T make_front() {
		T t;

		std::memcpy(&t, data + rw_size, sizeof(T));

		rw_size += sizeof(T);

		return t;
	}

	inline const size_t size() const {
		return rw_size;
	}

	size_t rw_size = 0;
	i8* data = nullptr;
};

//
// NET RELEATED STARTS HERE
// | | |
// V V V

static constexpr u64 MAX_NAME_SIZE = 64;

struct client_info {
	client_info() {}
	client_info(const str& s) : name(s) {}
	client_info(const u64 size) { name.resize(size); }

	u64 size() const {
		return name.size();
	}

	str name;
};

template <>
struct net::vectorize_msg<client_info> {
	template <typename Buf>
	std::vector<Buf> operator()(const client_info& obj) const {
		return net::build_custom_buf_seq<Buf>(
			Buf((void*)obj.name.data(), obj.name.size())
		);
	}
};


// The structure of the data is defined seperatly in the serialization and deserialization sections, 
// which may, and did, cause problems while changing types
struct host_info {
	host_info() {}

	struct client_data {
		u64 size;
		i64 id;
	};

	template <class UserHost>
	auto custom_const_buf(UserHost& host) {
		const i64 max_clients_allowed = host.clients.capacity();
		const i64 clients_count       = host.clients.size();

		data.reserve( // reserve the maximum space that may be used
			sizeof(max_clients_allowed) +
			sizeof(clients_count) +
			clients_count * (sizeof(client_data) + MAX_NAME_SIZE)
		);

		data.copy_back(&max_clients_allowed, sizeof(max_clients_allowed));

		data.copy_back(&clients_count, sizeof(clients_count));

		host.clients.for_each(
			[this, &clients = host.clients](const auto& ci) {
				const auto& name = ci.name;

				data.construct_back<client_data>(name.size(), std::distance(clients.data_begin(), &ci));

				data.copy_back(name.data(), name.size());
			});

		h.size = data.size();

		return std::array{
			h.to_const_buf(),
			net::const_buf{ data.data, data.size() }
		};
	}

	auto custom_mut_buf(const u64 size) {
		data.reserve(size);
		return net::mut_buf{ data.data, size };
	}

	template <class UserClient>
	void load_data(UserClient& client) {

		auto max_connections = data.make_front<i64>();
		client.clients.resize(max_connections);

		auto clients_count = data.make_front<i64>();
		
		for (int i = 0; i < clients_count; i++) {
			auto cdata = data.make_front<client_data>();

			auto& ci = client.clients.insert(cdata.id, client_info{ cdata.size });

			data.load_front(ci.name.data(), cdata.size);
		}
	}

	byte_buf data;
	net::header h{0, 0};
};

struct chat_msg {

	enum class type : i32 {
		ascii,
		utf8,
		link,
		command
	};

	chat_msg() {}
	chat_msg(u64 text_size) { text.resize(text_size); }

	u64 text_size() const {
		return text.size();
	}

	str text;
	type t{ type::ascii };
};

template <>
struct net::vectorize_msg<chat_msg> {
	template <typename Buf>
	std::vector<Buf> operator()(const chat_msg& obj) const {
		return net::build_custom_buf_seq<Buf>(
			Buf((void*)&obj.t, sizeof(obj.t)),
			Buf((void*)obj.text.data(), obj.text.size())
		);
	}
};

struct msg_t { // encapsulate enum but keep implicit conversion to int
	enum Type : i32 {
		NET_TO_CLIENT_MESSAGES,
		chat_msg
	};
};

#include "hcnet/msg.hpp"