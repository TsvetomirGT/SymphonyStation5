// SymphonyStation5 - background work with completions on the UI thread.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <vector>

namespace navi
{

// A fixed set of worker threads. post() queues `work` for a worker; when it
// returns, `done` is queued for the UI thread, which runs it in pump() (once
// per frame). Workers get a 1 MiB stack: console threads default to a small
// one, too small for TLS handshakes in curl/OpenSSL.
//
// With `newest_first`, the most recently posted job runs first: right for
// cover art, where what was just scrolled into view matters most.
class TaskPool
{
  public:
    TaskPool(int threads, bool newest_first);
    ~TaskPool();
    TaskPool(const TaskPool &) = delete;
    TaskPool &operator=(const TaskPool &) = delete;

    void post(std::function<void()> work, std::function<void()> done = {});
    // Runs finished jobs' `done` callbacks; call on the UI thread.
    void pump();
    // Drops jobs that have not started yet.
    void clear_pending();
    std::size_t pending() const;

  private:
    struct Job
    {
        std::function<void()> work;
        std::function<void()> done;
    };
    static void *thread_main(void *self);
    void run();

    bool newest_first_;
    mutable std::mutex mu_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::vector<std::function<void()>> finished_;
    bool stopping_ = false;
    std::vector<pthread_t> threads_;
};

} // namespace navi
