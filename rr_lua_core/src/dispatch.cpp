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
            ++generation_;
            cv_.notify_all();
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
        ++generation_;
        dropped.swap(tasks_);
        cv_.notify_all();
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

void DispatchQueue::note_refused()
{
    boost::mutex::scoped_lock lock(m_);
    ++refused_total_;
}

std::size_t DispatchQueue::refused_total() const
{
    boost::mutex::scoped_lock lock(m_);
    return refused_total_;
}

std::uint64_t DispatchQueue::generation() const
{
    boost::mutex::scoped_lock lock(m_);
    return generation_;
}

void DispatchQueue::wake()
{
    boost::mutex::scoped_lock lock(m_);
    ++generation_;
    cv_.notify_all();
}

void DispatchQueue::wait(std::uint64_t gen, boost::chrono::milliseconds timeout)
{
    boost::mutex::scoped_lock lock(m_);
    cv_.wait_for(lock, timeout, [&] { return closed_ || generation_ != gen || !tasks_.empty(); });
}

} // namespace rl
