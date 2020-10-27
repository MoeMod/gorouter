#include <iostream>
#include <vector>

#include "ThreadPoolContext.h"
#include "ClientManager.h"

#include <boost/asio/spawn.hpp>

const auto read_port = 80;
const auto desc_host = "z4cs.com";
const auto desc_port = "6666";

bool IsValidInitialPacket(const char *buffer, std::size_t n)
{
    //if (n >= 23 && !strcmp(buffer, "\xFF\xFF\xFF\xFFgetchallenge steam\n"))
    //    return true;

    //return false;
    return true;
}

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
            for(const auto &ep : desc_endpoints)
            {
                desc_endpoint = ep;
                std::cout << "Resolved IP Address "  << desc_endpoint << std::endl;
            }

            udp::endpoint read_endpoint(boost::asio::ip::udp::v4(), read_port);
            udp::socket socket(*ioc, read_endpoint);

            ClientManager MyClientManager;
            char buffer[4096];
            int id = 0;
            while(true)
            {
                ++id;
                udp::endpoint sender_endpoint;
                std::size_t n = socket.async_receive_from(boost::asio::buffer(buffer), sender_endpoint, yield);

                if(auto cd = MyClientManager.GetClientData(sender_endpoint))
                {
                    cd->OnRecv(buffer, n);
                }
                else
                {
                    if(IsValidInitialPacket(buffer, n))
                    {
                        cd = MyClientManager.AcceptClient(ioc, sender_endpoint, desc_endpoint, [ioc, &socket, sender_endpoint](std::vector<char> vec, boost::asio::yield_context yield){
                            socket.async_send_to(boost::asio::buffer(vec), sender_endpoint, yield);
                        });
                        cd->OnRecv(buffer, n);
                    }
                    else
                    {
                        std::cout << "Drop package #" << id << " due to not beginning with -1." << std::endl;
                        continue;
                    }
                }
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