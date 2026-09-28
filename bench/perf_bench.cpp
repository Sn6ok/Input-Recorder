// Performance benchmark for Input Recorder's hot paths (spec §35, §453-§457).
//
// Measures REAL throughput and memory of the OS-independent core: the bounded
// event queue, binary (de)serialization, the reconstruction engine, and the
// SQLite async writer. It prints numbers, never asserts — run it to get figures,
// not pass/fail.
//
// IMPORTANT (honesty): these are algorithmic throughput/footprint numbers for
// the portable core, measured on whatever host compiles this file. They are NOT
// the end-to-end idle CPU / working-set of the Windows GUI app under real user
// input — that must be measured on Windows (Task Manager / GetProcessMemoryInfo)
// and is noted as pending in docs/performance.md. The same file builds on
// Windows so those figures can be reproduced there.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/event.hpp"
#include "core/event_queue.hpp"
#include "core/serialization.hpp"
#include "reconstruction/reconstruction_engine.hpp"
#include "storage/event_store.hpp"
#include "storage/storage_worker.hpp"

#if defined(__linux__)
#include <cstdio>
#endif

namespace {

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Peak resident set size in KiB (Linux only; 0 elsewhere).
long peak_rss_kib() {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    long value = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "VmHWM: %ld kB", &value) == 1) break;
    }
    std::fclose(f);
    return value;
#else
    return 0;
#endif
}

ir::Event key_event(std::uint64_t id) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.time = ir::Timestamp{static_cast<std::int64_t>(id), static_cast<std::int64_t>(id)};
    e.type = ir::EventType::KeyDown;
    e.category = ir::EventCategory::UserInput;
    e.flags = ir::EventFlags::ModCtrl;
    e.payload = ir::KeyEventData{0x41, 0x1E};
    return e;
}

ir::Event char_event(std::uint64_t id, char c) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.time = ir::Timestamp{static_cast<std::int64_t>(id), static_cast<std::int64_t>(id)};
    e.type = ir::EventType::TextInput;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::TextInputData{std::string(1, c)};
    return e;
}

void report(const char* name, std::uint64_t ops, double seconds) {
    const double rate = seconds > 0 ? ops / seconds : 0.0;
    std::printf("  %-28s %10llu ops  %8.3f ms  %12.0f ops/sec\n", name,
                static_cast<unsigned long long>(ops), seconds * 1000.0, rate);
}

void bench_queue(std::uint64_t n) {
    ir::EventQueue queue(1u << 16);
    std::vector<ir::Event> out;
    out.reserve(4096);
    const double t0 = now_s();
    std::uint64_t pushed = 0, drained = 0;
    for (std::uint64_t i = 1; i <= n; ++i) {
        if (!queue.try_push(key_event(i))) {
            out.clear();
            drained += queue.try_drain(out, 4096);
        }
        ++pushed;
        if (!queue.try_push(key_event(i))) {}  // keep it near-full
    }
    out.clear();
    while (queue.try_drain(out, 4096) > 0) {
        drained += out.size();
        out.clear();
    }
    report("EventQueue push+drain", pushed, now_s() - t0);
}

void bench_serialize(std::uint64_t n) {
    std::uint64_t bytes = 0;
    const double t0 = now_s();
    for (std::uint64_t i = 1; i <= n; ++i) {
        std::vector<std::byte> b = ir::serialize_event(char_event(i, 'x'));
        bytes += b.size();
        ir::Event e;
        ir::deserialize_event(b, e);
    }
    const double dt = now_s() - t0;
    report("serialize+deserialize", n, dt);
    std::printf("  %-28s %10.1f bytes/event\n", "  avg wire size",
                static_cast<double>(bytes) / static_cast<double>(n));
}

void bench_reconstruct(std::uint64_t n) {
    ir::ReconstructionEngine engine;
    const double t0 = now_s();
    for (std::uint64_t i = 1; i <= n; ++i) {
        engine.process(char_event(i, static_cast<char>('a' + (i % 26))));
    }
    report("reconstruction process", n, now_s() - t0);
}

void bench_storage(std::uint64_t n) {
    const std::string path = "/tmp/ir_perf_bench.db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());

    ir::EventStore store;
    if (!store.open(path)) {
        std::printf("  storage: FAILED to open %s\n", path.c_str());
        return;
    }
    ir::StorageWorker writer(store);
    writer.start();
    const double t0 = now_s();
    for (std::uint64_t i = 1; i <= n; ++i) writer.submit_event(char_event(i, 'y'));
    writer.flush();
    const double dt = now_s() - t0;
    report("storage write (SQLite)", n, dt);

    writer.stop();
    store.close();

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
        std::fseek(f, 0, SEEK_END);
        const long fsz = std::ftell(f);
        std::fclose(f);
        std::printf("  %-28s %10.1f db-bytes/event (%ld total)\n",
                    "  on-disk size", static_cast<double>(fsz) / static_cast<double>(n),
                    fsz);
    }
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t n = 1'000'000;
    if (argc > 1) n = std::strtoull(argv[1], nullptr, 10);

    std::printf("Input Recorder hot-path benchmark (n=%llu)\n",
                static_cast<unsigned long long>(n));
    std::printf("  sizeof(Event)=%zu  sizeof(EventPayload)=%zu\n",
                sizeof(ir::Event), sizeof(ir::EventPayload));
    std::printf("  bounded queue (65536) memory ~= %.1f MiB\n",
                static_cast<double>(sizeof(ir::Event)) * 65536.0 / (1024.0 * 1024.0));
    std::puts("");

    bench_queue(n);
    bench_serialize(n);
    bench_reconstruct(n / 4);  // reconstruction grows a buffer; keep it bounded
    bench_storage(n / 10);     // storage is the slowest; fewer ops

    std::printf("\n  peak RSS: %ld KiB (%.1f MiB) [Linux only]\n", peak_rss_kib(),
                static_cast<double>(peak_rss_kib()) / 1024.0);
    return 0;
}
