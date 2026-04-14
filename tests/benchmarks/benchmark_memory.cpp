/**
 * Memory Usage Benchmark
 * Measures: baseline memory, per-stream overhead, leak detection
 */

#include <vlc/vlc.h>
#include <iostream>
#include <fstream>
#include <cassert>
#include <thread>
#include <chrono>

#ifdef __linux__
#include <fstream>
#include <string>
#endif

#ifndef BENCHMARK_TEST_MEDIA
#define BENCHMARK_TEST_MEDIA "test_media.mp4"
#endif

struct MemoryBenchmarkResults {
    long baseline_kb = 0;
    long with_player_kb = 0;
    long during_playback_kb = 0;
    long peak_kb = 0;
    int leak_test_cycles = 0;
    long leak_test_delta_kb = 0;
};

long getCurrentMemoryUsageKB() {
#ifdef __linux__
    // Read from /proc/self/status
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") == 0) {
            // Extract number
            size_t pos = line.find_first_of("0123456789");
            if (pos != std::string::npos) {
                return std::stol(line.substr(pos));
            }
        }
    }
#endif
    return 0;  // Not implemented for this platform
}

MemoryBenchmarkResults measureMemoryUsage() {
    MemoryBenchmarkResults results;

    std::cout << "Measuring baseline memory..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    results.baseline_kb = getCurrentMemoryUsageKB();
    std::cout << "  Baseline: " << results.baseline_kb << " KB" << std::endl;

    std::cout << "Measuring memory with player instance..." << std::endl;
    const char *vlc_argv[] = {"--quiet"};
    libvlc_instance_t *inst = libvlc_new(1, vlc_argv);
    assert(inst != nullptr);

    libvlc_media_t *media = libvlc_media_new_path(inst, BENCHMARK_TEST_MEDIA);
    assert(media != nullptr);

    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    assert(player != nullptr);
    libvlc_media_release(media);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    results.with_player_kb = getCurrentMemoryUsageKB();
    std::cout << "  With player: " << results.with_player_kb << " KB" << std::endl;
    std::cout << "  Overhead: " << (results.with_player_kb - results.baseline_kb) << " KB" << std::endl;

    std::cout << "Measuring memory during playback..." << std::endl;
    libvlc_media_player_play(player);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    results.during_playback_kb = getCurrentMemoryUsageKB();
    results.peak_kb = results.during_playback_kb;  // Simplification
    std::cout << "  During playback: " << results.during_playback_kb << " KB" << std::endl;

    libvlc_media_player_stop(player);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Running leak detection test..." << std::endl;
    long beforeLeakTest = getCurrentMemoryUsageKB();

    // Create and destroy player multiple times
    const int cycles = 10;
    for (int i = 0; i < cycles; ++i) {
        libvlc_media_t *m = libvlc_media_new_path(inst, BENCHMARK_TEST_MEDIA);
        libvlc_media_player_t *p = libvlc_media_player_new_from_media(m);
        libvlc_media_release(m);

        libvlc_media_player_play(p);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        libvlc_media_player_stop(p);
        libvlc_media_player_release(p);
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    long afterLeakTest = getCurrentMemoryUsageKB();

    results.leak_test_cycles = cycles;
    results.leak_test_delta_kb = afterLeakTest - beforeLeakTest;

    std::cout << "  Leak test cycles: " << cycles << std::endl;
    std::cout << "  Memory delta: " << results.leak_test_delta_kb << " KB" << std::endl;

    if (results.leak_test_delta_kb > 10000) {  // 10 MB threshold
        std::cout << "  ⚠️  Potential memory leak detected!" << std::endl;
    } else {
        std::cout << "  ✓  No significant leak detected" << std::endl;
    }

    libvlc_media_player_release(player);
    libvlc_release(inst);

    return results;
}

void writeJsonReport(const MemoryBenchmarkResults &results) {
    std::ofstream out("benchmark_results_memory.json");
    out << "{\n";
    out << "  \"benchmark\": \"memory\",\n";
    out << "  \"test_media\": \"" << BENCHMARK_TEST_MEDIA << "\",\n";
    out << "  \"results\": {\n";
    out << "    \"baseline_kb\": " << results.baseline_kb << ",\n";
    out << "    \"with_player_kb\": " << results.with_player_kb << ",\n";
    out << "    \"during_playback_kb\": " << results.during_playback_kb << ",\n";
    out << "    \"peak_kb\": " << results.peak_kb << ",\n";
    out << "    \"player_overhead_kb\": " << (results.with_player_kb - results.baseline_kb) << ",\n";
    out << "    \"leak_test_cycles\": " << results.leak_test_cycles << ",\n";
    out << "    \"leak_test_delta_kb\": " << results.leak_test_delta_kb << "\n";
    out << "  }\n";
    out << "}\n";
    out.close();

    std::cout << "\nResults written to: benchmark_results_memory.json" << std::endl;
}

int main() {
    std::cout << "=== ofxVlc4 Memory Usage Benchmark ===" << std::endl;
    std::cout << "Test media: " << BENCHMARK_TEST_MEDIA << std::endl;

#ifndef __linux__
    std::cout << "⚠️  Memory measurement only implemented for Linux" << std::endl;
    std::cout << "Results will show zeros on this platform." << std::endl;
#endif

    std::cout << std::endl;

    try {
        MemoryBenchmarkResults results = measureMemoryUsage();
        writeJsonReport(results);

        std::cout << "\nBenchmark complete!" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
}
