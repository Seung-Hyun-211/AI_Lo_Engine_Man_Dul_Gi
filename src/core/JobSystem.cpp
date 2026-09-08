#include "core/JobSystem.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace engine::core
{
    void JobFence::Wait() const
    {
        if (!m_state) return;
        std::unique_lock lock(m_state->mutex);
        m_state->completed.wait(lock, [this] { return m_state->remaining.load(std::memory_order_acquire) == 0; });
    }

    bool JobFence::IsComplete() const
    {
        return !m_state || m_state->remaining.load(std::memory_order_acquire) == 0;
    }

    JobSystem::JobSystem(std::size_t workerCount)
    {
        workerCount = std::max<std::size_t>(workerCount, 1);
        m_workers.reserve(workerCount);
        for (std::size_t index = 0; index < workerCount; ++index)
        {
            m_workers.emplace_back(&JobSystem::WorkerLoop, this);
        }
    }

    JobSystem::~JobSystem()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_running = false;
        }
        m_workReady.notify_all();
        for (std::thread& worker : m_workers)
        {
            if (worker.joinable()) worker.join();
        }
    }

    JobFence JobSystem::Enqueue(std::function<void()> job)
    {
        auto state = std::make_shared<JobFence::State>();
        state->remaining.store(1, std::memory_order_release);
        Submit(std::move(job), state);
        return JobFence(std::move(state));
    }

    JobFence JobSystem::ParallelFor(std::size_t begin, std::size_t end, std::size_t grainSize,
                                     std::function<void(std::size_t, std::size_t)> body)
    {
        if (grainSize == 0) throw std::invalid_argument("ParallelFor grainSize must be non-zero");
        auto state = std::make_shared<JobFence::State>();
        const std::size_t jobCount = end <= begin ? 0 : (end - begin + grainSize - 1) / grainSize;
        state->remaining.store(jobCount, std::memory_order_release);
        for (std::size_t rangeBegin = begin; rangeBegin < end; rangeBegin += grainSize)
        {
            const std::size_t rangeEnd = std::min(rangeBegin + grainSize, end);
            Submit([body, rangeBegin, rangeEnd] { body(rangeBegin, rangeEnd); }, state);
        }
        return JobFence(std::move(state));
    }

    void JobSystem::Submit(std::function<void()> job, const std::shared_ptr<JobFence::State>& fence)
    {
        {
            std::scoped_lock lock(m_mutex);
            if (!m_running) throw std::runtime_error("Cannot submit a job after JobSystem shutdown");
            m_queue.push(QueuedJob{ std::move(job), fence });
        }
        m_workReady.notify_one();
    }

    void JobSystem::WorkerLoop()
    {
        for (;;)
        {
            QueuedJob job;
            {
                std::unique_lock lock(m_mutex);
                m_workReady.wait(lock, [this] { return !m_running || !m_queue.empty(); });
                if (!m_running && m_queue.empty()) return;
                job = std::move(m_queue.front());
                m_queue.pop();
            }

            job.run();
            if (job.fence->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::scoped_lock lock(job.fence->mutex);
                job.fence->completed.notify_all();
            }
        }
    }
}
