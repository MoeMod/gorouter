#include <iostream>
#include <memory>
#include <boost/asio.hpp>

#include "TSourceEngineQuery.h"
#include "parsemsg.h"
#include "net_buffer.h"

using namespace std::chrono_literals;
using boost::asio::ip::udp;

TSourceEngineQuery::TSourceEngineQuery(std::shared_ptr<boost::asio::io_context> ioc) : ioc(ioc)
{

}

TSourceEngineQuery::~TSourceEngineQuery()
{

}

std::string UTF8_To_ANSI(const std::string &str)
{
    return str;
}

std::string ANSI_To_UTF8(const std::string& str)
{
    return str;
}

auto TSourceEngineQuery::MakeServerInfoQueryResultFromBuffer(const char *reply, std::size_t reply_length) -> ServerInfoQueryResult
{
    BufferReader buf(reply, reply_length);
    ServerInfoQueryResult result{};
    result.GenerateTime = std::chrono::steady_clock::now();
    result.header1 = buf.ReadLong(); // header -1
    result.header2 = buf.ReadByte(); // header ('I')

    if (result.header1 != -1)
        throw std::runtime_error("unsupported protocol format");

    if (result.header2 == 'I')
    {
        // Steam版
        result.Protocol = buf.ReadByte();
        result.ServerName = UTF8_To_ANSI(buf.ReadString());
        result.Map = UTF8_To_ANSI(buf.ReadString());
        result.Folder = UTF8_To_ANSI(buf.ReadString());
        result.Game = UTF8_To_ANSI(buf.ReadString());
        result.SteamID = buf.ReadShort();
        result.PlayerCount = buf.ReadByte();
        result.MaxPlayers = buf.ReadByte();
        result.BotCount = buf.ReadByte();
        result.ServerType = static_cast<ServerType_e>(buf.ReadByte());
        result.Environment = static_cast<Environment_e>(buf.ReadByte());
        result.Visibility = static_cast<Visibility_e>(buf.ReadByte());
        result.VAC = buf.ReadByte();
        result.GameVersion = UTF8_To_ANSI(buf.ReadString());

        int EDF = buf.ReadByte();
        if (EDF & 0x80)
            result.Port = buf.ReadShort();
        if (EDF & 0x10)
            result.SteamIDExtended = { buf.ReadLong(), buf.ReadLong() };
        if (EDF & 0x40)
            result.SourceTVData = { buf.ReadShort(), UTF8_To_ANSI(buf.ReadString()) };
        if (EDF & 0x20)
            result.Keywords = UTF8_To_ANSI(buf.ReadString());
        if (EDF & 0x01)
            result.GameID = { buf.ReadLong(), buf.ReadLong() };
    }
    else if (result.header2 == 'm')
    {
        // 非Steam版
        result.LocalAddress = UTF8_To_ANSI(buf.ReadString());
        result.ServerName = UTF8_To_ANSI(buf.ReadString());
        result.Map = UTF8_To_ANSI(buf.ReadString());
        result.Folder = UTF8_To_ANSI(buf.ReadString());
        result.Game = UTF8_To_ANSI(buf.ReadString());
        result.PlayerCount = buf.ReadByte();
        result.MaxPlayers = buf.ReadByte();
        result.Protocol = buf.ReadByte();
        result.ServerType = static_cast<ServerType_e>(buf.ReadByte());
        result.Environment = static_cast<Environment_e>(buf.ReadByte());
        result.Visibility = static_cast<Visibility_e>(buf.ReadByte());

        if ((result.Mod = buf.ReadByte()) == true)
        {
            result.ModData = {
                    UTF8_To_ANSI(buf.ReadString()),
                    UTF8_To_ANSI(buf.ReadString()),
                    buf.ReadByte(),
                    buf.ReadLong(),
                    buf.ReadLong(),
                    static_cast<ServerInfoQueryResult::ModData_s::ModType_e>(buf.ReadByte()),
                    static_cast<bool>(buf.ReadByte())
            };
        }

        result.VAC = buf.ReadByte();
        result.BotCount = buf.ReadByte();
    }
    else
    {
        throw std::runtime_error("unsupported protocol format");
    }
    return result;
}

