/**
 * Playback Performance Benchmark
 * Measures: startup time, seek latency, frame rate stability
 */

#include <vlc/vlc.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <cassert>
#include <fstream>

#ifndef BENCHMARK_TEST_MEDIA
#define BENCHMARK_TEST_MEDIA "test_media.mp4"
#endif

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct BenchmarkResults {
    double startup_time_ms = 0.0;
    double seek_forward_avg_ms = 0.0;
    double seek_backward_avg_ms = 0.0;
    int total_seeks = 0;
};

double measureStartupTime() {
    auto start = Clock::now();

    const char *vlc_argv[] = {"--quiet", "--no-video-title-show"};
    libvlc_instance_t *inst = libvlc_new(2, vlc_argv);
    assert(inst != nullptr);

    libvlc_media_t *media = libvlc_media_new_path(inst, BENCHMARK_TEST_MEDIA);
    assert(media != nullptr);

    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    assert(player != nullptr);
    libvlc_media_release(media);

    libvlc_media_player_play(player);

    // Wait for first frame
    while (!libvlc_media_player_is_playing(player)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = Clock::now();

    libvlc_media_player_stop(player);
    libvlc_media_player_release(player);
    libvlc_release(inst);

    return Duration(end - start).count();
}

double measureSeekTime(libvlc_media_player_t *player, float targetPos) {
    auto start = Clock::now();
    libvlc_media_player_set_position(player, targetPos);

    // Wait for seek to complete (position stabilizes)
    float lastPos = -1.0f;
    int stableCount = 0;
    while (stableCount < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        float curPos = libvlc_media_player_get_position(player);
        if (std::abs(curPos - lastPos) < 0.01f) {
            stableCount++;
        } else {
            stableCount = 0;
        }
        lastPos = curPos;
    }

    auto end = Clock::now();
    return Duration(end - start).count();
}

BenchmarkResults runPlaybackBenchmark() {
    BenchmarkResults results;

    std::cout << "Measuring startup time..." << std::endl;
    results.startup_time_ms = measureStartupTime();
    std::cout << "  Startup: " << results.startup_time_ms << " ms" << std::endl;

    std::cout << "Measuring seek performance..." << std::endl;

    const char *vlc_argv[] = {"--quiet"};
    libvlc_instance_t *inst = libvlc_new(1, vlc_argv);
    libvlc_media_t *media = libvlc_media_new_path(inst, BENCHMARK_TEST_MEDIA);
    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    libvlc_media_player_play(player);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::vector<double> forwardSeeks;
    std::vector<double> backwardSeeks;

    // Forward seeks
    for (float pos = 0.0f; pos <= 0.8f; pos += 0.2f) {
        double time = measureSeekTime(player, pos);
        forwardSeeks.push_back(time);
    }

    // Backward seeks
    for (float pos = 0.8f; pos >= 0.2f; pos -= 0.2f) {
        double time = measureSeekTime(player, pos);
        backwardSeeks.push_back(time);
    }

    // Calculate averages
    double forwardSum = 0.0, backwardSum = 0.0;
    for (double t : forwardSeeks) forwardSum += t;
    for (double t : backwardSeeks) backwardSum += t;

    results.seek_forward_avg_ms = forwardSum / forwardSeeks.size();
    results.seek_backward_avg_ms = backwardSum / backwardSeeks.size();
    results.total_seeks = forwardSeeks.size() + backwardSeeks.size();

    std::cout << "  Forward seek avg: " << results.seek_forward_avg_ms << " ms" << std::endl;
    std::cout << "  Backward seek avg: " << results.seek_backward_avg_ms << " ms" << std::endl;

    libvlc_media_player_stop(player);
    libvlc_media_player_release(player);
    libvlc_release(inst);

    return results;
}

void writeJsonReport(const BenchmarkResults &results) {
    std::ofstream out("benchmark_results_playback.json");
    out << "{\n";
    out << "  \"benchmark\": \"playback\",\n";
    out << "  \"test_media\": \"" << BENCHMARK_TEST_MEDIA << "\",\n";
    out << "  \"results\": {\n";
    out << "    \"startup_time_ms\": " << results.startup_time_ms << ",\n";
    out << "    \"seek_forward_avg_ms\": " << results.seek_forward_avg_ms << ",\n";
    out << "    \"seek_backward_avg_ms\": " << results.seek_backward_avg_ms << ",\n";
    out << "    \"total_seeks\": " << results.total_seeks << "\n";
    out << "  }\n";
    out << "}\n";
    out.close();

    std::cout << "\nResults written to: benchmark_results_playback.json" << std::endl;
}

int main() {
    std::cout << "=== ofxVlc4 Playback Performance Benchmark ===" << std::endl;
    std::cout << "Test media: " << BENCHMARK_TEST_MEDIA << std::endl;
    std::cout << std::endl;

    try {
        BenchmarkResults results = runPlaybackBenchmark();
        writeJsonReport(results);

        std::cout << "\nBenchmark complete!" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
}
