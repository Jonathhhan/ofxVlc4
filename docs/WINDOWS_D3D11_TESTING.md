# Windows D3D11 Testing Infrastructure

This document describes the Windows-specific CI infrastructure for testing Direct3D 11 (D3D11) video backend functionality in ofxVlc4.

## Overview

ofxVlc4 supports hardware-accelerated video rendering on Windows through Direct3D 11, including:
- D3D11 hardware decoding (via DXVA2 and D3D11 decoder devices)
- D3D11 HDR metadata rendering backend
- GPU-accelerated video processing pipeline

The Windows CI infrastructure validates these features on Windows runners with real D3D11 hardware/software availability.

## Components

### 1. GitHub Actions Workflow: `.github/workflows/windows-d3d11.yml`

**Purpose**: Automated Windows builds and tests on every push/PR

**Key Features**:
- Runs on `windows-latest` (Windows Server 2022)
- Matrix builds: Debug and Release configurations
- Uses Visual Studio 2022 with x64 architecture
- Runs all tests including D3D11-specific test suite
- Collects D3D11 diagnostics using `dxdiag`
- Archives diagnostic artifacts for troubleshooting

**Workflow Steps**:
1. **Checkout**: Clone repository
2. **Setup MSVC**: Configure Microsoft Visual C++ build tools
3. **Configure CMake**: Generate Visual Studio project files
4. **Build**: Compile all tests in parallel
5. **Run tests**: Execute CTest suite with output on failure
6. **Run D3D11-specific tests**: Execute video backend tests individually
7. **Check D3D11 availability**: Run `dxdiag` to collect graphics info
8. **Upload diagnostics**: Archive diagnostic results as artifacts

**Trigger Events**:
- Push to `main` or `develop` branches
- Pull requests targeting `main` or `develop` branches

### 2. CMake Configuration: `tests/CMakeLists.txt`

**D3D11-Specific Additions**:

```cmake
# test_d3d11_backend - D3D11 backend enumeration and availability tests
add_executable(test_d3d11_backend test_d3d11_backend.cpp)
target_include_directories(test_d3d11_backend PRIVATE "${SRC_DIR}/support")
if(WIN32)
    target_link_libraries(test_d3d11_backend PRIVATE d3d11 dxgi)
endif()

# Windows D3D11-specific configuration
if(WIN32)
    # Find D3D11 libraries
    find_library(D3D11_LIBRARY d3d11)
    find_library(DXGI_LIBRARY dxgi)

    # Link D3D11 to video-related tests
    target_link_libraries(test_video_helpers PRIVATE ${D3D11_LIBRARY} ${DXGI_LIBRARY})
    target_link_libraries(test_video_pipeline PRIVATE ${D3D11_LIBRARY} ${DXGI_LIBRARY})
    target_link_libraries(test_video_output_reinit PRIVATE ${D3D11_LIBRARY} ${DXGI_LIBRARY})

    # Define D3D11 availability flag
    target_compile_definitions(test_video_helpers PRIVATE OFXVLC4_D3D11_AVAILABLE)

    # MSVC compiler flags
    if(MSVC)
        add_compile_options(/W4)           # Enable level 4 warnings
        add_compile_options(/wd4100)       # Suppress unreferenced parameter warnings
        add_compile_options(/wd4189)       # Suppress unused local variable warnings
    endif()
endif()
```

### 3. Test Suite: `tests/test_d3d11_backend.cpp`

**Purpose**: Validate D3D11 backend enumeration, labels, and device availability

**Test Coverage**:

1. **VideoOutputBackend Label Tests**
   - Validates `VideoOutputBackend::Texture` → "Texture"
   - Validates `VideoOutputBackend::NativeWindow` → "Native Window"
   - Validates `VideoOutputBackend::D3D11Metadata` → "D3D11 HDR Metadata"

