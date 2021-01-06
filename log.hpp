#pragma once

#include <iostream>

template<class...Args>
void log(Args &&...args)
{
    std::ostringstream oss;
    (oss << ... << args);
    puts(oss.str().c_str());
}