std::size_t TSourceEngineQuery::WriteServerInfoQueryResultToBuffer(const ServerInfoQueryResult& result, char* buffer, std::size_t max_len)
{
    sizebuf_t buf;
    MSG_Init(&buf, "TSourceEngineQuery", buffer, max_len);

    MSG_WriteLong(&buf, (result.header1)); // header -1
    MSG_WriteByte(&buf, (result.header2)); // header ('I')
    if (result.header2 == 'I')
    {
        // Steam版
        MSG_WriteByte(&buf, (result.Protocol));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.ServerName).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.Map).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.Folder).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.Game).c_str()));
        MSG_WriteShort(&buf, (result.SteamID.value_or(0)));
        MSG_WriteByte(&buf, (result.PlayerCount));
        MSG_WriteByte(&buf, (result.MaxPlayers));
        MSG_WriteByte(&buf, (result.BotCount));
        MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.ServerType)));
        MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.Environment)));
        MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.Visibility)));
        MSG_WriteByte(&buf, (result.VAC));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.GameVersion.value_or("")).c_str()));

        int EDF = 0;
        EDF |= result.Port.has_value() ? 0x80 : 0;
        EDF |= result.SteamIDExtended.has_value() ? 0x10 : 0;
        EDF |= result.SourceTVData.has_value() ? 0x40 : 0;
        EDF |= result.Keywords.has_value() ? 0x20 : 0;
        EDF |= result.GameID.has_value() ? 0x10 : 0;
        MSG_WriteByte(&buf, (EDF));
        result.Port.has_value() ? MSG_WriteShort(&buf, (result.Port.value())) : void();
        result.SteamIDExtended.has_value() ? (MSG_WriteLong(&buf, (result.SteamIDExtended.value()[0])), MSG_WriteLong(&buf, (result.SteamIDExtended.value()[1]))) : void();
        result.SourceTVData.has_value() ? (MSG_WriteShort(&buf, (result.SourceTVData->SourceTVPort)), MSG_WriteString(&buf, (ANSI_To_UTF8(result.SourceTVData->SourceTVName).c_str()))) : void();
        result.Keywords.has_value() ? MSG_WriteString(&buf, (result.Keywords->c_str())) : void();
        result.GameID.has_value() ? (MSG_WriteLong(&buf, (result.GameID.value()[0])), MSG_WriteLong(&buf, (result.GameID.value()[1]))) : void();
    }
    else if (result.header2 == 'm')
    {
        // 非Steam版
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.LocalAddress.value()).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.ServerName).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.Map).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.Folder).c_str()));
        MSG_WriteString(&buf, (ANSI_To_UTF8(result.Game).c_str()));
        MSG_WriteByte(&buf, (result.PlayerCount));
        MSG_WriteByte(&buf, (result.MaxPlayers));
        MSG_WriteByte(&buf, (result.Protocol));
        MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.ServerType)));
        MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.Environment)));
        MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.Visibility)));

        MSG_WriteByte(&buf, (result.Mod.value_or(false)));
        if (result.Mod)
        {
            MSG_WriteString(&buf, (ANSI_To_UTF8(result.ModData->Link).c_str()));
            MSG_WriteString(&buf, (ANSI_To_UTF8(result.ModData->DownloadLink).c_str()));
            MSG_WriteByte(&buf, (result.ModData->NULL_));
            MSG_WriteLong(&buf, (result.ModData->Version));
            MSG_WriteLong(&buf, (result.ModData->Size));
            MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.ModData->Type)));
            MSG_WriteByte(&buf, (static_cast<std::uint8_t>(result.ModData->DLL)));
        }

        MSG_WriteByte(&buf, (result.VAC));
        MSG_WriteByte(&buf, (result.BotCount));
    }
    else
    {
        throw std::runtime_error("unsupported protocol format");
    }
    return MSG_GetNumBytesWritten(&buf);
}

auto TSourceEngineQuery::MakePlayerListQueryResultFromBuffer(const char *reply, std::size_t reply_length) -> PlayerListQueryResult
{
    PlayerListQueryResult result;
    BufferReader buf(reply, reply_length);
    result.GenerateTime = std::chrono::steady_clock::now();
    result.header1 = buf.ReadLong();

    if (result.header1 != -1)
        throw std::runtime_error("unsupported protocol format");

    result.header2 = buf.ReadByte();
    if (result.header2 == 'A')
    {
        result.Results.emplace<0>(buf.ReadLong());
    }
    else if (result.header2 == 'D')
    {
        volatile auto Players = buf.ReadByte();
        std::vector<PlayerListQueryResult::PlayerInfo_s> infos;
        while (!buf.Eof())
        {
            auto Index = buf.ReadByte();
            auto Name = UTF8_To_ANSI(buf.ReadString());
            auto Score = buf.ReadLong();
            float Duration = buf.ReadFloat();
            infos.push_back({ Index, std::move(Name), Score, Duration });
        }
        result.Results.emplace<1>(std::move(infos));
    }
    else
    {
        throw std::runtime_error("unsupported protocol format");
    }
    return result;
}

