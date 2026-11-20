// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// A node created by this binding (rl::init_node) must be destroyed once it is shut down and
// released, even when handlers holding it are still queued in its thread pool at shutdown,
// and even when shutdown comes right after init. RR core leaks the node in both cases (see
// rrlua/thread_pool.hpp); the binding must not.
//
// usage: node_release [stock]   ("stock" uses RR's ThreadPool, for a mutation check: it is
//                                expected to fail)

#include "rrlua/thread_pool.hpp"

#include <RobotRaconteur.h>

#include <boost/chrono.hpp>
#include <boost/thread.hpp>

#include <cstdio>
#include <cstring>

using namespace RobotRaconteur;

namespace
{

void slow_handler(RR_SHARED_PTR<RobotRaconteurNode> /*held*/)
{
    boost::this_thread::sleep_for(boost::chrono::milliseconds(2));
}

bool cycle(bool stock)
{
    RR_WEAK_PTR<RobotRaconteurNode> weak;
    {
        RR_SHARED_PTR<RobotRaconteurNode> node = RR_MAKE_SHARED<RobotRaconteurNode>();
        if (stock)
            node->Init();
        else
            rl::init_node(node);
        node->SetThreadPoolCount(2);
        RR_SHARED_PTR<IntraTransport> intra = RR_MAKE_SHARED<IntraTransport>(node);
        intra->StartServer();
        node->RegisterTransport(intra);

        // Far more slow handlers than 2 threads can run before Shutdown, each holding the
        // node the way RR's own posted tasks do.
        RR_SHARED_PTR<ThreadPool> pool = node->GetThreadPool();
        for (int i = 0; i < 200; ++i)
            pool->Post(boost::bind(&slow_handler, node));
        pool.reset();

        weak = node;
        node->Shutdown();
    }
    return weak.expired();
}

} // namespace

int main(int argc, char** argv)
{
    bool stock = argc > 1 && std::strcmp(argv[1], "stock") == 0;
    const int cycles = 30;
    int leaked = 0;
    for (int i = 0; i < cycles; ++i)
        if (!cycle(stock))
            ++leaked;
    std::printf("node_release (%s pool): %d of %d nodes not released\n",
                stock ? "stock RR" : "binding", leaked, cycles);
    return leaked == 0 ? 0 : 1;
}
