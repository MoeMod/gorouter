#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <shared_mutex>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <utility>

class ClientData
{
    using endpoint_t = boost::asio::ip::udp::endpoint;

    std::chrono::system_clock::time_point last_recv_time;
    const std::shared_ptr<boost::asio::io_context> ioc;
    const endpoint_t client_endpoint;
    const endpoint_t srcds_endpoint;
    const endpoint_t proxy_endpoint;
    endpoint_t::protocol_type::socket socket;
    std::function<void(std::vector<char>, boost::asio::yield_context yield)> callback;

    char buffer_send_to_srcds[4096];

public:
    explicit ClientData(std::shared_ptr<boost::asio::io_context> use_ioc, endpoint_t from, endpoint_t to, std::function<void(std::vector<char>, boost::asio::yield_context yield)> callbackOnRecvSrcds) :
        ioc(use_ioc),
        client_endpoint(from),
        srcds_endpoint(to),
        proxy_endpoint(endpoint_t::protocol_type::v4(), 0),
        socket(*ioc, proxy_endpoint),
        callback(std::move(callbackOnRecvSrcds))
    {
        last_recv_time = std::chrono::system_clock::now();

        boost::asio::spawn(*ioc, [this](boost::asio::yield_context yield) {
            char buffer[4096];
            try
            {
                while(true)
                {
                    endpoint_t sender_endpoint;
                    std::size_t n = socket.async_receive_from(boost::asio::buffer(buffer), sender_endpoint, yield);
                    if (sender_endpoint == srcds_endpoint) {
                        // srcds => router
                        boost::asio::spawn(*ioc, std::bind(callback, std::vector<char>(buffer, buffer + n), std::placeholders::_1));
                    }
                }
            }
            catch (std::exception& e)
            {
                std::cout << "Error: " << e.what() << std::endl;
            }
        });

        std::cout << "Add new client " << client_endpoint << " with new port " << proxy_endpoint << " " << std::endl;
    }

    void OnRecv(std::vector<char> buffer)
    {
        // router => srcds
        last_recv_time = std::chrono::system_clock::now();
        boost::asio::spawn(*ioc, [this, buffer = std::move(buffer)](boost::asio::yield_context yield) {
            socket.async_send_to(boost::asio::buffer(buffer), srcds_endpoint, yield);
        });
    }
};

class ClientManager
{
    using endpoint_t = boost::asio::ip::udp::endpoint;

    std::map<endpoint_t, std::shared_ptr<ClientData>> m_ClientMap;
    mutable std::shared_mutex sm;

public:
    // nullable
    std::shared_ptr<ClientData> GetClientData(endpoint_t ep) const
    {
        std::shared_lock sl(sm);
        auto iter = m_ClientMap.find(ep);
        return iter != m_ClientMap.end() ? iter->second : nullptr;
    }

    std::shared_ptr<ClientData> AcceptClient(std::shared_ptr<boost::asio::io_context> ioc, endpoint_t from, endpoint_t to, std::function<void(std::vector<char>, boost::asio::yield_context yield)> callbackOnRecvSrcds)
    {
        std::unique_lock sl(sm);
        auto cd = std::make_shared<ClientData>(ioc, from, to, callbackOnRecvSrcds);
        m_ClientMap.emplace(from, cd);
        return cd;
    }
};