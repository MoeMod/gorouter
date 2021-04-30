#include <iostream>
#include <vector>
#include <span>
#include <numeric>
#include <ranges>
#include <random>

#include "server_name.h"
#include "log.hpp"
#include "ClientManager.h"
#include "ServerManager.h"
#include <asio/awaitable.hpp>

#include "dummy_return.hpp"
#include <asio/use_awaitable.hpp>

#include "TSourceEngineQuery.h"
#include "parse_args.h"
#include "parse_ip.h"

#ifdef ENABLE_STEAM_SUPPORT
#include <archtypes.h>
#include <steam/steam_api.h>
#include <steam/steam_gameserver.h>
#endif

const auto desc_host = "z4.moemod.com";
std::string master_servers[] = {
    "hlmaster.net:27010",
    "hl1master.dt-club.net:27010",
    "hl1master.steampowered.com:27010",
    "css.setti.info:27010", // alive
    "188.40.40.201:27010", // alive
    "69.28.140.247:27010",
    "209.197.20.34:27010",
    "69.28.158.131:27010",
    "hl1master.5eplay.com:27010",
	"68.142.72.250:27010"
};

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

bool IsServerListResPacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "f", 5);
}

bool IsPingPacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "i", 5);
}

bool IsChallengePacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "getchallenge", 16);
}

using namespace asio::ip;
using namespace std::chrono_literals;

class Citrus
{
    asio::io_context& ioc;
    udp::endpoint desc_endpoint;

    std::optional<std::vector<TSourceEngineQuery::ServerInfoQueryResult>> ServerInfoQueryResultCache;
    std::optional<TSourceEngineQuery::PlayerListQueryResult> PlayerListQueryResultCache;

public:
    Citrus(asio::io_context& ioc) :
        ioc(ioc)
    {

    }

    template<std::ranges::input_range Ports, class...Args>
    void CoSpawn(Ports ports, unsigned dest_port, Args &&...args)
    {
        log("Cirtus HLDS router BUILD ", __DATE__, " ", __TIME__);
        asio::co_spawn(ioc, CoMain(ports, dest_port, std::forward<Args>(args)...), asio::detached);
    }

    template<std::ranges::input_range Ports, class...Args>
    asio::awaitable<void> CoMain(Ports ports, unsigned dest_port, const Args &...args)
    {
        udp::resolver resolver(ioc);

        auto desc_endpoints = co_await resolver.async_resolve(udp::v4(), desc_host, std::to_string(dest_port), asio::use_awaitable);
        for (const auto& ep : desc_endpoints)
        {
            desc_endpoint = ep;
            log("[Start] Resolved IP Address ", desc_endpoint);
        }

        co_await InitServers(ioc);

        using namespace std::chrono_literals;

        asio::co_spawn(ioc, CoCacheTSourceEngineQuery(), asio::detached);

        for (unsigned int port : ports)
            asio::co_spawn(ioc, CoHandlePlayerSection(port, args...), asio::detached);
    }

    asio::awaitable<void> CoHandleMasterServer(udp::socket& socket, std::string master_host, std::string master_port)
    {
        udp::resolver resolver(ioc);

        udp::endpoint master_endpoint;
        try
        {
            auto desc_endpoints = co_await resolver.async_resolve(udp::v4(), master_host, master_port, asio::use_awaitable);
            for (const auto& ep : desc_endpoints)
            {
                master_endpoint = ep;
                log("[MasterServer] Resolved IP Address ", master_endpoint);
            }
        }
        catch (const std::system_error& e)
        {
            log("[MasterServer] Resolved IP Address ", master_host, ":", master_port, " failed:", e.what());
            co_return;
        }

        constexpr auto S2M_HEARTBEAT3 = 'Z';
        constexpr auto S2M_HEARTBEAT2 = '0';
        constexpr auto S2M_GETFILE = 'J';

        constexpr char buffer[] = "\xFF\xFF\xFF\xFF" "Zcstrike" "\0";
        constexpr char buffer2[] = "\xFF\xFF\xFF\xFF" "i";

        std::atomic_int failed_times = 0;
        while (true)
        {
            try
            {
                co_await socket.async_send_to(asio::buffer(buffer), master_endpoint, asio::use_awaitable);
                co_await socket.async_send_to(asio::buffer(buffer2), master_endpoint, asio::use_awaitable);

                failed_times.store(0);

                asio::system_timer query_timer(ioc, 5s);
                co_await query_timer.async_wait(asio::use_awaitable);
                log("[MasterServer] Send S2M_HEARTBEAT3 to ", master_endpoint);
            }
            catch (const asio::system_error& e)
            {
                failed_times.fetch_add(1);
                log("[MasterServer] Send S2M_HEARTBEAT3 failed with retry: ", e.what());
                continue;
            }
        }
    }

