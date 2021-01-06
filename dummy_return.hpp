#pragma once

namespace boost::asio
{
    template <typename T>
    T dummy_return();

    template <>
    inline void dummy_return();
}
