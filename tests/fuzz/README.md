# Fuzzing ofxVlc4 Parsers

This directory contains LibFuzzer targets for testing the robustness of ofxVlc4's parsing code against malformed input.

## Overview

Fuzzing systematically tests parsers with randomly mutated inputs to discover crashes, hangs, memory errors, and undefined behavior. The targets include:

- **fuzz_midi_parser**: Tests MIDI file parsing (`ofxVlc4MidiAnalysis`)
- **fuzz_m3u_parser**: Tests M3U playlist parsing (`ofxVlc4PlaylistHelpers::deserializeM3U`)
- **fuzz_xspf_parser**: Tests XSPF playlist parsing (`ofxVlc4PlaylistHelpers::deserializeXSPF`)

## Prerequisites

- **Clang compiler** with LibFuzzer support (clang++ 6.0+)
- **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)** support
- Python 3 (for creating seed corpus files)

Install on Ubuntu/Debian:
```bash
sudo apt-get install clang
```

## Building Fuzz Targets

Navigate to the `tests/fuzz` directory and build with CMake:

```bash
cd tests/fuzz
mkdir build
cd build
CXX=clang++ cmake -DENABLE_FUZZING=ON ..
make
```

This produces three executables:
- `fuzz_midi_parser`
- `fuzz_m3u_parser`
- `fuzz_xspf_parser`

## Running Fuzz Targets

Each fuzzer reads from a corpus directory containing seed inputs. Run with a time limit:

```bash
# MIDI parser (60 seconds)
./fuzz_midi_parser ../corpus/midi -max_total_time=60

# M3U parser (60 seconds)
./fuzz_m3u_parser ../corpus/m3u -max_total_time=60

# XSPF parser (60 seconds)
./fuzz_xspf_parser ../corpus/xspf -max_total_time=60
```

### Recommended Options

- `-max_total_time=N`: Stop after N seconds
- `-max_len=N`: Limit input size to N bytes (default: unlimited)
- `-timeout=N`: Treat inputs taking >N seconds as hangs (default: 1200)
- `-workers=N`: Run N parallel fuzzing processes
- `-dict=file`: Use a dictionary of tokens for more efficient fuzzing

Example with extended options:
```bash
./fuzz_midi_parser ../corpus/midi \
    -max_total_time=300 \
    -max_len=1048576 \
    -workers=4 \
    -timeout=10
```

## Corpus

The `corpus/` directory contains seed inputs:

- **corpus/midi/**: Minimal valid MIDI files
- **corpus/m3u/**: Sample M3U playlists
- **corpus/xspf/**: Sample XSPF playlists

LibFuzzer automatically expands the corpus as it discovers new code paths. Crashes are saved to the current directory with names like `crash-<hash>`.

## Interpreting Results

### Successful Run
```
#1000000 DONE   cov: 1234 ft: 5678 corp: 123/45KB ...
```
No crashes found. Coverage metrics show how much code was exercised.

### Crash Detected
```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
```
A crash was found! The fuzzer saves the crashing input to `crash-<hash>`. Reproduce:
```bash
./fuzz_midi_parser crash-<hash>
```

### Common Issues

1. **Heap-buffer-overflow**: Parser reads past allocated memory
2. **Use-after-free**: Parser accesses freed memory
3. **Stack-overflow**: Recursive parsing without depth limits
4. **Integer overflow**: Arithmetic without bounds checking
5. **Timeout**: Parser hangs on malformed input (infinite loop)

## Continuous Fuzzing

For long-running fuzzing campaigns, use a persistent corpus directory:

```bash
# Create persistent corpus
mkdir -p ~/fuzz-corpus/midi

# Run overnight
./fuzz_midi_parser ~/fuzz-corpus/midi -max_total_time=0
```

The corpus directory grows over time as new code paths are discovered.

## CI Integration

Fuzzing runs in CI for 60 seconds per target on each PR. See `.github/workflows/fuzz.yml`.

To add coverage for a new parser:
1. Create a new fuzz target in `tests/fuzz/fuzz_<name>_parser.cpp`
2. Add seed corpus in `tests/fuzz/corpus/<name>/`
3. Update `CMakeLists.txt` to build the target
4. Update `.github/workflows/fuzz.yml` to run the target

## References

- [LibFuzzer Tutorial](https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md)
- [LibFuzzer Options](https://llvm.org/docs/LibFuzzer.html#options)
- [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
