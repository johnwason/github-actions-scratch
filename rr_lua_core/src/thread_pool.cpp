// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/thread_pool.hpp"

#include <boost/chrono.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <stdexcept>

namespace rl
{

ThreadPool::ThreadPool(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node)
    : RobotRaconteur::ThreadPool(node)
{}

void ThreadPool::Shutdown()
{
    RobotRaconteur::ThreadPool::Shutdown(); // stop, join every pool thread

    // No pool thread is left, and Post/TryPost refuse new work (keepgoing is false). Run
    // what is still queued so the handlers and everything they hold are released. A
    // handler can make more handlers ready (for example by cancelling a timer), so poll
    // until a pass runs nothing; the bound keeps a misbehaving handler from looping forever.
    _io_context.restart();
    for (int pass = 0; pass < 1000; ++pass)
    {
        std::size_t n = 0;
        try
        {
            n = _io_context.poll();
        }
        catch (...)
        {
            // A handler threw past RR's own wrapper; keep draining the rest.
            n = 1;
        }
        if (n == 0)
            break;
    }
}

RR_SHARED_PTR<RobotRaconteur::ThreadPool> ThreadPoolFactory::NewThreadPool(
    const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node)
{
    return RR_MAKE_SHARED<ThreadPool>(node);
}

namespace
{
struct Barrier
{
    boost::mutex m;
    boost::condition_variable cv;
    std::size_t arrived = 0;
    bool released = false;
};

void barrier_task(const RR_SHARED_PTR<Barrier>& b)
{
    boost::mutex::scoped_lock lock(b->m);
    ++b->arrived;
    b->cv.notify_all();
    while (!b->released)
        b->cv.wait(lock);
}
} // namespace

void init_node(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node, int timeout_ms)
{
    node->SetThreadPoolFactory(RR_MAKE_SHARED<ThreadPoolFactory>());
    node->Init();
    RR_SHARED_PTR<RobotRaconteur::ThreadPool> pool = node->GetThreadPool(); // posts the cleanup task

    // One barrier task per pool thread. Handlers run in FIFO order, so every thread can only
    // be blocked here once everything posted before (the cleanup task included) has finished.
    std::size_t n = pool->GetThreadPoolCount();
    RR_SHARED_PTR<Barrier> b = RR_MAKE_SHARED<Barrier>();
    for (std::size_t i = 0; i < n; ++i)
        pool->Post(boost::bind(&barrier_task, b));

    bool ok;
    {
        boost::mutex::scoped_lock lock(b->m);
        boost::chrono::steady_clock::time_point deadline =
            boost::chrono::steady_clock::now() + boost::chrono::milliseconds(timeout_ms);
        while (b->arrived < n)
            if (b->cv.wait_until(lock, deadline) == boost::cv_status::timeout)
                break;
        ok = b->arrived >= n;
        b->released = true;
        b->cv.notify_all();
    }
    if (!ok)
        throw std::runtime_error("robotraconteur: node thread pool did not start in time");
}

} // namespace rl