    asio::awaitable<void> CoCacheTSourceEngineQuery()
    {
        TSourceEngineQuery tseq(ioc);
        std::atomic_int failed_times = 0;
        bool wait = false;
        while (true)
        {
            try
            {
            	// dont know why we should add a delay here.
            	if(wait)
            	{
                    asio::system_timer query_timer(ioc, 180s);
                    co_await query_timer.async_wait(asio::use_awaitable);
            	}
            	
                auto vecfinfo = co_await tseq.GetServerInfoDataAsync(desc_endpoint, 500ms);
                auto fplayer = co_await tseq.GetPlayerListDataAsync(desc_endpoint, 500ms);

                if (!vecfinfo.empty())
                    log("[TSourceEngineQuery] Get TSourceEngineQuery success: ", vecfinfo[0].Map, " ", vecfinfo[0].PlayerCount, "/", vecfinfo[0].MaxPlayers);
                else
                    log("[TSourceEngineQuery] Get TSourceEngineQuery failed");
#ifdef ENABLE_STEAM_SUPPORT
                if (!vecfinfo.empty())
                {
                    const auto& info = vecfinfo[0];
                    UpdateSteamInfoConfig(info);
                }
#endif

                ServerInfoQueryResultCache = std::move(vecfinfo);
                PlayerListQueryResultCache = std::move(fplayer);

                failed_times.store(0);

                wait = true;
                continue;
            }
            catch (const asio::system_error& e)
            {
                failed_times.fetch_add(1);
                log("[TSourceEngineQuery] Get TSourceEngineQuery error with retry: ", e.what());
            	
                wait = true;
                continue;
            }
        }
    }

