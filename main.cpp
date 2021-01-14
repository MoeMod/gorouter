#include <iostream>
#include <vector>
#include <span>
#include <numeric>
#include <ranges>

#include "log.hpp"
#include "ClientManager.h"
#include <asio/awaitable.hpp>

#include "dummy_return.hpp"
#include <asio/use_awaitable.hpp>

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

using namespace asio::ip;
using namespace std::chrono_literals;

class Citrus
{
    asio::io_context &ioc;
    udp::endpoint desc_endpoint;

    std::optional<std::vector<TSourceEngineQuery::ServerInfoQueryResult>> ServerInfoQueryResultCache;
    std::optional<TSourceEngineQuery::PlayerListQueryResult> PlayerListQueryResultCache;
	
public:
    Citrus(asio::io_context& ioc) :
        ioc(ioc)
    {
        
    }

	template<std::ranges::input_range Ports>
    void CoSpawn(Ports ports)
    {
        asio::co_spawn(ioc, CoMain(ports), asio::detached) ;
    }

    template<std::ranges::input_range Ports>
    asio::awaitable<void> CoMain(Ports ports)
    {
        udp::resolver resolver(ioc);

        auto desc_endpoints = co_await resolver.async_resolve(udp::v4(), desc_host, desc_port, asio::use_awaitable);
        for (const auto& ep : desc_endpoints)
        {
            desc_endpoint = ep;
            log("[Start] Resolved IP Address ", desc_endpoint);
        }

        using namespace std::chrono_literals;

        asio::co_spawn(ioc, CoCacheTSourceEngineQuery(), asio::detached);

    	for(unsigned int port : ports)
            asio::co_spawn(ioc, CoHandlePlayerSection(port), asio::detached);
    }

    asio::awaitable<void> CoCacheTSourceEngineQuery()
    {
        TSourceEngineQuery tseq(ioc);
        std::atomic_int failed_times = 0;
        while (true)
        {
            try
            {
                auto vecfinfo = co_await tseq.GetServerInfoDataAsync(desc_endpoint, 500ms);
                auto fplayer = co_await tseq.GetPlayerListDataAsync(desc_endpoint, 500ms);

                if (!vecfinfo.empty())
                    log("[TSourceEngineQuery] Get TSourceEngineQuery success: ", vecfinfo[0].Map, " ", vecfinfo[0].PlayerCount, "/", vecfinfo[0].MaxPlayers);
                else
                    log("[TSourceEngineQuery] Get TSourceEngineQuery failed");

                ServerInfoQueryResultCache = std::move(vecfinfo);
                PlayerListQueryResultCache = std::move(fplayer);

                failed_times.store(0);
            	
                asio::system_timer query_timer(ioc, 15s);
                co_await query_timer.async_wait(asio::use_awaitable);
            }
            catch (const asio::system_error& e)
            {
                failed_times.fetch_add(1);
                log("[TSourceEngineQuery] Get TSourceEngineQuery error with retry: ", e.what());
                continue;
            }
        }
    }

