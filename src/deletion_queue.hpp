#pragma once

#include <functional>
#include <vector>

class DeletionQueue
{
    std::vector<std::function<void()>> m_queue;

  public:
    void add(std::function<void()> f)
    {
        m_queue.emplace_back(f);
    }

    void delete_all()
    {
        for (auto it = m_queue.rbegin(); it != m_queue.rend(); ++it)
        {
            (*it)();
        }
        m_queue.clear();
    }
};
