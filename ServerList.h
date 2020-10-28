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

class ServerData
{
	ServerData() {};
	~ServerData() {};
private:
	std::size_t id;
	std::string ip;
	std::string game;
	std::string proxy;
};

// Only using for template.
bool BuildServerList()
{
	// opening the file
	std::ifstream i("serverlist.json", std::ios::binary);

	if (!i.is_open())
	{
		throw std::exception("why cannot open the json file?");
		return false;
	}
	std::string result;

	std::copy(std::istream_iterator<unsigned char>(i), std::istream_iterator<unsigned char>(), std::back_inserter(result));

	Json::CharReaderBuilder builder;
	Json::Value root, servers;
	JSONCPP_STRING errs;
	std::unique_ptr<Json::CharReader> const jsonReader(builder.newCharReader());

	bool success = jsonReader->parse(result.c_str(), result.c_str() + result.length(), &root, &errs);
	if (!success || !errs.empty()) 
	{
		std::cout << "parse Json err: " << errs << std::endl;
		return false;
	}

	std::cout << "File name: " << root["name"].asString() << "\n" << std::endl;

	servers = root["servers"];

	for (std::size_t i = 0; i < servers.size(); i++)
	{
		std::cout << servers[i]["id"].asInt() << "\n" << std::endl;
		std::cout << servers[i]["ip"].asString() << "\n" << std::endl;
		std::cout << servers[i]["game"].asString() << "\n" << std::endl;
		std::cout << servers[i]["proxy"].asString() << "\n" << std::endl;
	}
	return true;
}