    template<std::ranges::random_access_range ServerNames, std::ranges::random_access_range MapNames>
    asio::awaitable<void> CoHandlePlayerSection(unsigned short read_port, ServerNames server_names, MapNames map_names, int player_num)
    {
        udp::socket socket(ioc);
        try
        {
            socket = udp::socket(ioc, udp::endpoint(asio::ip::udp::v4(), read_port));

        }
        catch (const std::system_error& e)
        {
            log("[CoHandlePlayerSection] socket port ", read_port, " start failed: ", e.what());
            co_return;
        }
#ifdef ENABLE_STEAM_SUPPORT
        asio::co_spawn(ioc, CoHandleSteamServer(socket, server_names, map_names, player_num), asio::detached);
#endif
        auto read_endpoint = socket.local_endpoint();
        /*
        for (const auto &server : master_servers)
        {
            auto [host, port] = ParseHostPort(server);
            asio::co_spawn(ioc, CoHandleMasterServer(socket, host, port), asio::detached);
        }
        */
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
                co_await socket.async_wait(socket.wait_read, asio::use_awaitable);
                std::size_t n = co_await socket.async_receive_from(asio::buffer(buffer), sender_endpoint, asio::use_awaitable);

                if (IsChallengePacket(buffer, n))
                {
                    auto cd = MyClientManager.AcceptClient(ioc, sender_endpoint);
                    co_await cd->OnRecv(buffer, n);
                }
            	else if (auto cd = MyClientManager.GetClientData(sender_endpoint))
                {
                    co_await cd->OnRecv(buffer, n);
                }
                else
                {
                    if (IsValidInitialPacket(buffer, n))
                    {
#ifdef ENABLE_STEAM_SUPPORT
                    	if(SteamGameServer()->BLoggedOn() && !IsTSourceEngineQueryPacket(buffer, n) && !IsPlayerListQueryPacket(buffer, n))
                        {
                            auto fromip = sender_endpoint.address().to_v4().to_uint();
                            auto port = sender_endpoint.port();
                            SteamGameServer()->HandleIncomingPacket(buffer, n, fromip, port);
                        }
#endif
                        if (IsTSourceEngineQueryPacket(buffer, n))
                        {
                            if (ServerInfoQueryResultCache.has_value()) {
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

                                    static std::random_device rd;
                                    //if (!std::uniform_int_distribution<std::size_t>(0, 10)(rd))
                                	if(auto size = std::ranges::distance(server_names))
                                    {
                                        auto& str = server_names[std::uniform_int_distribution<std::size_t>(0, size - 1)(rd)];
                                        finfo.ServerName = str;
                                    }
                                	if(auto size = std::ranges::distance(map_names))
                                    {
                                        auto& str = map_names[std::uniform_int_distribution<std::size_t>(0, size - 1)(rd)];
                                        finfo.Map = str;
                                    }

                                    finfo.VAC = id % 2;
                                    if (player_num >= 0 && player_num <= finfo.MaxPlayers)
                                        finfo.PlayerCount = player_num;

                                    len = TSourceEngineQuery::WriteServerInfoQueryResultToBuffer(finfo, send_buffer, sizeof(buffer));
                                    co_await socket.async_send_to(asio::const_buffer(send_buffer, len), sender_endpoint, asio::use_awaitable);

                                    if (buffer[4] == 'd')
                                        log("[", read_endpoint, "]", "Reply package #", id, " details to ", sender_endpoint);
                                    else
                                        log("[", read_endpoint, "]", "Reply package #", id, " TSource Engine Query to ", sender_endpoint);
                                }
                            }
                        }
                        else if (IsPlayerListQueryPacket(buffer, n))
                        {
                            if (PlayerListQueryResultCache.has_value()) {
                                auto fplayer = PlayerListQueryResultCache.value();
                                char send_buffer[4096];
                                std::size_t len = TSourceEngineQuery::WritePlayerListQueryResultToBuffer(fplayer, send_buffer, sizeof(buffer));
                                co_await socket.async_wait(socket.wait_write, asio::use_awaitable);
                                std::size_t bytes_transferred = co_await socket.async_send_to(asio::const_buffer(send_buffer, len), sender_endpoint, asio::use_awaitable);
                                log("[", read_endpoint, "]", "Reply package #", id, " A2S_PLAYERS to ", sender_endpoint);
                            }
                        }
                    	/*
                        else if (IsChallengePacket(buffer, n) && false)
                        {
                            // connect packet
                            cd = MyClientManager.AcceptClient(ioc, sender_endpoint);
                            const std::string response = "\xFF\xFF\xFF\xFF" "L" + std::string(desc_host) + ":" + std::to_string(desc_endpoint.port());
                            std::size_t bytes_transferred = co_await socket.async_send_to(asio::buffer(response, sizeof(response)), sender_endpoint, asio::use_awaitable);
                            log("[", read_endpoint, "]", "Reply package #", id, " redirect to ", sender_endpoint);
                        }
                        */
                        else if (IsPingPacket(buffer, n))
                        {
                            constexpr const char response[] = "\xFF\xFF\xFF\xFF" "j\r\n";
                            std::size_t bytes_transferred = co_await socket.async_send_to(asio::buffer(response, sizeof(response)), sender_endpoint, asio::use_awaitable);
                        }
                        else if (IsServerListResPacket(buffer, n) && false)
                        {
                            // ignored
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
#ifdef ENABLE_STEAM_SUPPORT
    class CSteam3Server
    {
    protected:
        bool m_bHasActivePlayers;
        bool m_bWantToBeSecure;
        bool m_bLanOnly;
        CSteamID m_SteamIDGS;

    public:
        CSteam3Server() :
            m_CallbackGSClientApprove(this, &CSteam3Server::OnGSClientApprove),
            m_CallbackGSClientDeny(this, &CSteam3Server::OnGSClientDeny),
            m_CallbackGSClientKick(this, &CSteam3Server::OnGSClientKick),
            m_CallbackGSPolicyResponse(this, &CSteam3Server::OnGSPolicyResponse),
            m_CallbackLogonSuccess(this, &CSteam3Server::OnLogonSuccess),
            m_CallbackLogonFailure(this, &CSteam3Server::OnLogonFailure),
            m_SteamIDGS(1, 0, k_EUniverseInvalid, k_EAccountTypeInvalid)
    	{}

        STEAM_GAMESERVER_CALLBACK(CSteam3Server, OnGSClientApprove, GSClientApprove_t, m_CallbackGSClientApprove)
        {
            auto steamid = pParam->m_SteamID;
            log("[Steam] ", " OnGSClientApprove steamid = ", steamid.ConvertToUint64());
        }

        STEAM_GAMESERVER_CALLBACK(CSteam3Server, OnGSClientDeny, GSClientDeny_t, m_CallbackGSClientDeny)
        {
            auto steamid = pParam->m_SteamID;
            log("[Steam] ", " OnGSClientDeny steamid = ", steamid.ConvertToUint64());
        }

        STEAM_GAMESERVER_CALLBACK(CSteam3Server, OnGSClientKick, GSClientKick_t, m_CallbackGSClientKick)
        {
            auto steamid = pParam->m_SteamID;
            auto reason = pParam->m_eDenyReason;
            log("[Steam] ", " OnGSClientKick steamid = ", steamid.ConvertToUint64(), " reason = ", reason);
        }

        STEAM_GAMESERVER_CALLBACK(CSteam3Server, OnGSPolicyResponse, GSPolicyResponse_t, m_CallbackGSPolicyResponse)
        {
            if (pParam->m_bSecure)
                log("[Steam] ", "   VAC secure mode is activated.");
            else
                log("[Steam] ", "   VAC secure mode disabled.");
        }

        STEAM_GAMESERVER_CALLBACK(CSteam3Server, OnLogonSuccess, SteamServersConnected_t, m_CallbackLogonSuccess)
        {
            log("[Steam] ", "Connection to Steam servers successful.");

            m_SteamIDGS = SteamGameServer()->GetSteamID();
            //CSteam3Server::SendUpdatedServerDetails();
        }

        STEAM_GAMESERVER_CALLBACK(CSteam3Server, OnLogonFailure, SteamServerConnectFailure_t, m_CallbackLogonFailure)
        {
            log("[Steam] ", "Could not establish connection to Steam servers.");
        }
    };

    template<std::ranges::random_access_range ServerNames, std::ranges::random_access_range MapNames>
    asio::awaitable<void> CoHandleSteamServer(udp::socket &socket, ServerNames server_names, MapNames map_names, int player_num)
    {
        auto steam_port = 26900;
        auto game_endpoint = socket.local_endpoint();
        if(!SteamGameServer_Init(game_endpoint.address().to_v4().to_uint(), steam_port, game_endpoint.port(), 0xFFFFu, eServerModeAuthenticationAndSecure, "1.1.2.7"))
        {
            log("[Steam] ", "Unable to initialize Steam.");
            co_return;
        }

        CSteam3Server steamsrv;
    	
        using namespace std::chrono_literals;
        // send
        asio::co_spawn(ioc, [&ioc = this->ioc, &socket]()->asio::awaitable<void> {
            try {
                while (1) {
                    asio::system_timer ddl(ioc, std::chrono::duration_cast<std::chrono::system_clock::duration>(0.01s));
                    co_await ddl.async_wait(asio::use_awaitable);

                    uint16 port = 0;
                    uint32 ip = 0;
                    char buffer[4096];
                    int iLen = SteamGameServer()->GetNextOutgoingPacket(buffer, sizeof(buffer), &ip, &port);
                    while (iLen > 0) {
                        udp::endpoint out_endpoint(asio::ip::address_v4(ip), port);

                        log("[Steam] ", " Send packet to ", out_endpoint, " size=", iLen);
                        co_await socket.async_send_to(asio::const_buffer(buffer, iLen), out_endpoint, asio::use_awaitable);

                        iLen = SteamGameServer()->GetNextOutgoingPacket(buffer, sizeof(buffer), &ip, &port);
                    }
                }
            }
            catch (const asio::system_error& e) { co_return; } // asio::error::operation_aborted
        }, asio::detached);

        // wait for server info
        while (!ServerInfoQueryResultCache.has_value())  {
            asio::system_timer ddl(ioc, std::chrono::duration_cast<std::chrono::system_clock::duration>(1s));
            co_await ddl.async_wait(asio::use_awaitable);
        }
        auto vecfinfo = ServerInfoQueryResultCache.value();
    	
        SteamGameServer()->SetProduct("cstrike");
        SteamGameServer()->SetModDir("cstrike");
        SteamGameServer()->SetDedicatedServer(true);
        SteamGameServer()->LogOnAnonymous();
        SteamGameServer()->EnableHeartbeats(true);
        SteamGameServer()->SetHeartbeatInterval((int)400);
        SteamGameServer()->ForceHeartbeat();

        SteamGameServer()->SetKeyValue("protocol", "48");

        if (player_num <= 0)
        {
            for (auto& info : vecfinfo)
            {
                player_num = std::max(player_num, info.PlayerCount);
            }
        }

        std::vector<CSteamID> fakeSteamIDs;
    	for(int i = 0; i < player_num; ++i)
    	{
            auto steamid = SteamGameServer()->CreateUnauthenticatedUserConnection();
            fakeSteamIDs.push_back(steamid);
            //SteamGameServer()->SendUserDisconnect(steamid);
    	}

        try {
            while (1) {

                asio::system_timer ddl(ioc, std::chrono::duration_cast<std::chrono::system_clock::duration>(0.1s));
                co_await ddl.async_wait(asio::use_awaitable);
                SteamGameServer_RunCallbacks();
            }
        }
        catch (const asio::system_error& e) { co_return; } // asio::error::operation_aborted
    	
        SteamGameServer()->EnableHeartbeats(false);
        SteamGameServer()->LogOff();
        SteamGameServer_Shutdown();
    }
	
    void UpdateSteamInfoConfig(const TSourceEngineQuery::ServerInfoQueryResult &info)
    {
        SteamGameServer()->SetMaxPlayerCount(info.MaxPlayers);
        SteamGameServer()->SetBotPlayerCount(0);
        SteamGameServer()->SetServerName(info.ServerName.c_str());
        SteamGameServer()->SetMapName(info.Map.c_str());
        SteamGameServer()->SetPasswordProtected(false);
        SteamGameServer()->SetGameDescription(info.Game.c_str());
    }
#endif
};

int main(int argc, char *argv[])
{
    auto spsv = std::span<char *>(argv, argc) | std::ranges::views::transform([](const char* arg) { return std::string_view(arg); });
    auto ports = GetPortsFromArgs(spsv);
    auto dest_port = 6666;
	auto server_names = GetMultiArgs("+hostname", spsv);
    auto map_names = GetMultiArgs("+map", spsv);
    int player_num = GetPlayerNum(spsv);
    asio::io_context ioc;
    Citrus app(ioc);
    app.CoSpawn(ports, dest_port, server_names, map_names, player_num);

    ioc.run();
    return 0;
}