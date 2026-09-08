#include "core/JobSystem.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace engine::core
{
    std::size_t RecommendedWorkerCount()
    {
        const unsigned int logicalProcessors = std::thread::hardware_concurrency();
        const std::size_t reserved = logicalProcessors > 2 ? logicalProcessors - 2 : 1;
        return std::clamp<std::size_t>(reserved, 1, 14);
    }

    void JobFence::Wait() const
    {
        if (!m_state) return;
        {
            std::unique_lock lock(m_state->mutex);
            m_state->completed.wait(lock, [this] { return m_state->remaining.load(std::memory_order_acquire) == 0; });
        }
        // Re-raise a job failure on the frame thread that waited for it, so a
        // throwing job surfaces as a normal exception instead of a deadlock.
        std::exception_ptr error;
        {
            std::scoped_lock lock(m_state->errorMutex);
            error = m_state->error;
        }
        if (error) std::rethrow_exception(error);
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

            // A job that throws must never skip the fence decrement below, or the
            // frame thread waits forever. Capture the first failure; JobFence::Wait
            // rethrows it once the batch completes.
            try
            {
                job.run();
            }
            catch (...)
            {
                std::scoped_lock lock(job.fence->errorMutex);
                if (!job.fence->error) job.fence->error = std::current_exception();
            }

            if (job.fence->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::scoped_lock lock(job.fence->mutex);
                job.fence->completed.notify_all();
            }
        }
    }
}
