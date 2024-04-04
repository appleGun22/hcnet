#pragma once

#include "hcnet/canyon.hpp"
#include "gef/byte_buffer.hpp"


struct msg_t {
	enum event : i16 {
		client_disconnect,
		new_client,
		chat_msg
	};

	struct connection_request {
		enum Type : i16 {
			new_host,
			duplicate_name,
			server_full
		};
	};
};


static constexpr u64 MAX_NAME_SIZE = 64;

struct client_info {
	client_info() noexcept {}
	client_info(std::string const& s) noexcept : name(s) {}
	client_info(const u64 struct_size) noexcept { name.resize(struct_size); }

public:
	std::string name;
};

template <>
struct net::vectorize_msg<client_info> {

	static constexpr auto identifier = msg_t::new_client;

	template <bool IncludeHeaderBuf, typename Buf>
	static std::vector<Buf> vectorize(client_info const& obj) noexcept {
		return net::build_custom_buf_seq<IncludeHeaderBuf, Buf>(
			Buf((void*)obj.name.data(), obj.name.size())
		);
	}
};


struct host_info {
	host_info() noexcept {}

	host_info(const size_t reserve_size) noexcept :
		data_buf(reserve_size)
	{}

	struct client {
		u64 size;
		u64 id;
	};

	struct data_info {
		i64 max_clients_allowed;
		i64 clients_count;
	};

	void serialize(gef::sparse_array<client_info>& clients) noexcept {

		data_buf.reserve(clients.size() * (sizeof(host_info::client) + MAX_NAME_SIZE));

		info.max_clients_allowed = clients.capacity();
		info.clients_count = clients.size();

		clients.for_each(
			[&](client_info& ci, size_t& index) {
				std::string const& name = ci.name;

				data_buf.construct_back<client>(name.size(), index);

				data_buf.copy_back(name.data(), name.size());
			});
	}

	void deserialize(gef::sparse_array<client_info>& clients) noexcept {
		clients.resize(info.max_clients_allowed);

		while (info.clients_count-- > 0) {
			auto cdata = data_buf.make_front<client>();

			auto& ci = clients.emplace_at(cdata.id, cdata.size);

			data_buf.load_front(ci.name.data(), cdata.size);
		}
	}

public:
	data_info info;
	byte_buffer data_buf;
};

template <>
struct net::vectorize_msg<host_info> {

	static constexpr auto identifier = msg_t::connection_request::new_host;

	template <bool IncludeHeaderBuf, typename Buf>
	static std::vector<Buf> vectorize(host_info const& obj) noexcept {
		return net::build_custom_buf_seq<IncludeHeaderBuf, Buf>(
			Buf((void*)&obj.info, sizeof(obj.info)),
			Buf((void*)obj.data_buf.buffer, obj.data_buf.buffer_size())
		);
	}
};


struct chat_msg {

	enum class type : i32 {
		ascii,
		utf8,
		link,
		command
	};

	chat_msg() noexcept {}

	chat_msg(const u64 struct_size) noexcept
	{
		text.resize(struct_size - sizeof(t));
	}

	std::string text;
	type t;
};

template <>
struct net::vectorize_msg<chat_msg> {

	static constexpr auto identifier = msg_t::chat_msg;

	template <bool IncludeHeaderBuf, typename Buf>
	static std::vector<Buf> vectorize(chat_msg const& obj) noexcept {
		return net::build_custom_buf_seq<IncludeHeaderBuf, Buf>(
			Buf((void*)&obj.t, sizeof(obj.t)),
			Buf((void*)obj.text.data(), obj.text.size())
		);
	}
};

#include "hcnet/msg.hpp"