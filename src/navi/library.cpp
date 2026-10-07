// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navi/library.hpp"

#include <pthread.h>

#include <mutex>
#include <utility>

namespace navi
{

struct Library::Shared
{
    std::mutex mu;
    bool busy = false;
    bool finished = false;
    Result result;
};

namespace
{

struct Job
{
    std::shared_ptr<Library::Shared> shared;
    Config config;
};

// Console threads get a small default stack; TLS handshakes in curl/OpenSSL
// need far more (ps5-homebrew-ui uses 1 MiB for its network worker).
constexpr size_t kWorkerStack = 1024 * 1024;

} // namespace

// Defined outside the anonymous namespace so it can name Library::Shared.
static void *RunJob(void *arg)
{
    std::unique_ptr<Job> job(static_cast<Job *>(arg));
    Library::Result result;
    result.config = job->config;
    subsonic::Client client(job->config);
    result.ok = client.Ping(&result.error) && client.GetArtists(&result.artists, &result.error);

    std::lock_guard<std::mutex> lock(job->shared->mu);
    job->shared->result = std::move(result);
    job->shared->busy = false;
    job->shared->finished = true;
    return nullptr;
}

Library::Library() : shared_(std::make_shared<Shared>()) {}

// The worker keeps its own reference to the shared state, so a request still
// running at exit finishes harmlessly.
Library::~Library() = default;

bool Library::busy() const
{
    std::lock_guard<std::mutex> lock(shared_->mu);
    return shared_->busy;
}

bool Library::start(const Config &config)
{
    {
        std::lock_guard<std::mutex> lock(shared_->mu);
        if (shared_->busy)
            return false;
        shared_->busy = true;
        shared_->finished = false;
    }
    auto *job = new Job{shared_, config};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kWorkerStack);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    const int rc = pthread_create(&thread, &attr, RunJob, job);
    pthread_attr_destroy(&attr);
    if (rc != 0)
    {
        delete job;
        std::lock_guard<std::mutex> lock(shared_->mu);
        shared_->busy = false;
        shared_->finished = true;
        shared_->result = Result{config, false, "could not start a worker thread", {}};
    }
    return true;
}

bool Library::take(Result *out)
{
    std::lock_guard<std::mutex> lock(shared_->mu);
    if (!shared_->finished)
        return false;
    shared_->finished = false;
    *out = std::move(shared_->result);
    return true;
}

} // namespace navi
