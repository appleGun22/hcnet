#pragma once

#include "canyon.hpp"

namespace net {
	enum class net_error {
		unknown_msg_type
	};

	enum class upnp_error {
		discover,
		IGD_not_found,
		IGD_not_connected,
		upnp_without_IGD,
		port_mapping,
		pull_wan_address,
		unspecified
	};

	namespace _lib {

		class net_error_category : public std::error_category
		{
		public:
			const char* name() const noexcept final {
				return "net";
			}

			std::string message(int ev) const final {
				switch (static_cast<net_error>(ev)) {
				case net_error::unknown_msg_type:
					return "Recieved a message of an unknown type";
				default:
					return "Unknown error code";
				}
			}
		};


		// Upnp error codes

		class upnp_error_category : public std::error_category
		{
		public:
			const char* name() const noexcept final {
				return "upnp";
			}

			std::string message(int ev) const final {
				switch (static_cast<upnp_error>(ev)) {
				case upnp_error::discover:
					return "Couldn't discover UPnP devices on the network";
				case upnp_error::IGD_not_found:
					return "NO IGD found";
				case upnp_error::IGD_not_connected:
					return "A valid IGD has been found but it reported as not connected";
				case upnp_error::upnp_without_IGD:
					return "UPnP device has been found but was not recognized as an IGD";
				case upnp_error::port_mapping:
					return "Failed to add port via UPnP";
				case upnp_error::pull_wan_address:
					return "Failed to obtain WAN IP address";
				default:
					return "Unknown error code";
				}
			}
		};

		// builders

		std::error_code make_ec_net(net_error e) {
			return std::error_code{ static_cast<int>(e), net_error_category{} };
		}

		std::error_code make_ec_upnp(upnp_error e) {
			return std::error_code{ static_cast<int>(e), upnp_error_category{} };
		}

	}
}