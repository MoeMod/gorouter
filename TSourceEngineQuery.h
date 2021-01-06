#pragma once

#include <string>
#include <array>
#include <vector>
#include <optional>
#include <variant>

#include <boost/asio.hpp>

#include "dummy_return.hpp"
#include <boost/asio/awaitable.hpp>

class TSourceEngineQuery
{
public:
    enum class ServerType_e : uint8_t
    {
        dedicated = 'd', // for a dedicated server
        DEDICATED = 'D', // for a dedicated server, on previous GoldSrc
        listen = 'l', // for a non - dedicated server
        LISTEN = 'L', // for a non - dedicated server, on previous GoldSrc
        hltv = 'p', // for a SourceTV relay(proxy)
        HLTV = 'P', // for a SourceTV relay(proxy), on previous GoldSrc
    };

    enum class Environment_e : uint8_t
    {
        linux = 'l', // for Linux
        LINUX = 'L', // for Linux, on previous GoldSrc
        windows = 'w', // for Windows
        WINDOWS = 'W', // for Windows, on previous GoldSrc

        mac = 'm', // for Mac
        osx = 'o', // for Mac, the code changed after L4D1
    };

    friend constexpr const char* EnvironmentName(Environment_e env)
    {
        return  (env == Environment_e::linux || env == Environment_e::LINUX) ? "Linux" :
            (env == Environment_e::windows || env == Environment_e::WINDOWS) ? "Windows" :
            (env == Environment_e::mac || env == Environment_e::osx) ? "macOS" : "Unknown";
    }

    enum Visibility_e : uint8_t
    {
        Public = 0,
        Private = 1
    };

    struct ServerInfoQueryResult
    {
        std::chrono::time_point<std::chrono::steady_clock> GenerateTime;
        int32_t header1;  // header -1
        uint8_t  header2; // header ('I')
        std::optional<std::string> LocalAddress;
        uint8_t Protocol;
        std::string ServerName;
        std::string Map;
        std::string Folder;
        std::string Game;
        std::optional<int16_t> SteamID;
        int PlayerCount;
        int MaxPlayers;
        int BotCount;
        ServerType_e ServerType;
        Environment_e Environment;
        Visibility_e Visibility;

        std::optional<bool> Mod;
        struct ModData_s
        {
            std::string Link;
            std::string DownloadLink;
            uint8_t NULL_;
            int32_t Version;
            int32_t Size;
            enum class ModType_e
            {
                SingleAndMultiplayer = 0,
                MultiplayerOnly = 1,
            } Type;
            bool DLL;

        };
        std::optional<ModData_s> ModData;

        bool VAC;
        std::optional<std::string> GameVersion;

        std::optional<uint8_t> EDF;
        std::optional<int16_t> Port;
        std::optional<std::array<int32_t, 2>> SteamIDExtended;

        struct SourceTVData_s
        {
            int16_t SourceTVPort;
            std::string SourceTVName;
        };
        std::optional<SourceTVData_s> SourceTVData;
        std::optional<std::string> Keywords;
        std::optional<std::array<int32_t, 2>> GameID;
    };

    struct PlayerListQueryResult
    {
        std::chrono::time_point<std::chrono::steady_clock> GenerateTime;
        int32_t header1;  // header -1
        uint8_t  header2; // header ('A') for challenge or header ('D') for A2S_PLAYER

        struct PlayerInfo_s
        {
            uint8_t Index;
            std::string Name;
            int32_t Score;
            float Duration;
        };
        std::variant<int32_t, std::vector<PlayerInfo_s>> Results;
    };

    TSourceEngineQuery(boost::asio::io_context& ioc);
    ~TSourceEngineQuery();
    boost::asio::awaitable<std::vector<ServerInfoQueryResult>> GetServerInfoDataAsync(boost::asio::ip::udp::endpoint endpoint, std::chrono::seconds timeout);
    boost::asio::awaitable<PlayerListQueryResult> GetPlayerListDataAsync(boost::asio::ip::udp::endpoint endpoint, std::chrono::seconds timeout);

public:
    static ServerInfoQueryResult MakeServerInfoQueryResultFromBuffer(const char *reply, std::size_t reply_length);
    static PlayerListQueryResult MakePlayerListQueryResultFromBuffer(const char *reply, std::size_t reply_length);

    static std::size_t WriteServerInfoQueryResultToBuffer(const ServerInfoQueryResult &res, char* buffer, std::size_t max_len);
    static std::size_t WritePlayerListQueryResultToBuffer(const PlayerListQueryResult& res, char* buffer, std::size_t max_len);

private:
    boost::asio::io_context &ioc;
};