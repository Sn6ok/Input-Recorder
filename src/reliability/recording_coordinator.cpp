#include "reliability/recording_coordinator.hpp"

#include "core/text_snapshot.hpp"

namespace ir {

RecordingCoordinator::RecordingCoordinator(EventQueue& queue,
                                           StorageWorker& writer,
                                           EventStore& store, SessionId session)
    : RecordingCoordinator(queue, writer, store, session, Options{}, nullptr) {}

RecordingCoordinator::RecordingCoordinator(EventQueue& queue,
                                           StorageWorker& writer,
                                           EventStore& store, SessionId session,
                                           Options options,
                                           EmergencyBuffer* emergency)
    : queue_(queue),
      writer_(writer),
      store_(store),
      emergency_(emergency),
      session_(session),
      options_(options),
      snapshot_policy_(options.snapshots) {
    next_snapshot_id_ = store_.max_snapshot_id() + 1;
}

RecordingCoordinator::~RecordingCoordinator() { stop(); }

void RecordingCoordinator::start() {
    if (started_) return;
    started_ = true;
    thread_ = std::thread([this] { run(); });
}

void RecordingCoordinator::stop() {
    if (!started_) return;
    queue_.close();  // wakes the consumer; remaining events still drain
    if (thread_.joinable()) thread_.join();
    started_ = false;
}

void RecordingCoordinator::process(const Event& e) {
    engine_.process(e);
    if (e.type == EventType::ContextChanged) current_context_ = e.context;

    const bool degraded = degraded_.load(std::memory_order_acquire);
    if (degraded && emergency_ != nullptr) {
        emergency_->append(e);  // last-resort durability; replayed on recovery
    } else {
        writer_.submit_event(e);
    }

    if (snapshot_policy_.on_event(e.time.wall_ms)) {
        if (!degraded) {
            TextSnapshot snap;
            snap.id = SnapshotId{next_snapshot_id_++};
            snap.session = session_;
            snap.context = current_context_;
            snap.anchor_event = e.id;
            snap.timestamp_ms = e.time.wall_ms;
            snap.text = engine_.text();
            snap.confidence = engine_.confidence();
            snap.cursor = engine_.cursor();
            snap.cursor_known = engine_.cursor_known();
            writer_.submit_snapshot(snap);
            snapshots_taken_.fetch_add(1, std::memory_order_relaxed);
        }
        snapshot_policy_.note_snapshot(e.time.wall_ms);
    }
    ++processed_count_;
}

void RecordingCoordinator::publish_view() {
    std::lock_guard<std::mutex> lock(view_mutex_);
    view_text_ = engine_.text();
    view_annotated_ = engine_.annotated_text();
    view_confidence_ = engine_.confidence();
    view_processed_ = processed_count_;
}

void RecordingCoordinator::run() {
    std::vector<Event> batch;
    for (;;) {
        batch.clear();
        const std::size_t n = queue_.wait_and_drain(batch, options_.drain_batch);
        if (n == 0) break;  // queue closed and drained
        for (const Event& e : batch) process(e);
        publish_view();
    }
    publish_view();  // final state
}

RecordingCoordinator::View RecordingCoordinator::view() const {
    std::lock_guard<std::mutex> lock(view_mutex_);
    return View{view_text_, view_annotated_, view_confidence_, view_processed_};
}

std::uint64_t RecordingCoordinator::processed() const {
    std::lock_guard<std::mutex> lock(view_mutex_);
    return view_processed_;
}

}  // namespace ir
