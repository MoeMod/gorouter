#include <iostream>
#include <vector>

#include "log.hpp"
#include "ThreadPoolContext.h"
#include "ClientManager.h"
#include <boost/asio/awaitable.hpp>

#include "dummy_return.hpp"
#include <boost/asio/use_awaitable.hpp>

#include "TSourceEngineQuery.h"

const auto desc_host = "z4cs.com";
const auto desc_port = "6666";

bool IsValidInitialPacket(const char *buffer, std::size_t n)
{
    return n >= 4 && !strncmp(buffer, "\xFF\xFF\xFF\xFF", 4);
}

bool IsTSourceEngineQueryPacket(const char* buffer, std::size_t n)
{
    return (n >= 24 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "TSource Engine Query", 24))
		|| (n >= 11 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "details", 11))
		|| (n >= 8 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "info", 8));
}

bool IsPlayerListQueryPacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "U", 5);
}

bool IsPingPacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "i", 5);
}

using namespace boost::asio::ip;
using namespace std::chrono_literals;

class Citrus
{
    boost::asio::io_context &ioc;
    boost::asio::system_timer query_timer;
    udp::endpoint desc_endpoint;

    std::atomic<std::shared_ptr<const std::vector<TSourceEngineQuery::ServerInfoQueryResult>>> atomicServerInfoQueryResult;
    std::atomic<std::shared_ptr<const TSourceEngineQuery::PlayerListQueryResult>> atomicPlayerListQueryResult;
	
public:
    Citrus(boost::asio::io_context& ioc) :
        ioc(ioc),
        query_timer(ioc)
    {
        
    }

    void CoSpawn()
    {
        boost::asio::co_spawn(ioc, CoMain(), boost::asio::detached) ;
    }
	
    boost::asio::awaitable<void> CoMain()
    {
        udp::resolver resolver(ioc);

        auto desc_endpoints = co_await resolver.async_resolve(udp::v4(), desc_host, desc_port, boost::asio::use_awaitable);
        for (const auto& ep : desc_endpoints)
        {
            desc_endpoint = ep;
            log("[Start] Resolved IP Address ", desc_endpoint);
        }

        using namespace std::chrono_literals;

        boost::asio::co_spawn(ioc, CoCacheTSourceEngineQuery(), boost::asio::detached);
        boost::asio::co_spawn(ioc, CoHandlePlayerSection(27015), boost::asio::detached);
        boost::asio::co_spawn(ioc, CoHandlePlayerSection(6666), boost::asio::detached);
    }

    boost::asio::awaitable<void> CoCacheTSourceEngineQuery()
    {
        TSourceEngineQuery tseq(ioc);
        int failed_times = 0;
        while (true)
        {
            try
            {
                auto vecfinfo = co_await tseq.GetServerInfoDataAsync(desc_endpoint, 2s);
                auto new_spfinfo = std::make_shared<const std::vector<TSourceEngineQuery::ServerInfoQueryResult>>(vecfinfo);
                atomicServerInfoQueryResult.store(new_spfinfo);

                auto fplayer = co_await tseq.GetPlayerListDataAsync(desc_endpoint, 2s);
                auto new_spfplayer = std::make_shared<const TSourceEngineQuery::PlayerListQueryResult>(fplayer);
                atomicPlayerListQueryResult.store(new_spfplayer);

                if (!vecfinfo.empty())
                    log("[TSourceEngineQuery] Get TSourceEngineQuery success: ", vecfinfo[0].Map, " ", vecfinfo[0].PlayerCount, "/", vecfinfo[0].MaxPlayers);

                failed_times = 0;
            }
            catch (const boost::system::system_error& e)
            {
                ++failed_times;
                log("[TSourceEngineQuery] Get TSourceEngineQuery error with retry: ", e.what());
                continue;
            }
            query_timer.expires_from_now(15s);
            co_await query_timer.async_wait(boost::asio::use_awaitable);
        }
    }

