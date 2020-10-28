#pragma once
#include <string>
#include <functional>
#include <fstream>
#include <iterator>
#include <json/json.h> // Using jsoncpp.

// Json file structure template.
/*
{
  "name": "ServerListReader",
  "servers": [
	{
	  "id": 1,
	  "ip": "wtf.org:27015",
	  "game": "csgo",
	  "proxy": "None"
	},
	{
	  "id": 2,
	  "ip": "wtf.org:27016",
	  "game": "cs1.6",
	  "proxy": "47"
	}
  ]
}
*/

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
	std::vector<ServerData> _ret;

	std::ifstream i("serverlist.json", std::ios::binary);

	if (!i.is_open()) throw std::exception("Unable to open \"serverlist.json\", aborting.");

	std::string result;

	std::copy(std::istream_iterator<unsigned char>(i), std::istream_iterator<unsigned char>(), std::back_inserter(result));

	Json::CharReaderBuilder builder;
	Json::Value root, servers;
	JSONCPP_STRING errs;
	std::unique_ptr<Json::CharReader> const jsonReader(builder.newCharReader());

	bool success = jsonReader->parse(result.c_str(), result.c_str() + result.length(), &root, &errs);
	if (!success || !errs.empty()) std::cout << "parse Json err: " << errs << std::endl;

	std::cout << "File name: " << root["name"].asString() << "\n" << std::endl;

	servers = root["servers"];
	for (std::size_t i = 0; i < servers.size(); i++)
	{
		data.idx = servers[i]["id"].asInt();
		data.ip = servers[i]["ip"].asString();
		data.game = servers[i]["game"].asString();
		data.proxy = servers[i]["proxy"].asString();
		_ret.push_back(data);
	}

	return _ret;
}