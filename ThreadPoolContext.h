#pragma once

#include <memory>
#include <thread>
#include <asio.hpp>

class ThreadPoolContext : public std::enable_shared_from_this<ThreadPoolContext> {
    asio::io_context io_context;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard;
    std::vector<std::thread> thread_pool;

public:
    ThreadPoolContext() : work_guard(make_work_guard(io_context))
    {
        // dont start here when shared_from_this() is not ready
    }

    ~ThreadPoolContext()
    {
        stop();
    }

    std::shared_ptr<ThreadPoolContext> start(int thread_num = std::max<int>(std::thread::hardware_concurrency() + 1, 2))
    {
        assert(thread_num >= 1);
        std::generate_n(std::back_inserter(thread_pool), thread_num, std::bind(&ThreadPoolContext::make_thread, this));
        return shared_from_this();
    }

    void stop()
    {
        io_context.stop();
        std::for_each(thread_pool.begin(), thread_pool.end(), std::mem_fn(&std::thread::detach));
        thread_pool.clear();
    }

	void join()
    {
        auto &ioc = io_context;
        ioc.run();
    }

    std::thread make_thread()
    {
        return std::thread([&ioc = io_context] {
	        ioc.run();
            return void();
        });
    }
	
    std::shared_ptr<asio::io_context> as_io_context()
    {
        auto sp = shared_from_this();
        return std::shared_ptr<asio::io_context>(sp, &sp->io_context);
    }
	
    static std::shared_ptr<ThreadPoolContext> create()
    {
        auto sp = std::make_shared<ThreadPoolContext>();
        return sp;
    }
};
