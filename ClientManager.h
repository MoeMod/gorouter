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


#include "parsemsg.h"
#include "ServerManager.h"
#include "munge.h"
#include "net_buffer.h"

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
    endpoint_t srcds_endpoint;
    asio::ip::udp::socket& main_socket;
    const endpoint_t client_endpoint;
    endpoint_t::protocol_type::socket socket;
    unsigned short port;
    int has_server_num;
    bool reconnect;

public:
    explicit ClientData(ClientManager &outer, endpoint_t from) :
        cm(outer),
        ioc(outer.ioc),
        has_server_num(0),
        main_socket(outer.main_socket),
        client_endpoint(from),
        socket(ioc, endpoint_t(endpoint_t::protocol_type::v4(), 0))
    {
        last_recv_time = std::chrono::system_clock::now();
        reconnect = false;
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
                timeout_timer.expires_from_now(10s);
                co_await timeout_timer.async_wait(asio::use_awaitable);
                if (std::chrono::system_clock::now() > last_recv_time.load() + 10s)
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

	void SelectServer()
    {
        srcds_endpoint = GetRandomServer(ioc);
        ++has_server_num;
    }

	asio::awaitable<void> Co_ChangeDestIP()
    {
        //reconnect = true;
        //SelectServer();

        //constexpr char buffer[] = "\xFF\xFF\xFF\xFF" "\x09" "reconnect\n" "\0";
        //co_await main_socket.async_send_to(asio::buffer(buffer), client_endpoint, asio::use_awaitable);

        //const std::string response = "\xFF\xFF\xFF\xFF" "L" "127.0.0.1" ":" "27015";
        //std::size_t bytes_transferred = co_await socket.async_send_to(asio::buffer(response, sizeof(response)), client_endpoint, asio::use_awaitable);
        co_return;
    }

    asio::awaitable<void> Co_RedirectTest()
    {
        auto weak_that = weak_from_this();
        try
        {
        	while(auto that = weak_that.lock())
        	{
                using namespace std::chrono_literals;
                asio::system_timer timeout_timer(ioc, 5s);
                co_await timeout_timer.async_wait(asio::use_awaitable);
                co_await Co_ChangeDestIP();
        	}
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
            auto weak_that = weak_from_this();
            while (auto that = weak_that.lock())
            {
                endpoint_t sender_endpoint;
                char buffer[4096];
                std::size_t n = co_await socket.async_receive_from(asio::buffer(buffer), sender_endpoint, asio::use_awaitable);
                if (sender_endpoint == srcds_endpoint) {
                    // srcds => router
                	if(reconnect && n < 512)
                	{
                        BufferReader buf(buffer, n);
                        auto w1 = buf.ReadLong();
                        auto w2 = buf.ReadLong();
                        bool send_reliable = w1 & (1 << 31);
                        bool send_reliable_fragment = w1 & (1 << 30);
                		bool chan_incoming_reliable_sequence = w2 & (1 << 31);
                		int chan_outgoing_sequence = w1 & ~(1 << 31) & ~(1 << 30);
                        int chan_incoming_sequence = w2 & ~(1 << 31);
                        COM_UnMunge2((unsigned char*)buffer + 8, n - 8, (unsigned char)(chan_outgoing_sequence - 1));
#if 0

                        //chan_outgoing_sequence = 0;
                        //chan_incoming_sequence = 0;
                        send_reliable = true;
                        send_reliable_fragment = false;
                        //chan_incoming_reliable_sequence = false;
                		
                        sizebuf_t sb;
                        MSG_Init(&sb, "ServerPacketOverride", buffer, sizeof buffer);
                        //SelectServer();
                		
                        w1 = chan_outgoing_sequence | (send_reliable << 31);
                        if (send_reliable && send_reliable_fragment)
                            w1 |= (1 << 30);
                        w2 = chan_incoming_sequence | (chan_incoming_reliable_sequence << 31);
                        MSG_WriteLong(&sb, w1);
                        MSG_WriteLong(&sb, w2);
                		// MAX_STREAMS == 2
                        // 
                        //MSG_WriteByte(&sb, 0);
                        //MSG_WriteByte(&sb, 0);
                		
                        //MSG_WriteByte(&sb, 9);
                        //MSG_WriteString(&sb, "reconnect\n");

                        // svc_nop = 1
                        MSG_WriteByte(&sb, 7);
                        MSG_WriteByte(&sb, 7);
                        MSG_WriteByte(&sb, 7);
                        MSG_WriteByte(&sb, 7);
                        MSG_WriteByte(&sb, 1);
                        MSG_WriteByte(&sb, 1);
                        MSG_WriteByte(&sb, 1);
                        MSG_WriteByte(&sb, 1);
                        MSG_WriteByte(&sb, 1);

                        n = MSG_GetNumBytesWritten(&sb); // 20
#else
                        //SelectServer();
                        //char append[] = "\x01" "\x09" "reconnect\n";
                        //char append[] = "\x01";
                        //strcpy(buffer + n, append);
                        //n += strlen(append);
                        buffer[n++] = 'r';
                        buffer[n++] = 'e';
                        buffer[n++] = 'c';
                        buffer[n++] = 'o';
                        buffer[n++] = 'n';
                        buffer[n++] = 'n';
                        buffer[n++] = 'e';
                        buffer[n++] = 'c';
                        buffer[n++] = 't';
                        buffer[n++] = '\n';
                        buffer[n++] = '\0';
                        reconnect = false;
#endif
                        log("[RedirectTest]", " Co_RedirectTest()");
                        COM_Munge2((unsigned char*)buffer + 8, n - 8, (unsigned char)(chan_outgoing_sequence - 1));
                	}
                    co_await main_socket.async_wait(main_socket.wait_write, asio::use_awaitable);
                    std::size_t bytes_transferred = co_await main_socket.async_send_to(asio::const_buffer(buffer, n), client_endpoint, asio::use_awaitable);
                    //log("[ClientData]", " server ", srcds_endpoint, " forward to ", client_endpoint);
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
        //asio::co_spawn(ioc, Co_RedirectTest(), asio::detached);
    }

    asio::awaitable<void> OnRecv(const char *buffer, std::size_t n)
    {
        // router => srcds
    	if(!has_server_num)
            SelectServer();
        last_recv_time = std::chrono::system_clock::now();
        co_await socket.async_wait(socket.wait_write, asio::use_awaitable);
        co_await socket.async_send_to(asio::buffer(buffer, n), srcds_endpoint, asio::use_awaitable);
        //log("[ClientData]", " client ", client_endpoint, " forward to ", srcds_endpoint);
    }

	void OnReconnect()
    {
        SelectServer();
    }
};

inline std::shared_ptr<ClientData> ClientManager::AcceptClient(asio::io_context &ioc, ClientManager::endpoint_t client_endpoint) {
	if(auto cd = GetClientData(client_endpoint))
	{
        cd->OnReconnect();
        return cd;
	}
    auto cd = std::make_shared<ClientData>(*this, client_endpoint);
    m_ClientMap.emplace(client_endpoint, cd);
    cd->Run();
    auto read_endpoint = main_socket.local_endpoint();
    log("[", read_endpoint, "]", "Add new client ", client_endpoint, " (", m_ClientMap.size(), " total)");
    return cd;
}