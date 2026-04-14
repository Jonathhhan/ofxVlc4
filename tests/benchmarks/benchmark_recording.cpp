/**
 * Recording Performance Benchmark
 * Measures: encoding throughput, mux overhead
 */

#include <vlc/vlc.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

#ifndef BENCHMARK_TEST_MEDIA
#define BENCHMARK_TEST_MEDIA "test_media.mp4"
#endif

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct RecordingBenchmarkResults {
    double recording_duration_ms = 0.0;
    double finalization_time_ms = 0.0;
    long output_file_size = 0;
    double throughput_mbps = 0.0;
};

long getFileSize(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

RecordingBenchmarkResults measureRecordingPerformance() {
    RecordingBenchmarkResults results;
    const char *output = "/tmp/benchmark_recording.mp4";

    // Remove old output
    std::remove(output);

    std::cout << "Setting up recording..." << std::endl;

    const char *vlc_argv[] = {"--quiet"};
    libvlc_instance_t *inst = libvlc_new(1, vlc_argv);
    assert(inst != nullptr);

    libvlc_media_t *media = libvlc_media_new_path(inst, BENCHMARK_TEST_MEDIA);
    assert(media != nullptr);

    // Setup recording with standard settings
    char sout[512];
    snprintf(sout, sizeof(sout),
        "#duplicate{dst=display,dst=std{access=file,mux=mp4,dst=%s}}",
        output);
    libvlc_media_add_option(media, sout);

    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    assert(player != nullptr);
    libvlc_media_release(media);

    std::cout << "Starting recording..." << std::endl;
    auto startRecord = Clock::now();

    libvlc_media_player_play(player);

    // Record for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));

    auto endRecord = Clock::now();
    results.recording_duration_ms = Duration(endRecord - startRecord).count();

    std::cout << "Stopping and finalizing..." << std::endl;
    auto startFinalize = Clock::now();

    libvlc_media_player_stop(player);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    libvlc_media_player_release(player);
    libvlc_release(inst);

    // Wait for file finalization
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto endFinalize = Clock::now();
    results.finalization_time_ms = Duration(endFinalize - startFinalize).count();

    // Get file size
    results.output_file_size = getFileSize(output);

    // Calculate throughput (Mbps)
    if (results.recording_duration_ms > 0) {
        double seconds = results.recording_duration_ms / 1000.0;
        double bits = results.output_file_size * 8.0;
        results.throughput_mbps = (bits / 1000000.0) / seconds;
    }

    std::cout << "  Recording duration: " << results.recording_duration_ms << " ms" << std::endl;
    std::cout << "  Finalization time: " << results.finalization_time_ms << " ms" << std::endl;
    std::cout << "  Output size: " << results.output_file_size << " bytes" << std::endl;
    std::cout << "  Throughput: " << results.throughput_mbps << " Mbps" << std::endl;

    // Cleanup
    std::remove(output);

    return results;
}

void writeJsonReport(const RecordingBenchmarkResults &results) {
    std::ofstream out("benchmark_results_recording.json");
    out << "{\n";
    out << "  \"benchmark\": \"recording\",\n";
    out << "  \"test_media\": \"" << BENCHMARK_TEST_MEDIA << "\",\n";
    out << "  \"results\": {\n";
    out << "    \"recording_duration_ms\": " << results.recording_duration_ms << ",\n";
    out << "    \"finalization_time_ms\": " << results.finalization_time_ms << ",\n";
    out << "    \"output_file_size\": " << results.output_file_size << ",\n";
    out << "    \"throughput_mbps\": " << results.throughput_mbps << "\n";
    out << "  }\n";
    out << "}\n";
    out.close();

    std::cout << "\nResults written to: benchmark_results_recording.json" << std::endl;
}

int main() {
    std::cout << "=== ofxVlc4 Recording Performance Benchmark ===" << std::endl;
    std::cout << "Test media: " << BENCHMARK_TEST_MEDIA << std::endl;
    std::cout << std::endl;

    try {
        RecordingBenchmarkResults results = measureRecordingPerformance();
        writeJsonReport(results);

        std::cout << "\nBenchmark complete!" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
}
