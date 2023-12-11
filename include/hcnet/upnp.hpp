#pragma once

#include "canyon.hpp"
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include "error.hpp"

#include <string.h>

namespace net
{
    class Upnp {
    public:
        // WAN_port and LAN_port [6 bytes]
        // desc up to [80 bytes]
        // protocol "TCP" or "UDP" only! [4 bytes]
        Upnp(cstr WAN_port, cstr LAN_port, cstr desc, cstr protocol = "TCP") :
            m_WAN_port(WAN_port), m_LAN_port(LAN_port), m_description(desc), m_protocol(protocol)
        {

        }

        ~Upnp() {
            // free upnp stuff
            freeUPNPDevlist(upnp_dev);
            FreeUPNPUrls(&upnp_urls);
        }

        std::error_code Discover();
        std::error_code Get_valid_IGD();
        std::error_code Pull_wan_address();
        std::error_code Add_port_mapping();
        bool Port_mapping_exists();

    public:
        char LAN_address[64]{}; // C string of your LAN address
        char WAN_address[64]{}; // C string of your WAN address (ip)

    private:
        UPNPDev* upnp_dev = nullptr;
        UPNPUrls upnp_urls{};
        IGDdatas upnp_data{};

        cstr m_WAN_port;
        cstr m_LAN_port;
        cstr m_description;
        cstr m_protocol;
    };

    // Discover UPnP devices on the network.
    // - Returns 0 on success.
    std::error_code Upnp::Discover() {
        int err = 0;
        upnp_dev = upnpDiscover(
            2000, // time to wait (milliseconds)
            nullptr, // multicast interface (or null defaults to 239.255.255.250 [router addr] )
            nullptr, // path to minissdpd socket (or null defaults to /var/run/minissdpd.sock)
            0, // source port to use (or zero defaults to port 1900 [router port] )
            0, // 0==IPv4, 1==IPv6
            2, // ttl hop Limit, defult is 2
            &err); // error

        if (err != 0) {
            return _lib::make_ec_upnp(net::upnp_error::discover);
        }

        return {};
    }

    std::error_code Upnp::Get_valid_IGD() {
        // err = 1, A valid connected IGD has been found, anything else is an error
        int err = UPNP_GetValidIGD(upnp_dev, &upnp_urls, &upnp_data, LAN_address, sizeof(LAN_address));

        if (err != 1) {
            switch (err) {
            case 0:
                return _lib::make_ec_upnp(net::upnp_error::IGD_not_found);
            case 2:
                return _lib::make_ec_upnp(net::upnp_error::IGD_not_connected);
            case 3:
                return _lib::make_ec_upnp(net::upnp_error::upnp_without_IGD);
            default:
                return _lib::make_ec_upnp(net::upnp_error::unspecified);
            }
        }

        return {};
    }

    // Obtain WAN IP address
    std::error_code Upnp::Pull_wan_address() {
        int err = UPNP_GetExternalIPAddress(upnp_urls.controlURL, upnp_data.first.servicetype, WAN_address);

        if (err != 0) {
            return _lib::make_ec_upnp(net::upnp_error::pull_wan_address);
        }

        return {};
    }

    std::error_code Upnp::Add_port_mapping() {
        int err = UPNP_AddPortMapping(
            upnp_urls.controlURL, upnp_data.first.servicetype, m_WAN_port, m_LAN_port, LAN_address, m_description, m_protocol,
            nullptr, // remote (peer) host address or nullptr for no restriction
            "0"); // lease duration (in seconds), zero for permanent

        if (err != 0) {
            return _lib::make_ec_upnp(net::upnp_error::port_mapping);
        }

        return {};
    }

    bool Upnp::Port_mapping_exists() {
        size_t index = 0;
        while (true) {
            char wan_port[6] = "",
                lan_address[64] = "",
                lan_port[6] = "",
                protocol[4] = "",
                description[80] = "",
                mapping_enabled[4] = "",
                remote_host[64] = "",
                lease_duration[16] = "";

            int err = UPNP_GetGenericPortMappingEntry(
                upnp_urls.controlURL,
                upnp_data.first.servicetype,
                std::to_string(index).c_str(),
                wan_port,
                lan_address,
                lan_port,
                protocol,
                description,
                mapping_enabled,
                remote_host,
                lease_duration);

            // fmt::println("\n=====\nWAN port: {}\nLAN ip: {}\nLAN port: {}\nDesc: {}\nProto: {}\n", wan_port, lan_address, lan_port, description, protocol);
                
            if (err)
            {
                return false; // no more port mappings available
            }
            else if (
                !strcmp(m_WAN_port, wan_port) && 
                !strcmp(m_LAN_port, lan_port) && 
                !strcmp(m_description, description) &&
                !strcmp(m_protocol, protocol))
            {
                return true;
            }

            index++;
        }
    }
}