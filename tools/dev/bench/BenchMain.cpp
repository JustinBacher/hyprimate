// Benchmark driver. Not linked into the plugin; the operator new replacement below counts
// allocations for this executable only.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include "Bench.hpp"

namespace {
    std::atomic<std::uint64_t> g_alloc_count{0};
    std::atomic<std::uint64_t> g_alloc_bytes{0};

    void *counted_alloc(std::size_t size, std::size_t alignment) {
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
        g_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
        void *pointer = nullptr;
        if (alignment > alignof(std::max_align_t)) {
            if (posix_memalign(&pointer, alignment, size == 0 ? 1 : size) != 0) {
                pointer = nullptr;
            }
        } else {
            pointer = std::malloc(size == 0 ? 1 : size);
        }
        if (pointer == nullptr) {
            throw std::bad_alloc();
        }
        return pointer;
    }
}  // namespace

// NOLINTBEGIN(misc-definitions-in-headers)
void *operator new(std::size_t size) {
    return counted_alloc(size, 0);
}
void *operator new[](std::size_t size) {
    return counted_alloc(size, 0);
}
void *operator new(std::size_t size, std::align_val_t alignment) {
    return counted_alloc(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
    return counted_alloc(size, static_cast<std::size_t>(alignment));
}
void operator delete(void *pointer) noexcept {
    std::free(pointer);
}
void operator delete[](void *pointer) noexcept {
    std::free(pointer);
}
void operator delete(void *pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void *pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete(void *pointer, std::align_val_t) noexcept {
    std::free(pointer);
}
void operator delete[](void *pointer, std::align_val_t) noexcept {
    std::free(pointer);
}
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
    std::free(pointer);
}
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    std::free(pointer);
}
// NOLINTEND(misc-definitions-in-headers)

namespace bench {
    AllocationCounters allocations() noexcept {
        return {g_alloc_count.load(std::memory_order_relaxed),
            g_alloc_bytes.load(std::memory_order_relaxed)};
    }
}  // namespace bench

namespace {
    using Clock = std::chrono::steady_clock;

    struct Result {
        const bench::Scenario *scenario = nullptr;
        std::vector<double> ns_per_run;
        std::uint64_t allocations = 0;  // median over samples
        std::uint64_t alloc_bytes = 0;
        bool deterministic = true;
        double checksum = 0;
    };

    double percentile(const std::vector<double> &sorted, double fraction) {
        const double POSITION = fraction * static_cast<double>(sorted.size() - 1);
        const auto LOWER = static_cast<std::size_t>(POSITION);
        const std::size_t UPPER = std::min(LOWER + 1, sorted.size() - 1);
        return sorted[LOWER] + ((sorted[UPPER] - sorted[LOWER]) * (POSITION - static_cast<double>(LOWER)));
    }

    Result measure(const bench::Scenario &scenario, int warmup, int samples) {
        Result result;
        result.scenario = &scenario;
        for (int i = 0; i < warmup; ++i) {
            bench::keep(scenario.run());
        }
        std::vector<std::uint64_t> counts;
        std::vector<std::uint64_t> bytes;
        for (int i = 0; i < samples; ++i) {
            const bench::AllocationCounters BEFORE = bench::allocations();
            const auto START = Clock::now();
            const double CHECKSUM = scenario.run();
            const auto STOP = Clock::now();
            const bench::AllocationCounters AFTER = bench::allocations();
            result.ns_per_run.push_back(
                std::chrono::duration<double, std::nano>(STOP - START).count());
            counts.push_back(AFTER.count - BEFORE.count);
            bytes.push_back(AFTER.bytes - BEFORE.bytes);
            if (i == 0) {
                result.checksum = CHECKSUM;
            } else if (CHECKSUM != result.checksum) {
                result.deterministic = false;
            }
        }
        std::ranges::sort(counts);
        std::ranges::sort(bytes);
        result.allocations = counts[counts.size() / 2];
        result.alloc_bytes = bytes[bytes.size() / 2];
        return result;
    }

    std::string json_escape(const std::string &text) {
        std::string out;
        for (const char CH : text) {
            if (CH == '"' || CH == '\\') {
                out += '\\';
            }
            out += CH;
        }
        return out;
    }

