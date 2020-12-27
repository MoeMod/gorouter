#include <iostream>
#include <vector>

#include "ThreadPoolContext.h"
#include "ClientManager.h"

#include <boost/asio/spawn.hpp>

#include "TSourceEngineQuery.h"

const auto read_port = 6666;
const auto desc_host = "43.248.187.127";
const auto desc_port = "6666";

bool IsValidInitialPacket(const char *buffer, std::size_t n)
{
    return n >= 4 && !strncmp(buffer, "\xFF\xFF\xFF\xFF", 4);
}

bool IsTSourceEngineQueryPacket(const char* buffer, std::size_t n)
{
    return n >= 24 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "TSource Engine Query", 24);
}

bool IsPlayerListQueryPacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "U", 5);
}

bool IsPingPacket(const char* buffer, std::size_t n)
{
    return n >= 5 && !strncmp(buffer, "\xFF\xFF\xFF\xFF" "i", 5);
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

            std::shared_ptr<TSourceEngineQuery::ServerInfoQueryResult> atomicServerInfoQueryResult;
            std::shared_ptr<TSourceEngineQuery::PlayerListQueryResult> atomicPlayerListQueryResult;
        	
            boost::asio::system_timer query_timer(*ioc);
            using namespace std::chrono_literals;
            boost::asio::spawn(*ioc, [ioc, &query_timer, &desc_endpoint, &atomicServerInfoQueryResult, &atomicPlayerListQueryResult](boost::asio::yield_context yield) {
                try
                {
                    TSourceEngineQuery tseq(ioc);
                    int failed_times = 0;
                    while (true)
                    {
                    	try
                    	{
                            auto finfo = tseq.GetServerInfoDataAsync(desc_endpoint, 2s, yield);
                            auto new_spfinfo = std::make_shared<TSourceEngineQuery::ServerInfoQueryResult>(finfo);
                            std::atomic_store(&atomicServerInfoQueryResult, new_spfinfo);
                    		
                            auto fplayer = tseq.GetPlayerListDataAsync(desc_endpoint, 2s, yield);
                            auto new_spfplayer = std::make_shared<TSourceEngineQuery::PlayerListQueryResult>(fplayer);
                            std::atomic_store(&atomicPlayerListQueryResult, new_spfplayer);

                            std::cout << "Get TSourceEngineQuery success: " << finfo.Map << " " << finfo.PlayerCount << "/" << finfo.MaxPlayers << std::endl;
                    		
                            failed_times = 0;
                    	}
                        catch (const boost::system::system_error& e)
                        {
                            ++failed_times;
                            std::cout << "Get TSourceEngineQuery error with retry: " << e.what() << std::endl;
                            continue;
                        }
                        query_timer.expires_from_now(15s);
                        query_timer.async_wait(yield);
                    }
                }
                catch (const boost::system::system_error& e)
                {
                    if (e.code() == boost::system::errc::operation_canceled)
                        return;
                    std::cout << "Error: " << e.what() << std::endl;
                }
            });


            udp::endpoint read_endpoint(boost::asio::ip::udp::v4(), read_port);
            udp::socket socket(*ioc, read_endpoint);
            ClientManager MyClientManager;
            char buffer[4096];
            int id = 0;
            while(true)
            {
            	try
            	{
                    ++id;
                    udp::endpoint sender_endpoint;
                    std::size_t n = socket.async_receive_from(boost::asio::buffer(buffer), sender_endpoint, yield);

                    if (auto cd = MyClientManager.GetClientData(sender_endpoint))
                    {
                        cd->OnRecv(buffer, n, yield);
                    }
                    else
                    {
                        if (IsValidInitialPacket(buffer, n))
                        {
                        	if(IsTSourceEngineQueryPacket(buffer, n) && false)
                        	{
                                boost::asio::spawn([&atomicServerInfoQueryResult, &socket, sender_endpoint, ioc, id](boost::asio::yield_context yield){
                                    if (auto spfinfo = std::atomic_load(&atomicServerInfoQueryResult))
                                    {
                                        char send_buffer[4096];
                                        auto finfo = *spfinfo;
                                        std::size_t len = TSourceEngineQuery::WriteServerInfoQueryResultToBuffer(*spfinfo, send_buffer, sizeof(buffer));
                                        socket.async_wait(socket.wait_write, yield);
                                        std::size_t bytes_transferred = socket.async_send_to(boost::asio::const_buffer(send_buffer, len), sender_endpoint, yield);
                                        std::cout << "Reply package #" << id << " TSource Engine Query." << std::endl;
                                    }
                                });
                        	}
                            else if(IsPlayerListQueryPacket(buffer, n) && false)
                        	{

                                boost::asio::spawn([&atomicPlayerListQueryResult, &socket, sender_endpoint, ioc, id](boost::asio::yield_context yield) {
                                    if (auto spfplayer = std::atomic_load(&atomicPlayerListQueryResult))
                                    {
                                        char send_buffer[4096];
                                        std::size_t len = TSourceEngineQuery::WritePlayerListQueryResultToBuffer(*spfplayer, send_buffer, sizeof(buffer));
                                        socket.async_wait(socket.wait_write, yield);
                                        std::size_t bytes_transferred = socket.async_send_to(boost::asio::const_buffer(send_buffer, len), sender_endpoint, yield);
                                        std::cout << "Reply package #" << id << " A2S_PLAYERS." << std::endl;
                                    }
                                });
                        	}
                            else if(IsPingPacket(buffer, n) && false)
                            {
                                constexpr const char response[] = "\xFF\xFF\xFF\xFF" "j" "00000000000000";
                                std::size_t bytes_transferred = socket.async_send_to(boost::asio::buffer(response, sizeof(response)), sender_endpoint, yield);
                            }
                            else
                            {
                                cd = MyClientManager.AcceptClient(ioc, sender_endpoint, desc_endpoint, [ioc, &socket, sender_endpoint](const char* send_buffer, std::size_t len, boost::asio::yield_context yield) {

                                    socket.async_wait(socket.wait_write, yield);
                                    std::size_t bytes_transferred = socket.async_send_to(boost::asio::const_buffer(send_buffer, len), sender_endpoint, yield);
                                    });
                                cd->OnRecv(buffer, n, yield);
                            }
                        }
                        else
                        {
                            std::cout << "Drop package #" << id << " due to not beginning with -1." << std::endl;
                            continue;
                        }
                    }
            	}
                catch (const boost::system::system_error& e)
                {
                    if (e.code() == boost::asio::error::connection_reset) // 10054
                        continue;
                	
                    if (e.code() == boost::asio::error::connection_refused) // 10061
                        continue;

                    if (e.code() == boost::asio::error::connection_aborted) // 10053
                        continue;
                    
                    std::cout << "Error with retry: " << e.what() << std::endl;
                    continue;
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