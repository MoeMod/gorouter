#pragma once
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

struct ServerData
{
	int idx;
	std::string ip;
	std::string game;
	std::string proxy;
};

std::vector<ServerData> BuildServerList() noexcept(false)
{
	ServerData data;
	std::vector<ServerData> Ret;

	using nlohmann::json;
	std::ifstream i("serverlist.json");

	if (!i.is_open()) throw std::exception("FATAL: Unable to open \"serverlist.json\"");

	json j;
	i >> j;

	for (json& j2 : j["servers"])
	{
		j2.at("id").get_to(data.idx);
		j2.at("ip").get_to(data.ip);
		j2.at("game").get_to(data.game);
		j2.at("proxy").get_to(data.proxy);
		Ret.push_back(data);
	}
	return Ret;
}