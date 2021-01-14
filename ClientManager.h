#pragma once

#include "log.hpp"
#include <asio.hpp>
#include <asio/awaitable.hpp>

#include "dummy_return.hpp"
#include <asio/use_awaitable.hpp>

#include <shared_mutex>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <utility>
#include <system_error>

class ClientData;

class ClientManager
{
    friend class ClientData;
    using endpoint_t = asio::ip::udp::endpoint;

    std::map<endpoint_t, std::shared_ptr<ClientData>> m_ClientMap;
    mutable std::shared_mutex sm;
    asio::io_context& ioc;
    const endpoint_t srcds_endpoint;
    asio::ip::udp::socket& main_socket;

public:
    ClientManager(asio::io_context& use_ioc, endpoint_t to, asio::ip::udp::socket& out_socket) :
        ioc(use_ioc),
        srcds_endpoint(to),
        main_socket(out_socket)
	{}
    
    // nullable
    std::shared_ptr<ClientData> GetClientData(endpoint_t ep) const
    {
        std::shared_lock sl(sm);
        auto iter = m_ClientMap.find(ep);
        return iter != m_ClientMap.end() ? iter->second : nullptr;
    }

    std::shared_ptr<ClientData> AcceptClient(asio::io_context& ioc, ClientManager::endpoint_t from);

    std::shared_ptr<ClientData> RemoveClient(endpoint_t client_endpoint)
    {
        std::unique_lock ul(sm);
        if(auto iter = m_ClientMap.find(client_endpoint); iter != m_ClientMap.end())
        {
            auto sp = iter->second;
            m_ClientMap.erase(iter);
        	
            auto read_endpoint = main_socket.local_endpoint();
            log("[", read_endpoint, "]", "Remove client ", client_endpoint, " (", m_ClientMap.size(), " total)");
        	
            return sp;
        }
        return nullptr;
    }
};

class ClientData : public std::enable_shared_from_this<ClientData>
{
    using endpoint_t = asio::ip::udp::endpoint;

    std::atomic<std::chrono::system_clock::time_point> last_recv_time;
    ClientManager &cm;
    asio::io_context& ioc;
    const endpoint_t srcds_endpoint;
    asio::ip::udp::socket& main_socket;
    const endpoint_t client_endpoint;
    endpoint_t::protocol_type::socket socket;
    unsigned short port;

public:
    explicit ClientData(ClientManager &outer, endpoint_t from) :
        cm(outer),
        ioc(outer.ioc),
        srcds_endpoint(outer.srcds_endpoint),
        main_socket(outer.main_socket),
        client_endpoint(from),
        socket(ioc, endpoint_t(endpoint_t::protocol_type::v4(), 0))
    {
        last_recv_time = std::chrono::system_clock::now();
    }

    ~ClientData()
    {
    }

    asio::awaitable<void> Co_Timer()
    {
        using namespace std::chrono_literals;
        try
        {
            asio::system_timer timeout_timer(ioc);
            while (true)
            {
                timeout_timer.expires_from_now(3s);
                co_await timeout_timer.async_wait(asio::use_awaitable);
                if (std::chrono::system_clock::now() > last_recv_time.load() + 3s)
                    break;
            }

            socket.close();
            auto that = cm.RemoveClient(client_endpoint);
            // auto delete this
        }
        catch (const asio::system_error& e)
        {
            if (e.code() == asio::error::operation_aborted)
                co_return;
            std::cout << "Error: " << e.what() << std::endl;
        }
    }

    asio::awaitable<void> Co_Run()
    {
        try
        {
            while (true)
            {
                auto that = shared_from_this();
                endpoint_t sender_endpoint;
                char buffer[4096];
                std::size_t n = co_await socket.async_receive_from(asio::buffer(buffer), sender_endpoint, asio::use_awaitable);
                if (sender_endpoint == srcds_endpoint) {
                    // srcds => router
                    co_await main_socket.async_wait(main_socket.wait_write, asio::use_awaitable);
                    std::size_t bytes_transferred = co_await main_socket.async_send_to(asio::const_buffer(buffer, n), client_endpoint, asio::use_awaitable);
                    continue;
                }
            }
        }
        catch (const asio::system_error& e)
        {
            if (e.code() == asio::error::operation_aborted)
                co_return;
            std::cout << "Error: " << e.what() << std::endl;
        }
    }

	void Run()
    {
        asio::co_spawn(ioc, Co_Timer(), asio::detached);
        asio::co_spawn(ioc, Co_Run(), asio::detached);
    }

    asio::awaitable<void> OnRecv(const char *buffer, std::size_t n)
    {
        // router => srcds
        last_recv_time = std::chrono::system_clock::now();
        co_await socket.async_wait(socket.wait_write, asio::use_awaitable);
        co_await socket.async_send_to(asio::buffer(buffer, n), srcds_endpoint, asio::use_awaitable);
    }
};

inline std::shared_ptr<ClientData> ClientManager::AcceptClient(asio::io_context &ioc, ClientManager::endpoint_t client_endpoint) {
    std::unique_lock sl(sm);
    auto cd = std::make_shared<ClientData>(*this, client_endpoint);
    m_ClientMap.emplace(client_endpoint, cd);
    cd->Run();
    auto read_endpoint = main_socket.local_endpoint();
    log("[", read_endpoint, "]", "Add new client ", client_endpoint, " (", m_ClientMap.size(), " total)");
    return cd;
}