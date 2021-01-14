#pragma once

#include <iostream>
#include <sstream>
#include <ctime>

template<class...Args>
void log(Args &&...args)
{
    std::ostringstream oss;
    std::time_t now = std::time(nullptr);
    oss << std::ctime(&now); // thread-unsafe
    (oss << ... << args);
    puts(oss.str().c_str());
}