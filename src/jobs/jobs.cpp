#include <algorithm>
#include <mantis/jobs.hpp>
namespace mantis::jobs {
Manager::Manager(LogSink logger) : logger_(std::move(logger)) {
    worker_ = std::jthread([this](std::stop_token stop) {
        while (true) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);
                if (!ready_.wait(lock, stop, [&] { return !queue_.empty(); }))
                    return;
                job = queue_.front();
                queue_.pop_front();
                if (job->cancel.cancelled()) {
                    job->snapshot.state = State::cancelled;
                    job->task = {};
                    continue;
                }
                job->snapshot.state = State::running;
            }
            Context context{job->cancel, [this, job](double progress, std::string status) {
                                std::lock_guard lock(mutex_);
                                job->snapshot.progress = std::clamp(progress, 0.0, 1.0);
                                job->snapshot.status = std::move(status);
                            }};
            try {
                context.cancellation.check();
                auto result = job->task(context);
                std::lock_guard lock(mutex_);
                job->snapshot.result = std::move(result);
                job->snapshot.progress = 1;
                job->snapshot.state = State::completed;
                job->snapshot.status = "Completed";
            } catch (const Failure &e) {
                std::lock_guard lock(mutex_);
                job->snapshot.state = e.error.code == Status::cancelled ? State::cancelled : State::failed;
                job->snapshot.diagnostics = e.what();
            } catch (const std::exception &e) {
                std::lock_guard lock(mutex_);
                job->snapshot.state = State::failed;
                job->snapshot.diagnostics = e.what();
            }
            {
                std::lock_guard lock(mutex_);
                job->task = {};
                if (logger_)
                    logger_({job->snapshot.state == State::failed ? "error" : "info", "jobs",
                             job->snapshot.id.value + ": " + state_name(job->snapshot.state) + " " +
                                 job->snapshot.diagnostics});
            }
        }
    });
}
Manager::~Manager() {
    {
        std::lock_guard lock(mutex_);
        for (auto &[id, j] : jobs_)
            j->cancel.cancel();
        worker_.request_stop();
        ready_.notify_all();
    }
    if (worker_.joinable())
        worker_.join();
}
Id Manager::submit(std::string name, Task task) {
    std::lock_guard lock(mutex_);
    if (queue_.size() >= 64)
        fail(Status::busy, "Job queue capacity reached");
    if (jobs_.size() >= 256) {
        auto old = std::find_if(jobs_.begin(), jobs_.end(), [](auto &entry) {
            return entry.second->snapshot.state == State::completed ||
                   entry.second->snapshot.state == State::failed ||
                   entry.second->snapshot.state == State::cancelled;
        });
        if (old == jobs_.end())
            fail(Status::busy, "Job history capacity reached");
        jobs_.erase(old);
    }
    auto job = std::make_shared<Job>();
    job->snapshot.id = Id::random();
    job->snapshot.name = std::move(name);
    job->task = std::move(task);
    jobs_.emplace(job->snapshot.id, job);
    queue_.push_back(job);
    ready_.notify_one();
    return job->snapshot.id;
}
std::vector<Snapshot> Manager::list() const {
    std::lock_guard lock(mutex_);
    std::vector<Snapshot> out;
    for (auto &[id, j] : jobs_)
        out.push_back(j->snapshot);
    return out;
}
Snapshot Manager::get(const Id &id) const {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(id);
    if (it == jobs_.end())
        fail(Status::not_found, "Unknown job");
    return it->second->snapshot;
}
void Manager::cancel(const Id &id) {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(id);
    if (it == jobs_.end())
        fail(Status::not_found, "Unknown job");
    it->second->cancel.cancel();
}
bool Manager::busy() const {
    std::lock_guard lock(mutex_);
    for (auto &[id, j] : jobs_)
        if (j->snapshot.state == State::queued || j->snapshot.state == State::running)
            return true;
    return false;
}
} // namespace mantis::jobs
