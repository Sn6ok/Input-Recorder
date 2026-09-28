#include "storage/storage_worker.hpp"

#include <utility>

namespace ir {

StorageWorker::~StorageWorker() { stop(); }

void StorageWorker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) return;
    started_ = true;
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void StorageWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) return;
        stop_ = true;
    }
    work_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }
    flushed_cv_.notify_all();  // release any flush() waiting on shutdown
}

bool StorageWorker::has_pending_locked() const {
    return !events_.empty() || !contexts_.empty() || !clipboard_.empty() ||
           !snapshots_.empty();
}

void StorageWorker::submit_event(Event e) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(e));
        ++submitted_;
    }
    work_cv_.notify_one();
}

void StorageWorker::submit_context(const Context& c) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        contexts_.push_back(c);
        ++submitted_;
    }
    work_cv_.notify_one();
}

void StorageWorker::submit_clipboard_entry(const ClipboardEntry& e) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clipboard_.push_back(e);
        ++submitted_;
    }
    work_cv_.notify_one();
}

void StorageWorker::submit_snapshot(const TextSnapshot& s) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_.push_back(s);
        ++submitted_;
    }
    work_cv_.notify_one();
}

void StorageWorker::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    const std::uint64_t target = submitted_;
    // Nudge the worker in case it is idle.
    work_cv_.notify_all();
    flushed_cv_.wait(lock, [&] { return written_ >= target || !started_; });
}

void StorageWorker::run() {
    for (;;) {
        std::vector<Event> ev;
        std::vector<Context> ct;
        std::vector<ClipboardEntry> cl;
        std::vector<TextSnapshot> sn;
        std::uint64_t batch = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_cv_.wait(lock, [&] { return stop_ || has_pending_locked(); });
            if (!has_pending_locked() && stop_) break;

            ev.swap(events_);
            ct.swap(contexts_);
            cl.swap(clipboard_);
            sn.swap(snapshots_);
            batch = ev.size() + ct.size() + cl.size() + sn.size();
        }

        // Write the whole batch in one transaction (contexts + clipboard first
        // so events that reference them are already present). No lock held.
        std::uint64_t ev_ok = 0, ct_ok = 0, cl_ok = 0, sn_ok = 0;
        const bool ok = store_.db().transaction([&] {
            for (const auto& c : ct)
                if (store_.upsert_context(c)) ++ct_ok;
            for (const auto& c : cl)
                if (store_.insert_clipboard_entry(c)) ++cl_ok;
            for (const auto& s : sn)
                if (store_.insert_snapshot(s)) ++sn_ok;
            for (const auto& e : ev)
                if (store_.insert_event(e)) ++ev_ok;
            return true;
        });

        {
            std::lock_guard<std::mutex> lock(mutex_);
            written_ += batch;
            ++stats_.transactions;
            if (ok) {
                stats_.contexts_written += ct_ok;
                stats_.clipboard_written += cl_ok;
                stats_.snapshots_written += sn_ok;
                stats_.events_written += ev_ok;
            } else {
                ++stats_.write_errors;
            }
        }
        flushed_cv_.notify_all();
    }
}

StorageWorker::Stats StorageWorker::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::size_t StorageWorker::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::size_t>(submitted_ - written_);
}

}  // namespace ir