2. **PreferredDecoderDevice Label Tests**
   - Validates `PreferredDecoderDevice::Any` → "Auto"
   - Validates `PreferredDecoderDevice::D3D11` → "D3D11"
   - Validates `PreferredDecoderDevice::DXVA2` → "DXVA2"
   - Validates `PreferredDecoderDevice::Nvdec` → "NVDEC"
   - Validates `PreferredDecoderDevice::None` → "None"

3. **Backend Selection Logic Tests**
   - Verifies all `VideoOutputBackend` enum values are distinct
   - Verifies all `PreferredDecoderDevice` enum values are distinct
   - Validates enum value assignments match expected ordinals

4. **D3D11 Availability Tests** (Windows only)
   - Attempts to create D3D11 device with hardware driver
   - Tests feature levels (11.1, 11.0, 10.1, 10.0)
   - Reports feature level support
   - Handles graceful failure in headless/VM environments

5. **D3D11 Device Features Tests** (Windows only)
   - Creates D3D11 device with video support flag
   - Queries for `ID3D10Multithread` interface (required for thread-safe video callbacks)
   - Validates multithreading protection can be enabled

**Cross-Platform Design**:
- Core label/enum tests run on all platforms (Linux, macOS, Windows)
- D3D11 device creation tests only run on Windows (`#ifdef _WIN32`)
- Non-Windows platforms skip platform-specific tests with informational message
- Tests never fail due to missing D3D11 hardware (CI/VMs may lack GPU)

## D3D11 Backend Architecture

### Video Output Backend Modes

ofxVlc4 supports three video output backends:

1. **Texture Mode** (default)
   - OpenGL texture rendering
   - Cross-platform (Linux, macOS, Windows)
   - No hardware decoding assumptions

2. **Native Window Mode**
   - Platform-native window rendering (Win32 HWND, X11 Window, NSView)
   - Allows VLC to render directly to window surface
   - Bypasses OpenGL texture pipeline

3. **D3D11 HDR Metadata Mode** (Windows only)
   - Uses D3D11 rendering pipeline for HDR content
   - Exposes HDR metadata (color space, luminance, mastering display)
   - Requires D3D11 device creation and render target management
   - Enables hardware-accelerated HDR → SDR tone mapping

### Decoder Device Preferences

ofxVlc4 allows users to specify preferred hardware decoder:

- **Any** (Auto): Let VLC choose best available decoder
- **D3D11**: Force Direct3D 11 Video Acceleration (Windows 8+)
- **DXVA2**: Force DirectX Video Acceleration 2 (Windows Vista+)
- **Nvdec**: Force NVIDIA NVDEC (CUDA-based)
- **None**: Disable hardware decoding, use software decoder

These preferences are passed to libVLC via `--avcodec-hw` option.

## Running Windows Tests Locally

### Prerequisites

- Windows 10/11 or Windows Server 2019/2022
- Visual Studio 2019 or later (with C++ desktop development workload)
- CMake 3.14 or later
- Git for Windows

### Build and Run

```powershell
# Clone repository
git clone https://github.com/Jonathhhan/ofxVlc4.git
cd ofxVlc4

# Configure with CMake
cmake -S tests -B build/tests -G "Visual Studio 17 2022" -A x64

# Build Debug configuration
cmake --build build/tests --config Debug --parallel

# Run all tests
ctest --test-dir build/tests --build-config Debug --output-on-failure

# Run D3D11-specific test individually
.\build\tests\Debug\test_d3d11_backend.exe
```

### Build Release Configuration

```powershell
# Build Release configuration
cmake --build build/tests --config Release --parallel

# Run tests
ctest --test-dir build/tests --build-config Release --output-on-failure
```

## Interpreting CI Results

### Successful Run

```
[videoOutputBackendLabel]
  PASS  std::string(videoOutputBackendLabel(VideoOutputBackend::Texture)) == std::string("Texture")
  PASS  std::string(videoOutputBackendLabel(VideoOutputBackend::D3D11Metadata)) == std::string("D3D11 HDR Metadata")

[D3D11 Availability (Windows)]
  INFO  D3D11 device created successfully
  INFO  Feature level: 45056
  PASS  true
```

