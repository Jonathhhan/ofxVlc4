/**
 * Integration test for recording workflow
 * Requires: Real VLC runtime
 */

#include <vlc/vlc.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <sys/stat.h>

#ifndef INTEGRATION_TEST_MEDIA
#define INTEGRATION_TEST_MEDIA "test_media.mp4"
#endif

bool fileExists(const char *path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

long getFileSize(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

void testBasicRecording() {
    std::cout << "Testing basic recording..." << std::endl;

    const char *output = "/tmp/test_recording.mp4";

    // Remove old output if exists
    if (fileExists(output)) {
        std::remove(output);
    }

    // Initialize VLC
    const char *vlc_argv[] = {
        "--quiet",
        "--no-video-title-show"
    };
    int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);

    libvlc_instance_t *inst = libvlc_new(vlc_argc, vlc_argv);
    assert(inst != nullptr && "Failed to create VLC instance");

    // Create media
    libvlc_media_t *media = libvlc_media_new_path(inst, INTEGRATION_TEST_MEDIA);
    assert(media != nullptr && "Failed to create media");

    // Add record option
    char sout[512];
    snprintf(sout, sizeof(sout),
        "#duplicate{dst=display,dst=std{access=file,mux=mp4,dst=%s}}",
        output);
    libvlc_media_add_option(media, sout);

    // Create player
    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    assert(player != nullptr && "Failed to create player");

    libvlc_media_release(media);

    std::cout << "  ✓ Recording setup complete" << std::endl;

    // Start playback/recording
    int ret = libvlc_media_player_play(player);
    assert(ret == 0 && "Failed to start playback");

    // Record for 3 seconds
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "  ✓ Recording in progress..." << std::endl;

    // Stop
    libvlc_media_player_stop(player);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Cleanup VLC
    libvlc_media_player_release(player);
    libvlc_release(inst);

    std::cout << "  ✓ Recording stopped" << std::endl;

    // Wait for file finalization
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Verify output file
    assert(fileExists(output) && "Recording output file should exist");

    long fileSize = getFileSize(output);
    assert(fileSize > 1000 && "Recording should have reasonable file size");

    std::cout << "  ✓ Recording file created (" << fileSize << " bytes)" << std::endl;

    // Cleanup
    std::remove(output);

    std::cout << "  ✓ Cleanup successful" << std::endl;
}

void testRecordingFormats() {
    std::cout << "Testing different recording formats..." << std::endl;

    const char *formats[] = {
        "mp4",
        "mkv"
    };

    for (const char *format : formats) {
        char output[256];
        snprintf(output, sizeof(output), "/tmp/test_recording.%s", format);

        // Remove old output
        if (fileExists(output)) {
            std::remove(output);
        }

        const char *vlc_argv[] = {"--quiet"};
        libvlc_instance_t *inst = libvlc_new(1, vlc_argv);
        assert(inst != nullptr);

        libvlc_media_t *media = libvlc_media_new_path(inst, INTEGRATION_TEST_MEDIA);
        assert(media != nullptr);

        // Setup recording
        char sout[512];
        snprintf(sout, sizeof(sout),
            "#duplicate{dst=display,dst=std{access=file,mux=%s,dst=%s}}",
            format, output);
        libvlc_media_add_option(media, sout);

        libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
        assert(player != nullptr);
        libvlc_media_release(media);

        // Record for 2 seconds
        libvlc_media_player_play(player);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        libvlc_media_player_stop(player);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        libvlc_media_player_release(player);
        libvlc_release(inst);

        // Verify
        assert(fileExists(output) && "Recording should create output file");
        std::cout << "  ✓ " << format << " format works" << std::endl;

        // Cleanup
        std::remove(output);
    }
}

int main() {
    std::cout << "=== ofxVlc4 Integration Tests: Recording ===" << std::endl;
    std::cout << "Test media: " << INTEGRATION_TEST_MEDIA << std::endl;
    std::cout << std::endl;

    try {
        testBasicRecording();
        std::cout << std::endl;
        testRecordingFormats();
        std::cout << std::endl;
        std::cout << "All recording integration tests passed!" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