std::size_t TSourceEngineQuery::WritePlayerListQueryResultToBuffer(const PlayerListQueryResult& result, char* buffer, std::size_t max_len)
{
    sizebuf_t buf;
    MSG_Init(&buf, "TSourceEngineQuery", buffer, max_len);
	
    MSG_WriteLong(&buf, (result.header1));
    MSG_WriteLong(&buf, (result.header2));
    if (result.header2 == 'A')
    {
        MSG_WriteLong(&buf, (std::get<0>(result.Results)));
    }
    else if (result.header2 == 'D')
    {
        const std::vector<PlayerListQueryResult::PlayerInfo_s> &infos = std::get<1>(result.Results);
    	for(const PlayerListQueryResult::PlayerInfo_s &info : infos)
    	{
            MSG_WriteByte(&buf, (info.Index));
            MSG_WriteString(&buf, (ANSI_To_UTF8(info.Name).c_str()));
            MSG_WriteLong(&buf, (info.Score));
            MSG_WriteFloat(&buf, info.Duration);
    	}
    }
    else
    {
        throw std::runtime_error("unsupported protocol format");
    }
    return MSG_GetNumBytesWritten(&buf);
}

inline std::shared_ptr<boost::asio::system_timer> TimeoutCloseSocket(std::shared_ptr<boost::asio::io_context> ioc, std::weak_ptr<udp::socket> wsocket, std::chrono::seconds timeout)
{
	std::shared_ptr<boost::asio::system_timer> ddl = std::make_shared<boost::asio::system_timer>(*ioc);
    ddl->expires_from_now(timeout);
    boost::asio::spawn([ioc, wsocket, timeout, ddl](boost::asio::yield_context yield) {
    	try
    	{
            ddl->async_wait(yield);
            if (auto socket = wsocket.lock())
                socket->close();
    	}
    	catch(const boost::system::system_error &e)
    	{
    		// boost::asio::error::operation_aborted
    		// ...
    	}
    });
    return ddl;
}

// Reference: https://developer.valvesoftware.com/wiki/Server_queries#A2S_INFO
auto TSourceEngineQuery::GetServerInfoDataAsync(boost::asio::ip::udp::endpoint endpoint, std::chrono::seconds timeout, boost::asio::yield_context yield) -> ServerInfoQueryResult
{
    std::shared_ptr<udp::socket> socket = std::make_shared<udp::socket>(*ioc, udp::endpoint(udp::v4(), 0));
    auto ddl = TimeoutCloseSocket(ioc, socket, timeout);
	
    static constexpr char request1[] = "\xFF\xFF\xFF\xFF" "TSource Engine Query"; // Source / GoldSrc Steam
    //static constexpr char request2[] = "\xFF\xFF\xFF\xFF" "details"; // GoldSrc WON
    //static constexpr char request3[] = "\xFF\xFF\xFF\xFF" "info"; // Xash3D

    std::size_t bytes_transferred = socket->async_send_to(boost::asio::buffer(request1, sizeof(request1)), endpoint, yield);
    char buffer[4096];
    udp::endpoint sender_endpoint(udp::v4(), 0);
    std::size_t reply_length = socket->async_receive_from(boost::asio::buffer(buffer, 4096), sender_endpoint, yield);

    ddl->cancel();
    return MakeServerInfoQueryResultFromBuffer(buffer, reply_length);
}

auto TSourceEngineQuery::GetPlayerListDataAsync(boost::asio::ip::udp::endpoint endpoint, std::chrono::seconds timeout, boost::asio::yield_context yield) -> PlayerListQueryResult
{
    std::shared_ptr<udp::socket> socket = std::make_shared<udp::socket>(*ioc, udp::endpoint(udp::v4(), 0));
    auto ddl = TimeoutCloseSocket(ioc, socket, timeout);

    // first attempt
    constexpr char request_challenge[] = "\xFF\xFF\xFF\xFF" "U" "\xFF\xFF\xFF\xFF";
    std::size_t bytes_transferred = socket->async_send_to(boost::asio::buffer(request_challenge, sizeof(request_challenge)), endpoint, yield);
    char buffer[4096];
    udp::endpoint sender_endpoint(udp::v4(), 0);
    std::size_t reply_length = socket->async_receive_from(boost::asio::buffer(buffer, 4096), sender_endpoint, yield);

    PlayerListQueryResult first_result = MakePlayerListQueryResultFromBuffer(buffer, reply_length);
    if (first_result.Results.index() == 1)
        return first_result;

    // second attempt
    const int32_t challenge = std::get<int32_t>(first_result.Results);
    const char(&accessor)[4] = reinterpret_cast<const char(&)[4]>(challenge);
    char request3[10] = { '\xFF', '\xFF', '\xFF', '\xFF', 'U', accessor[0], accessor[1], accessor[2], accessor[3], '\0' };
    bytes_transferred = socket->async_send_to(boost::asio::buffer(request3, sizeof(request3)), endpoint, yield);
    reply_length = socket->async_receive_from(boost::asio::buffer(buffer, 4096), sender_endpoint, yield);

    ddl->cancel();
    return MakePlayerListQueryResultFromBuffer(buffer, reply_length);
}