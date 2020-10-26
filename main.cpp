#include <iostream>

#include "ThreadPoolContext.h"

#include <boost/asio/spawn.hpp>

const auto read_port = 6666;
const auto desc_host = "z4cs.com";
const auto desc_port = "6666";

int main()
{
    auto thread_pool = ThreadPoolContext::create();
    auto ioc = thread_pool->as_io_context();

    using namespace boost::asio::ip;

    std::cout << "main() : start" << std::endl;

    std::condition_variable exit_cv;

    boost::asio::spawn(*ioc, [ioc, &exit_cv](boost::asio::yield_context yield) {
        try
        {
            std::cout << "coroutine : start" << std::endl;

            udp::resolver resolver(*ioc);

            udp::endpoint desc_endpoint;
            auto desc_endpoints = resolver.async_resolve(udp::v4(), desc_host, desc_port, yield);
            for(const udp::endpoint &ep : desc_endpoints)
            {
                std::cout << "Resolved IP Address "  << ep.address().to_string() << ":" << ep.port() << std::endl;
                desc_endpoint = ep;
            }

            udp::endpoint read_endpoint(boost::asio::ip::udp::v4(), read_port);
            udp::socket socket(*ioc, read_endpoint);

            char buffer[4096];
            int id = 0;
            while(1)
            {
                ++id;
                udp::endpoint sender_endpoint;
                std::size_t n = socket.async_receive_from(boost::asio::buffer(buffer), sender_endpoint, yield);

                giauto ep = sender_endpoint;
                std::cout << "Read packet #" << id << " from "  << ep.address().to_string() << ":" << ep.port() << ", size = " << n << std::endl;

                socket.async_send_to(boost::asio::buffer(buffer, n), desc_endpoint, yield);
                ep = desc_endpoint;
                std::cout << "Send packet #" << id << " to "  << ep.address().to_string() << ":" << ep.port() << ", size = " << n << std::endl;
            }
        }
        catch (std::exception& e)
        {
            std::cout << "Error: " << e.what() << std::endl;
            exit_cv.notify_all();
        }
    });

    std::mutex mtx;
    std::unique_lock ul(mtx);

    thread_pool->start();
    exit_cv.wait(ul);

    return 0;
}