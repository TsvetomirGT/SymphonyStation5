#include "navi/tasks.hpp"

#include <utility>

namespace navi
{

namespace
{
constexpr size_t kWorkerStack = 1024 * 1024;
}

TaskPool::TaskPool(int threads, bool newest_first) : newest_first_(newest_first)
{
    for (int i = 0; i < threads; ++i)
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, kWorkerStack);
        pthread_t thread;
        if (pthread_create(&thread, &attr, &TaskPool::thread_main, this) == 0)
            threads_.push_back(thread);
        pthread_attr_destroy(&attr);
    }
}

TaskPool::~TaskPool()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    // A worker inside a network call finishes it first (bounded by curl's
    // timeouts); the app only destroys pools when it closes.
    for (pthread_t thread : threads_)
        pthread_join(thread, nullptr);
}

void *TaskPool::thread_main(void *self)
{
    static_cast<TaskPool *>(self)->run();
    return nullptr;
}

void TaskPool::run()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            if (newest_first_)
            {
                job = std::move(queue_.back());
                queue_.pop_back();
            }
            else
            {
                job = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (job.work)
            job.work();
        if (job.done)
        {
            std::lock_guard<std::mutex> lock(mu_);
            finished_.push_back(std::move(job.done));
        }
    }
}

void TaskPool::post(std::function<void()> work, std::function<void()> done)
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back({std::move(work), std::move(done)});
    }
    wake_.notify_one();
}

void TaskPool::pump()
{
    std::vector<std::function<void()>> finished;
    {
        std::lock_guard<std::mutex> lock(mu_);
        finished.swap(finished_);
    }
    for (auto &done : finished)
        done();
}

void TaskPool::clear_pending()
{
    std::lock_guard<std::mutex> lock(mu_);
    queue_.clear();
}

std::size_t TaskPool::pending() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

} // namespace navi
