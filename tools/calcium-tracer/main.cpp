// calcium-tracer — per-stage frame timing summaries.
//
// Reads the per-frame CSV the compositor writes (frame,ui_us,compositor_us,
// gpu_us) and reports the frame-budget statistics that are the CI gate's
// numbers (docs/00-overview.md §4.1.5, docs/06-roadmap.md "Continuous, from
// M0"): p50/p95/p99 per stage, overrun counts, and the implied refresh rate.
//
// Usage:
//   calcium-tracer [path-to-trace.csv]   (default: calcium-trace.csv)

#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Stage {
    std::vector<double> microseconds;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    std::uint64_t budget_overruns = 0;
};

double percentile(std::vector<double>& samples, double fraction) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t index = std::min(
        samples.size() - 1,
        static_cast<std::size_t>(static_cast<double>(samples.size()) * fraction));
    return samples[index];
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "calcium-trace.csv";
    std::ifstream file{path};
    if (!file.is_open()) {
        std::fprintf(stderr, "calcium-tracer: cannot open %s\n", path);
        return 1;
    }

    Stage ui, compositor, gpu;
    std::string line;
    std::getline(file, line);  // header: frame,ui_us,compositor_us,gpu_us

    std::uint64_t frame_count = 0;
    double first_compositor_us = 0.0;
    double last_compositor_us = 0.0;
    while (std::getline(file, line)) {
        std::uint64_t frame = 0;
        double ui_us = 0.0, compositor_us = 0.0, gpu_us = 0.0;
        if (std::sscanf(line.c_str(), "%llu,%lf,%lf,%lf", &frame, &ui_us,
                        &compositor_us, &gpu_us) != 4) {
            continue;
        }
        if (frame_count == 0) {
            first_compositor_us = compositor_us;
        }
        last_compositor_us = compositor_us;
        ui.microseconds.push_back(ui_us);
        compositor.microseconds.push_back(compositor_us);
        gpu.microseconds.push_back(gpu_us);
        ++frame_count;
    }

    if (frame_count == 0) {
        std::fprintf(stderr, "calcium-tracer: no frames in %s\n", path);
        return 1;
    }

    ui.p50 = percentile(ui.microseconds, 0.50);
    ui.p95 = percentile(ui.microseconds, 0.95);
    ui.p99 = percentile(ui.microseconds, 0.99);
    compositor.p50 = percentile(compositor.microseconds, 0.50);
    compositor.p95 = percentile(compositor.microseconds, 0.95);
    compositor.p99 = percentile(compositor.microseconds, 0.99);
    gpu.p50 = percentile(gpu.microseconds, 0.50);
    gpu.p95 = percentile(gpu.microseconds, 0.95);
    gpu.p99 = percentile(gpu.microseconds, 0.99);

    constexpr double budget_us = 1000.0 / 120.0 * 1000.0;  // 8333 us
    for (Stage* stage : {&ui, &compositor, &gpu}) {
        for (const double sample : stage->microseconds) {
            if (sample > budget_us) {
                ++stage->budget_overruns;
            }
        }
    }

    std::printf("calcium-tracer: %s\n", path);
    std::printf("  frames: %llu\n", static_cast<unsigned long long>(frame_count));
    std::printf("  stage        p50     p95     p99   >8.33ms\n");
    const auto row = [](const char* name, const Stage& stage) {
        std::printf("  %-12s %6.1f %6.1f %6.1f   %5llu\n", name, stage.p50,
                    stage.p95, stage.p99,
                    static_cast<unsigned long long>(stage.budget_overruns));
    };
    row("ui", ui);
    row("compositor", compositor);
    row("gpu", gpu);
    return 0;
}
