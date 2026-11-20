// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/dispatch.hpp"

namespace rl
{

bool DispatchQueue::post(std::unique_ptr<Task> task)
{
    {
        boost::mutex::scoped_lock lock(m_);
        if (!closed_)
        {
            tasks_.push_back(std::move(task));
            ++posted_total_;
            return true;
        }
    }
    return false; // task destroyed here, outside the lock
}

std::unique_ptr<Task> DispatchQueue::pop()
{
    boost::mutex::scoped_lock lock(m_);
    if (tasks_.empty())
        return nullptr;
    std::unique_ptr<Task> t = std::move(tasks_.front());
    tasks_.pop_front();
    return t;
}

void DispatchQueue::close()
{
    std::deque<std::unique_ptr<Task> > dropped;
    {
        boost::mutex::scoped_lock lock(m_);
        closed_ = true;
        dropped.swap(tasks_);
    }
    // dropped tasks are destroyed here, outside the lock
}

bool DispatchQueue::closed() const
{
    boost::mutex::scoped_lock lock(m_);
    return closed_;
}

std::size_t DispatchQueue::size() const
{
    boost::mutex::scoped_lock lock(m_);
    return tasks_.size();
}

std::size_t DispatchQueue::posted_total() const
{
    boost::mutex::scoped_lock lock(m_);
    return posted_total_;
}

} // namespace rl
