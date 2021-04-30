#pragma once
#include <string>
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include "parse_ip.h"
inline std::string dest_servers[] = {
    "134.175.190.225:27016",
    "134.175.190.225:27010",
    "106.55.247.210:27015",
    "121.37.13.108:27013",
    "121.37.13.108:27013",
    "121.37.13.108:27011",
    "121.37.13.108:27017",
    "121.37.13.108:27020"
};
/*
inline std::string dest_servers[] = {
    "z4.moemod.com:6666"
};
*/
inline std::vector<asio::ip::udp::endpoint> dest_servers_endpoints;

asio::awaitable<void> InitServers(asio::io_context& ioc)
{
    using namespace asio::ip;
    for(auto dest_server : dest_servers)
    {
        auto [host, port] = ParseHostPort(dest_server);

        udp::resolver resolver(ioc);
        auto dest_endpoints = co_await resolver.async_resolve(udp::v4(), host, port, asio::use_awaitable);
        udp::endpoint dest_endpoint;
        for (const auto& ep : dest_endpoints)
            dest_endpoint = ep;

        dest_servers_endpoints.emplace_back(dest_endpoint);
        log("[ServerManager] ", "add server ip ", dest_endpoint);
    }
}

asio::ip::udp::endpoint GetRandomServer(asio::io_context &ioc)
{
    static std::size_t srv_id = 0;
    srv_id = (srv_id + 1) % dest_servers_endpoints.size();
    return dest_servers_endpoints[srv_id];
}