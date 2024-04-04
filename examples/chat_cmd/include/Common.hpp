#pragma once

#include <iostream>
#include <ranges>

#include "hcnet/host.hpp"
#include "hcnet/client.hpp"
#include "gef/sparse_array.hpp"

#include "fmt/core.h"
#include "fmt/format.h"

#include "Messages.hpp"

#define RETURN_ON_KEY_PRESS \
	std::ignore = std::getchar(); \
	return 1;

using fmt::println;
using fmt::print;

constexpr auto APP_NAME = "Tester app";
constexpr u16 PORT = 9180;
constexpr i16 MAX_CONNECTIONS = 10;

constexpr u32 djb2_hash(std::string const& s) {
	u32 hash = 5381;

	for (size_t i = 0; i < s.length(); i++)
		hash = ((hash << 5) + hash) + (u32)s[i];

	return hash;
}

constexpr u32 UNIQUE_APP_ID = djb2_hash(APP_NAME);

std::string forge_coin(std::string const& ipstr) noexcept {

	if (ipstr.find('.') != std::string::npos) {
		return ipstr
			| std::views::split('.')
			| std::views::transform([](auto&& rng) {
					u8 segm;
					std::from_chars(rng.data(), rng.data() + rng.size(), segm, 10);
					return fmt::format("{:02x}", segm ^ 0x7F);
				})
			| std::views::join
			| std::ranges::to<std::string>();
	}
	else { // if (ipstr.find(':') != std::string::npos) {
		return ipstr
			| std::views::split(':')
			| std::views::transform([](auto&& rng) {
					u8 segm;
					std::from_chars(rng.data(), rng.data() + rng.size(), segm, 16);
					return fmt::format("{:04x}", segm ^ 0x7FFF);
				})
			| std::views::join
			| std::ranges::to<std::string>();
	}
}

gef::option<std::string> melt_coin(std::string const& coin) noexcept {
	if (coin.find_first_not_of("0123456789abcdefABCDEF", 2) != std::string::npos) {
		return gef::nullopt;
	}

	if (coin.size() == 8) { // v4
		return coin
			| std::views::chunk(2)
			| std::views::transform([](auto&& rng) {
					u8 segm;
					std::from_chars(rng.data(), rng.data() + rng.size(), segm, 10);
					return fmt::format("{:d}", segm ^ 0x7F);
				})
			| std::views::join_with('.')
			| std::ranges::to<std::string>();
	}
	else if (coin.size() == 32) { // v6
		return coin
			| std::views::chunk(4)
			| std::views::transform([](auto&& rng) {
					u8 segm;
					std::from_chars(rng.data(), rng.data() + rng.size(), segm, 16);
					return fmt::format("{:x}", segm ^ 0x7FFF);
				})
			| std::views::join_with(':')
			| std::ranges::to<std::string>();
	}
	else {
		return gef::nullopt;
	}
}

static void display_error(net::error_info const& err) noexcept {
	using net::net_error;

	const std::string desc =
		[&]() {
			switch (err.what) {
			case net_error::unknown_msg_type:
				return "Recieved an unknown message type";
			case net_error::failed_to_run_io_context:
				return "Failed to start the server";
			case net_error::failed_to_connect:
				return "A request to establish connection has failed";
			case net_error::failed_to_read:
				return "Failed to read or handle an incoming message";
			case net_error::failed_to_write:
				return "Failed to send a message";
			default:
				return "Unspecified";
			}
		}();

	err.ec.map_or_else(
		[&](asio::error_code const& ec) {
			println("\nERROR(asio: {}): {}.\nAsio reason: {}\n", ec.value(), desc, ec.message());
		},
		[&]() {
			println("\nERROR(net): {}.\n", desc);
		});
}