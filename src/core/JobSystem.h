#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace engine::core
{
    class JobFence
    {
    public:
        JobFence() = default;
        void Wait() const;
        bool IsComplete() const;

    private:
        struct State
        {
            std::atomic_size_t remaining{};
            std::mutex mutex;
            std::condition_variable completed;
        };

        explicit JobFence(std::shared_ptr<State> state) : m_state(std::move(state)) {}
        std::shared_ptr<State> m_state;
        friend class JobSystem;
    };

    // Initial implementation: one guarded queue and fixed worker threads.
    // Work stealing is intentionally deferred until contention is measured.
    class JobSystem final
    {
    public:
        explicit JobSystem(std::size_t workerCount);
        ~JobSystem();
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        JobFence Enqueue(std::function<void()> job);
        JobFence ParallelFor(std::size_t begin, std::size_t end, std::size_t grainSize,
                             std::function<void(std::size_t, std::size_t)> body);

        [[nodiscard]] std::size_t WorkerCount() const { return m_workers.size(); }

    private:
        struct QueuedJob
        {
            std::function<void()> run;
            std::shared_ptr<JobFence::State> fence;
        };

        void WorkerLoop();
        void Submit(std::function<void()> job, const std::shared_ptr<JobFence::State>& fence);

        std::mutex m_mutex;
        std::condition_variable m_workReady;
        bool m_running{ true };
        std::queue<QueuedJob> m_queue;
        std::vector<std::thread> m_workers;
    };
}
