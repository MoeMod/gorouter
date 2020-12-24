#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <shared_mutex>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <utility>

class ClientData;
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

    std::shared_ptr<ClientData> AcceptClient(std::shared_ptr<boost::asio::io_context> ioc, endpoint_t from, endpoint_t to, std::function<void(const char *, std::size_t, boost::asio::yield_context yield)> callbackOnRecvSrcds);


    std::shared_ptr<ClientData> RemoveClient(endpoint_t ep)
    {
        std::unique_lock ul(sm);
        if(auto iter = m_ClientMap.find(ep); iter != m_ClientMap.end())
        {
            auto sp = iter->second;
            m_ClientMap.erase(iter);
            return sp;
        }
        return nullptr;
    }
};

class ClientData : public std::enable_shared_from_this<ClientData>
{
    using endpoint_t = boost::asio::ip::udp::endpoint;

    std::chrono::system_clock::time_point last_recv_time;
    ClientManager &cm;
    const std::shared_ptr<boost::asio::io_context> ioc;
    const endpoint_t client_endpoint;
    const endpoint_t srcds_endpoint;
    endpoint_t::protocol_type::socket socket;
    boost::asio::system_timer timeout_timer;
    std::function<void(const char *, std::size_t, boost::asio::yield_context yield)> callback;

public:
    explicit ClientData(ClientManager &outer, std::shared_ptr<boost::asio::io_context> use_ioc, endpoint_t from, endpoint_t to, std::function<void(const char *, std::size_t, boost::asio::yield_context yield)> callbackOnRecvSrcds) :
        cm(outer),
        ioc(use_ioc),
        client_endpoint(from),
        srcds_endpoint(to),
        socket(*ioc, endpoint_t(endpoint_t::protocol_type::v4(), 0)),
        timeout_timer(*ioc),
        callback(std::move(callbackOnRecvSrcds))
    {
        last_recv_time = std::chrono::system_clock::now();
        std::cout << "Add new client " << client_endpoint << " with port " << socket.local_endpoint().port() << " " << std::endl;
    }

    ~ClientData()
    {
        std::cout << "Remove client " << client_endpoint << " with port " << socket.local_endpoint().port() << " " << std::endl;
    }

	void Run()
    {
        boost::asio::spawn(*ioc, [this, that = weak_from_this()](boost::asio::yield_context yield) {
            char buffer[4096];
            try
            {
                while (true)
                {
                    endpoint_t sender_endpoint;
                    std::size_t n = socket.async_receive_from(boost::asio::buffer(buffer), sender_endpoint, yield);
                    if (sender_endpoint == srcds_endpoint) {
                        // srcds => router
                        callback(buffer, n, yield);
                    }
                }
            }
            catch (const boost::system::system_error& e)
            {
                if (e.code() == boost::system::errc::operation_canceled)
                    return;
                std::cout << "Error: " << e.what() << std::endl;
            }
        });

        // timeout
        boost::asio::spawn(*ioc, [this](boost::asio::yield_context yield) {
            using namespace std::chrono_literals;
            try
            {
                while (true)
                {
                    timeout_timer.expires_from_now(10s);
                    timeout_timer.async_wait(yield);
                    if (std::chrono::system_clock::now() > last_recv_time + 10s)
                        break;
                }

                auto that = cm.RemoveClient(client_endpoint);
                // auto delete this
            }
            catch (const boost::system::system_error& e)
            {
                if (e.code() == boost::system::errc::operation_canceled)
                    return;
                std::cout << "Error: " << e.what() << std::endl;
            }
            });

    }

    void OnRecv(const char *buffer, std::size_t n, boost::asio::yield_context yield)
    {
        // router => srcds
        last_recv_time = std::chrono::system_clock::now();
        socket.async_send_to(boost::asio::buffer(buffer, n), srcds_endpoint, yield);
    }
};



inline std::shared_ptr<ClientData> ClientManager::AcceptClient(std::shared_ptr<boost::asio::io_context> ioc, ClientManager::endpoint_t from, ClientManager::endpoint_t to, std::function<void(const char *, std::size_t, boost::asio::yield_context yield)> callbackOnRecvSrcds) {
    std::unique_lock sl(sm);
    auto cd = std::make_shared<ClientData>(*this, ioc, from, to, callbackOnRecvSrcds);
    m_ClientMap.emplace(from, cd);
    cd->Run();
    return cd;
}