    boost::asio::awaitable<void> CoHandlePlayerSection(unsigned short read_port)
    {
        udp::socket socket(ioc, udp::endpoint(boost::asio::ip::udp::v4(), read_port));
        auto read_endpoint = socket.local_endpoint();
        ClientManager MyClientManager(ioc, desc_endpoint, socket);
        char buffer[4096];
        int id = 0;
        log("[", read_endpoint, "]", "Start");
        while (true)
        {
            try
            {
                ++id;
                udp::endpoint sender_endpoint;
                std::size_t n = co_await socket.async_receive_from(boost::asio::buffer(buffer), sender_endpoint, boost::asio::use_awaitable);

                if (auto cd = MyClientManager.GetClientData(sender_endpoint))
                {
                    co_await cd->OnRecv(buffer, n);
                }
                else
                {
                    if (IsValidInitialPacket(buffer, n))
                    {
                        if (IsTSourceEngineQueryPacket(buffer, n))
                        {
                            if (auto spfinfo = std::atomic_load(&atomicServerInfoQueryResult))
                            {
                                char send_buffer[4096];
                                auto vecfinfo = *spfinfo;
                                for (auto finfo : vecfinfo)
                                {
                                    //finfo.PlayerCount = 233;
                                    std::size_t len;
                                    co_await socket.async_wait(socket.wait_write, boost::asio::use_awaitable);

                                    finfo.LocalAddress = (std::ostringstream() << read_endpoint).str();
                                    finfo.Port = read_endpoint.port();
                                    len = TSourceEngineQuery::WriteServerInfoQueryResultToBuffer(finfo, send_buffer, sizeof(buffer));
                                    co_await socket.async_send_to(boost::asio::const_buffer(send_buffer, len), sender_endpoint, boost::asio::use_awaitable);

                                    if (buffer[4] == 'd')
                                        log("[", read_endpoint, "]", "Reply package #", id, " details to ", sender_endpoint);
                                    else
                                        log("[", read_endpoint, "]", "Reply package #", id, " TSource Engine Query to ", sender_endpoint);
                                }
                            }
                        }
                        else if (IsPlayerListQueryPacket(buffer, n))
                        {
                            if (auto spfplayer = atomicPlayerListQueryResult.load())
                            {
                                char send_buffer[4096];
                                std::size_t len = TSourceEngineQuery::WritePlayerListQueryResultToBuffer(*spfplayer, send_buffer, sizeof(buffer));
                                co_await socket.async_wait(socket.wait_write, boost::asio::use_awaitable);
                                std::size_t bytes_transferred = co_await socket.async_send_to(boost::asio::const_buffer(send_buffer, len), sender_endpoint, boost::asio::use_awaitable);
                                log("[", read_endpoint, "]", "Reply package #", id, " A2S_PLAYERS to ", sender_endpoint);
                            }
                        }
                        else if (IsPingPacket(buffer, n) && false)
                        {
                            constexpr const char response[] = "\xFF\xFF\xFF\xFF" "j" "00000000000000";
                            std::size_t bytes_transferred = co_await socket.async_send_to(boost::asio::buffer(response, sizeof(response)), sender_endpoint, boost::asio::use_awaitable);
                        }
                        else
                        {
                            cd = MyClientManager.AcceptClient(ioc, sender_endpoint);
                            co_await cd->OnRecv(buffer, n);
                        }
                    }
                    else
                    {
                        log("[", read_endpoint, "]", "Drop package #", id, " due to not beginning with -1.");
                        continue;
                    }
                }
            }
            catch (const boost::system::system_error& e)
            {
                if (e.code() == boost::asio::error::connection_reset) // 10054
                    continue;

                if (e.code() == boost::asio::error::connection_refused) // 10061
                    continue;

                if (e.code() == boost::asio::error::connection_aborted) // 10053
                    continue;

                log("[", read_endpoint, "]", "Error with retry: ", e.what());
                continue;
            }
        }
    }

private:
	
};

int main()
{
    boost::asio::thread_pool pool; // 4 threads
	
    auto thread_pool = ThreadPoolContext::create();
    Citrus app(*thread_pool->as_io_context());
    app.CoSpawn();
	
    thread_pool->start();
    thread_pool->join();
    return 0;
}