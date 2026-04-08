cmake_minimum_required(VERSION 3.14)
project(ofxVlc4Tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# The ring-buffer implementation lives one level up in src/support/.
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(STUBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

add_executable(test_ringbuffer
    test_ringbuffer.cpp
    "${SRC_DIR}/support/ofxVlc4RingBuffer.cpp"
)

# Expose the stub headers in preference to any real OF installation.
target_include_directories(test_ringbuffer PRIVATE
    "${STUBS_DIR}"
    "${SRC_DIR}/support"
)

# The stub provides ofSoundBuffer.h directly; suppress the nested OF path.
target_compile_definitions(test_ringbuffer PRIVATE
    OFXVLC4_RINGBUFFER_STUB
)

add_test(NAME ringbuffer COMMAND test_ringbuffer)

# ---------------------------------------------------------------------------
# Utility tests (isUri, trimWhitespace, sanitizeFileStem, filter chains, ...)
# ofxVlc4Utils.h is header-only; the stubs provide OF/GLFW/VLC stand-ins.
# ---------------------------------------------------------------------------
add_executable(test_utils
    test_utils.cpp
)

target_include_directories(test_utils PRIVATE
    "${STUBS_DIR}"
    "${SRC_DIR}/support"
)

add_test(NAME utils COMMAND test_utils)
