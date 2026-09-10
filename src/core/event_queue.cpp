#include "core/event_queue.hpp"

#include <algorithm>

namespace ir {

EventQueue::EventQueue(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {
    buffer_.resize(capacity_);
}

bool EventQueue::try_push(Event&& event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        if (count_ == capacity_) {
            ++dropped_;
            overflowed_ = true;
            return false;
        }
        const std::size_t tail = (head_ + count_) % capacity_;
        buffer_[tail] = std::move(event);
        ++count_;
        ++pushed_;
        high_water_ = std::max<std::uint64_t>(high_water_, count_);
    }
    not_empty_.notify_one();
    return true;
}

std::size_t EventQueue::drain_locked(std::vector<Event>& out, std::size_t max_batch) {
    const std::size_t n = std::min(count_, max_batch);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(std::move(buffer_[head_]));
        buffer_[head_] = Event{};  // release moved-from payload storage
        head_ = (head_ + 1) % capacity_;
        --count_;
    }
    return n;
}

std::size_t EventQueue::wait_and_drain(std::vector<Event>& out, std::size_t max_batch) {
    if (max_batch == 0) {
        return 0;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return count_ > 0 || closed_; });
    if (count_ == 0) {
        return 0;  // closed and empty
    }
    return drain_locked(out, max_batch);
}

std::size_t EventQueue::try_drain(std::vector<Event>& out, std::size_t max_batch) {
    std::lock_guard<std::mutex> lock(mutex_);
    return drain_locked(out, max_batch);
}

void EventQueue::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    not_empty_.notify_all();
}

bool EventQueue::is_closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

std::size_t EventQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

bool EventQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ == 0;
}

EventQueue::Stats EventQueue::stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.pushed = pushed_;
    s.dropped = dropped_;
    s.high_water = high_water_;
    s.size = count_;
    s.capacity = capacity_;
    return s;
}

bool EventQueue::consume_overflow_flag() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool had = overflowed_;
    overflowed_ = false;
    return had;
}

}  // namespace ir
