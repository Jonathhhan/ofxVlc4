/**
 * Integration test for basic playback lifecycle
 * Requires: Real VLC runtime, test media file
 */

#include <vlc/vlc.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

#ifndef INTEGRATION_TEST_MEDIA
#define INTEGRATION_TEST_MEDIA "test_media.mp4"
#endif

void testBasicPlayback() {
    std::cout << "Testing basic playback lifecycle..." << std::endl;

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
    assert(media != nullptr && "Failed to create media (check test_media.mp4 exists)");

    // Create player
    libvlc_media_player_t *player = libvlc_media_player_new_from_media(media);
    assert(player != nullptr && "Failed to create player");

    libvlc_media_release(media);

    // Start playback
    int ret = libvlc_media_player_play(player);
    assert(ret == 0 && "Failed to start playback");

    std::cout << "  ✓ Playback started" << std::endl;

    // Let it play for 2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Check if playing
    bool isPlaying = libvlc_media_player_is_playing(player);
    assert(isPlaying && "Player should be playing");

    std::cout << "  ✓ Player is playing" << std::endl;

    // Seek to 50%
    libvlc_media_player_set_position(player, 0.5f);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    float pos = libvlc_media_player_get_position(player);
    assert(pos > 0.3f && pos < 0.7f && "Position should be around 50%");

    std::cout << "  ✓ Seeking works (position: " << pos << ")" << std::endl;

    // Pause
    libvlc_media_player_pause(player);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Check paused state
    libvlc_state_t state = libvlc_media_player_get_state(player);
    assert(state == libvlc_Paused && "Player should be paused");

    std::cout << "  ✓ Pause works" << std::endl;

    // Stop
    libvlc_media_player_stop(player);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    state = libvlc_media_player_get_state(player);
    assert(state == libvlc_Stopped && "Player should be stopped");

    std::cout << "  ✓ Stop works" << std::endl;

    // Cleanup
    libvlc_media_player_release(player);
    libvlc_release(inst);

    std::cout << "  ✓ Cleanup successful" << std::endl;
}

void testMediaInfo() {
    std::cout << "Testing media info..." << std::endl;

    const char *vlc_argv[] = {"--quiet"};
    libvlc_instance_t *inst = libvlc_new(1, vlc_argv);
    assert(inst != nullptr);

    libvlc_media_t *media = libvlc_media_new_path(inst, INTEGRATION_TEST_MEDIA);
    assert(media != nullptr);

    // Parse media
    int parse_ret = libvlc_media_parse_with_options(media,
        libvlc_media_parse_local, -1);
    assert(parse_ret == 0 && "Failed to start media parsing");

    // Wait for parsing (max 5 seconds)
    for (int i = 0; i < 50; ++i) {
        libvlc_media_parsed_status_t status = libvlc_media_get_parsed_status(media);
        if (status == libvlc_media_parsed_status_done) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Get duration
    libvlc_time_t duration = libvlc_media_get_duration(media);
    assert(duration > 0 && "Media should have positive duration");

    std::cout << "  ✓ Media duration: " << duration << " ms" << std::endl;

    libvlc_media_release(media);
    libvlc_release(inst);
}

int main() {
    std::cout << "=== ofxVlc4 Integration Tests: Playback ===" << std::endl;
    std::cout << "Test media: " << INTEGRATION_TEST_MEDIA << std::endl;
    std::cout << std::endl;

    try {
        testBasicPlayback();
        std::cout << std::endl;
        testMediaInfo();
        std::cout << std::endl;
        std::cout << "All playback integration tests passed!" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
