#pragma once

#include <vector>
#include <string>
#include <ranges>

template<std::ranges::input_range ArgsRange>
std::vector<unsigned int> GetPortsFromArgs(ArgsRange spsv)
{
    std::vector<unsigned int> res;
    bool port = false;
    bool ports = false;
    bool portsnum = false;
    for (std::string_view sv : spsv)
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

template<std::ranges::input_range ArgsRange, std::ranges::input_range PortsRange>
unsigned int GetDestPortServerFromArgs(ArgsRange spsv, PortsRange from_ports)
{
    bool destport = false;
    for (std::string_view sv : spsv)
    {
        try
        {
            if (std::exchange(destport, false))
            {
                return std::stoi(std::string(sv));
            }
        }
        catch (const std::exception& e)
        {
            log("[GetDestPortServerFromArgs] error: ", e.what());
        }
        if (sv == "-destport")
            destport = true;
    }
    if (std::ranges::distance(from_ports) > 0)
    {
        if(auto port = *std::ranges::begin(from_ports))
			return port;
    }
    return 27015;
}

template<std::ranges::input_range ArgsRange>
std::vector<std::string> GetServerNames(ArgsRange spsv)
{
    std::vector<std::string> res;
    bool parse = false;
    for (std::string_view sv : spsv)
    {
        try
        {
            if (std::exchange(parse, false))
            {
                res.push_back(std::string(sv));
            }
        }
        catch (const std::exception& e)
        {
            log("[GetServerNames] error: ", e.what());
        }
        if (sv == "+hostname")
            parse = true;
    }
    return res;
}

template<std::ranges::input_range ArgsRange>
std::vector<std::string> GetMapNames(ArgsRange spsv)
{
    std::vector<std::string> res;
    bool parse = false;
    for (std::string_view sv : spsv)
    {
        try
        {
            if (std::exchange(parse, false))
            {
                res.push_back(std::string(sv));
            }
        }
        catch (const std::exception& e)
        {
            log("[GetMapNames] error: ", e.what());
        }
        if (sv == "+map")
            parse = true;
    }
    return res;
}


template<std::ranges::input_range ArgsRange>
std::vector<std::string> GetMultiArgs(std::string_view arg, ArgsRange spsv)
{
    std::vector<std::string> res;
    bool parse = false;
    for (std::string_view sv : spsv)
    {
        try
        {
            if (std::exchange(parse, false))
            {
                res.push_back(std::string(sv));
            }
        }
        catch (const std::exception& e)
        {
            log("[GetMapNames] error: ", e.what());
        }
        if (sv == arg)
            parse = true;
    }
    return res;
	
}

template<std::ranges::input_range ArgsRange>
int GetPlayerNum(ArgsRange spsv)
{
    bool maxplayers = false;
    for (std::string_view sv : spsv)
    {
        try
        {
            if (std::exchange(maxplayers, false))
            {
                return std::stoi(std::string(sv));
            }
        }
        catch (const std::exception& e)
        {
            log("[GetPlayerNum] error: ", e.what());
        }
        if (sv == "+maxplayers")
            maxplayers = true;
    }
    return -1;
}