### Expected Warnings in Headless Environments

```
[D3D11 Availability (Windows)]
  WARN  D3D11 device creation failed (HRESULT: 0x887A0004)
  INFO  This may be expected in headless/VM environments
  PASS  true
```

**Note**: GitHub-hosted Windows runners may use WARP (Windows Advanced Rasterization Platform) software renderer instead of GPU hardware. This is expected and tests should still pass.

### Failure Cases

If tests fail, check:

1. **Link Errors**: Ensure `d3d11.lib` and `dxgi.lib` are available
   - Requires Windows SDK installed
   - Visual Studio installer includes Windows SDK by default

2. **Compilation Errors**: Verify `<d3d11.h>` header is found
   - Windows SDK headers must be in include path
   - Check CMake output for missing SDK warnings

3. **Runtime Errors**: Check diagnostic artifact `d3d11_diagnostics.txt`
   - Available in Actions → Workflow Run → Artifacts section
   - Contains `dxdiag` output with GPU driver info

## Troubleshooting

### D3D11 Device Creation Fails

**Symptom**: Test logs show `D3D11CreateDevice` returns failure HRESULT

**Possible Causes**:
- Running in headless/VM environment without GPU
- Graphics drivers not installed
- Remote Desktop/RDP session (D3D11 may be disabled)

**Resolution**: Tests are designed to gracefully handle this scenario. The test will pass with a warning message.

### Missing d3d11.lib Link Error

**Symptom**: Linker error `LNK1104: cannot open file 'd3d11.lib'`

**Resolution**:
- Install Windows SDK via Visual Studio Installer
- Ensure Windows SDK component is selected during installation
- Re-run CMake configuration after installing SDK

### MSVC Compiler Warnings

**Symptom**: Many `/W4` warnings in stub code

**Resolution**: Warnings `/wd4100` and `/wd4189` are already suppressed in CMakeLists.txt for stub-related noise. Add additional suppressions if needed:

```cmake
add_compile_options(/wd4505)  # unreferenced local function
```

## Future Enhancements

Potential additions to Windows D3D11 testing infrastructure:

1. **HDR Content Testing**
   - Add test media files with HDR10/HLG metadata
   - Validate HDR metadata extraction from D3D11 backend
   - Test tone mapping behavior

2. **DXVA Decoder Testing**
   - Create test for hardware decoder enumeration
   - Validate D3D11 vs DXVA2 decoder selection
   - Measure decode performance benchmarks

3. **Multi-GPU Testing**
   - Test adapter enumeration with multiple GPUs
   - Validate GPU selection for video decode
   - Test GPU switching scenarios (hybrid laptops)

4. **Performance Profiling**
   - Add PIX (Performance Investigator for Xbox) integration
   - Capture GPU timeline traces in CI
   - Analyze D3D11 render target allocation patterns

5. **Compatibility Testing**
   - Test on Windows 8.1 (minimum for D3D11 HDR)
   - Test with Intel/AMD/NVIDIA GPUs separately
   - Test with older feature levels (10.0, 10.1)

## References

- [Direct3D 11 Overview (Microsoft Docs)](https://learn.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
- [D3D11 Video APIs (Microsoft Docs)](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation)
- [libVLC Video Output Documentation](https://www.videolan.org/developers/vlc/doc/doxygen/html/group__libvlc__media__player.html)
- [GitHub Actions: Windows Runners](https://docs.github.com/en/actions/using-github-hosted-runners/about-github-hosted-runners#supported-runners-and-hardware-resources)
- [CMake: Visual Studio Generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html)

## Maintenance

This infrastructure was added as **Phase 4** of the ofxVlc4 deep review improvement plan.

**Maintainer Notes**:
- Windows runners in CI may change GPU hardware/drivers over time
- Update expected feature levels if GitHub runner specs change
- Monitor for WARP vs hardware GPU rendering behavior changes
- Keep Visual Studio generator version in sync with GitHub Actions runner (`Visual Studio 17 2022`)