    void write_json(const std::string &path, const std::vector<Result> &results, int warmup) {
        std::ofstream out(path);
        out.precision(17);
        out << "{\n  \"schema\": 1,\n  \"warmup\": " << warmup << ",\n  \"scenarios\": [\n";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const Result &r = results[i];
            out << "    {\"name\": \"" << json_escape(r.scenario->name) << "\", \"frames\": "
                << r.scenario->frames << ", \"threaded\": " << (r.scenario->threaded ? "true" : "false")
                << ", \"deterministic\": " << (r.deterministic ? "true" : "false")
                << ", \"checksum\": " << r.checksum << ", \"allocations\": " << r.allocations
                << ", \"alloc_bytes\": " << r.alloc_bytes << ", \"ns_per_run\": [";
            for (std::size_t s = 0; s < r.ns_per_run.size(); ++s) {
                out << (s == 0 ? "" : ", ") << r.ns_per_run[s];
            }
            out << "]}" << (i + 1 == results.size() ? "\n" : ",\n");
        }
        out << "  ]\n}\n";
    }

    int parse_int(const char *text, const char *name) {
        char *end = nullptr;
        const long VALUE = std::strtol(text, &end, 10);
        if (end == text || *end != '\0' || VALUE < 0 || VALUE > 1000000) {
            std::fprintf(stderr, "invalid %s: %s\n", name, text);
            std::exit(2);
        }
        return static_cast<int>(VALUE);
    }
}  // namespace

int main(int argc, char **argv) {
    int warmup = 5;
    int samples = 30;
    std::string filter;
    std::string json_path;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string ARG = argv[i];
        const bool HAS_VALUE = i + 1 < argc;
        if (ARG == "--list") {
            list = true;
        } else if (ARG == "--warmup" && HAS_VALUE) {
            warmup = parse_int(argv[++i], "warmup");
        } else if (ARG == "--samples" && HAS_VALUE) {
            samples = std::max(1, parse_int(argv[++i], "samples"));
        } else if (ARG == "--filter" && HAS_VALUE) {
            filter = argv[++i];
        } else if (ARG == "--json" && HAS_VALUE) {
            json_path = argv[++i];
        } else {
            std::fprintf(stderr,
                "usage: %s [--list] [--filter SUBSTRING] [--warmup N] [--samples N] [--json FILE]\n",
                argv[0]);
            return 2;
        }
    }

    bench::Registry registry;
    bench::register_geometry(registry);
    bench::register_bubble(registry);
    bench::register_smoke(registry);
    bench::register_sheets(registry);
    bench::register_construction(registry);

    std::vector<const bench::Scenario *> selected;
    for (const bench::Scenario &scenario : registry) {
        if (filter.empty() || scenario.name.find(filter) != std::string::npos) {
            selected.push_back(&scenario);
        }
    }
    if (list) {
        for (const bench::Scenario *scenario : selected) {
            std::printf("%-28s %4u frames  %s\n",
                scenario->name.c_str(),
                scenario->frames,
                scenario->description.c_str());
        }
        return 0;
    }
    if (selected.empty()) {
        std::fprintf(stderr, "no scenario matches '%s'\n", filter.c_str());
        return 2;
    }

    std::printf("%-28s %10s %10s %10s %10s %9s %10s\n",
        "scenario", "med us/frm", "p90", "p99", "max", "allocs", "alloc KiB");
    std::vector<Result> results;
    bool nondeterministic = false;
    for (const bench::Scenario *scenario : selected) {
        Result result = measure(*scenario, warmup, samples);
        std::vector<double> sorted = result.ns_per_run;
        std::ranges::sort(sorted);
        const double PER_FRAME = 1e-3 / static_cast<double>(scenario->frames);
        std::printf("%-28s %10.3f %10.3f %10.3f %10.3f %9llu %10.1f%s\n",
            scenario->name.c_str(),
            percentile(sorted, 0.5) * PER_FRAME,
            percentile(sorted, 0.9) * PER_FRAME,
            percentile(sorted, 0.99) * PER_FRAME,
            sorted.back() * PER_FRAME,
            static_cast<unsigned long long>(result.allocations),
            static_cast<double>(result.alloc_bytes) / 1024.0,
            result.deterministic ? "" : "  NONDETERMINISTIC");
        nondeterministic = nondeterministic || (!result.deterministic && !scenario->threaded);
        results.push_back(std::move(result));
    }
    if (!json_path.empty()) {
        write_json(json_path, results, warmup);
    }
    return nondeterministic ? 1 : 0;
}