    asio::awaitable<void> CoHandlePlayerSection(unsigned short read_port)
    {
        udp::socket socket(ioc, udp::endpoint(asio::ip::udp::v4(), read_port));
        auto read_endpoint = socket.local_endpoint();
        ClientManager MyClientManager(ioc, desc_endpoint, socket);
        char buffer[4096];
        int id = 0;
        log("[", read_endpoint, "]", "Start");
        co_await socket.async_wait(socket.wait_read, asio::use_awaitable);
        while (true)
        {
            try
            {
                ++id;
                udp::endpoint sender_endpoint;
                std::size_t n = co_await socket.async_receive_from(asio::buffer(buffer), sender_endpoint, asio::use_awaitable);

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
                            try{
                                auto vecfinfo = ServerInfoQueryResultCache.value();
                                char send_buffer[4096];
                                for (auto finfo : vecfinfo)
                                {
                                    //finfo.PlayerCount = 233;
                                    std::size_t len;
                                    co_await socket.async_wait(socket.wait_write, asio::use_awaitable);

                                    std::ostringstream oss;
                                    oss << read_endpoint;
                                    finfo.LocalAddress = oss.str();
                                    finfo.Port = read_endpoint.port();
                                    len = TSourceEngineQuery::WriteServerInfoQueryResultToBuffer(finfo, send_buffer, sizeof(buffer));
                                    co_await socket.async_send_to(asio::const_buffer(send_buffer, len), sender_endpoint, asio::use_awaitable);

                                    if (buffer[4] == 'd')
                                        log("[", read_endpoint, "]", "Reply package #", id, " details to ", sender_endpoint);
                                    else
                                        log("[", read_endpoint, "]", "Reply package #", id, " TSource Engine Query to ", sender_endpoint);
                                }
                            }
                            catch(const std::bad_optional_access &e) {}
                        }
                        else if (IsPlayerListQueryPacket(buffer, n))
                        {
                            try {
                                auto fplayer = PlayerListQueryResultCache.value();
                                char send_buffer[4096];
                                std::size_t len = TSourceEngineQuery::WritePlayerListQueryResultToBuffer(fplayer, send_buffer, sizeof(buffer));
                                co_await socket.async_wait(socket.wait_write, asio::use_awaitable);
                                std::size_t bytes_transferred = co_await socket.async_send_to(asio::const_buffer(send_buffer, len), sender_endpoint, asio::use_awaitable);
                                log("[", read_endpoint, "]", "Reply package #", id, " A2S_PLAYERS to ", sender_endpoint);
                            }
                            catch(const std::bad_optional_access &e) {}
                        }
                        else if (IsPingPacket(buffer, n) && false)
                        {
                            constexpr const char response[] = "\xFF\xFF\xFF\xFF" "j" "00000000000000";
                            std::size_t bytes_transferred = co_await socket.async_send_to(asio::buffer(response, sizeof(response)), sender_endpoint, asio::use_awaitable);
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
            catch (const asio::system_error& e)
            {
                if (e.code() == asio::error::connection_reset) // 10054
                    continue;

                if (e.code() == asio::error::connection_refused) // 10061
                    continue;

                if (e.code() == asio::error::connection_aborted) // 10053
                    continue;

                log("[", read_endpoint, "]", "Error with retry: ", e.what());
                continue;
            }
        }
    }
};

template<std::ranges::input_range ArgsRange>
std::vector<unsigned int> GetPortsFromArgs(ArgsRange spsv)
{
    std::vector<unsigned int> res;
    bool port = false;
    bool ports = false;
    bool portsnum = false;
	for(std::string_view sv : spsv)
	{
		try
		{
            if (std::exchange(port, false))
            {
                res.push_back(std::stoi(std::string(sv)));
            }
            else if (std::exchange(ports, false))
            {
                auto left = sv.substr(0, sv.find('-'));
                auto right = sv.substr(sv.find('-') + 1);
                auto left_port = std::stoi(std::string(left));
                auto right_port = std::stoi(std::string(right));
                auto last_size = res.size();
                res.resize(last_size + std::abs(right_port - left_port) + 1);
                std::iota(res.begin() + last_size, res.end(), std::min(right_port, left_port));
            }
            else if (std::exchange(portsnum, false))
            {
                auto num = std::stoi(std::string(sv));
                std::fill_n(std::back_inserter(res), num, 0);
            }
		}
        catch (const std::exception& e)
        {
            log("[GetPortsFromArgs] error: ", e.what());
        }
        if (sv == "-port")
            port = true;
		else if (sv == "-ports")
            ports = true;
        else if (sv == "-portsnum")
            portsnum = true;
	}
    if (res.empty())
        res.push_back(27015);
    return res;
}

int main(int argc, char *argv[])
{
    auto spsv = std::span<char *>(argv, argc) | std::ranges::views::transform([](const char* arg) { return std::string_view(arg); });
    auto ports = GetPortsFromArgs(spsv);
    asio::io_context ioc;
    auto wg = make_work_guard(ioc);
    Citrus app(ioc);
    app.CoSpawn(ports);
	
    ioc.run();
    return 0;
}