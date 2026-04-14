# Contributing to ofxVlc4

Thank you for your interest in contributing to ofxVlc4! This document provides guidelines and workflows for contributing to this project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Standards](#code-standards)
- [Testing Guidelines](#testing-guidelines)
- [Documentation](#documentation)
- [Pull Request Process](#pull-request-process)

## Code of Conduct

This project follows standard open-source collaboration principles:
- Be respectful and constructive in discussions
- Focus on technical merit and project goals
- Welcome newcomers and help them get started
- Provide constructive feedback in code reviews

## Getting Started

### Prerequisites

- openFrameworks 0.12 or later
- C++17 compatible compiler
- CMake 3.14+ (for running tests)
- Git
- Platform-specific requirements (see README.md)

### Setting Up Your Development Environment

1. Fork the repository on GitHub
2. Clone your fork:
   ```bash
   git clone https://github.com/YOUR_USERNAME/ofxVlc4.git
   cd ofxVlc4
   ```

3. Install libVLC:
   ```bash
   bash scripts/install-libvlc.sh
   ```

4. Build and run tests to verify setup:
   ```bash
   mkdir -p /tmp/test-build
   cd /tmp/test-build
   cmake /path/to/ofxVlc4/tests
   make
   ctest --output-on-failure
   ```

5. Build an example to verify libVLC integration:
   ```bash
   cd ofxVlc4Example
   make
   ```

## Development Workflow

### Branching Strategy

- `main` - Stable releases only
- `develop` - Active development (if present)
- Feature branches: `feature/your-feature-name`
- Bug fixes: `fix/issue-description`

### Making Changes

1. Create a feature branch:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. Make your changes following the [Code Standards](#code-standards)

3. Add tests for new functionality (see [Testing Guidelines](#testing-guidelines))

4. Run tests locally:
   ```bash
   cd /tmp/test-build
   make
   ctest --output-on-failure
   ```

5. Run clang-tidy on changed files:
   ```bash
   find src -name "*.cpp" -newer .git/ORIG_HEAD | xargs clang-tidy -p /tmp/test-build -- -std=c++17
   ```

6. Update documentation if needed

7. Commit your changes with clear messages:
   ```bash
   git add .
   git commit -m "Add feature: brief description

   Detailed explanation of what changed and why.
   Fixes #issue-number (if applicable)"
   ```

## Code Standards

### Naming Conventions

Enforced by `.clang-tidy` configuration:

- **Classes/Structs/Enums**: `CamelCase`
  ```cpp
  class VideoComponent { };
  struct MediaInfo { };
  enum class PlaybackMode { };
  ```

- **Functions/Variables/Parameters**: `camelBack`
  ```cpp
  void playMedia();
  int frameCount = 0;
  void setVolume(int volumeLevel);
  ```

- **Private Members**: `m_` prefix
  ```cpp
  class MyClass {
  private:
      int m_privateValue;
      std::string m_internalState;
  };
  ```

- **Constants**: `k` prefix, `CamelCase`
  ```cpp
  constexpr int kMaxBufferSize = 1024;
  const float kDefaultVolume = 0.8f;
  ```

### Architecture Patterns

- **PIMPL Pattern**: All implementation details in `ofxVlc4::Impl`
- **Component Model**: Logic separated into AudioComponent, VideoComponent, MediaComponent, etc.
- **Single Source of Truth**: All libVLC handles owned by `VlcCoreSession`
- **Pure Logic Helpers**: Testable helpers in `src/support/` with no OF dependencies

### C++ Standards

- **C++17** required
- Use modern C++ features:
  - Smart pointers (`std::unique_ptr`, `std::shared_ptr`)
  - RAII for resource management
  - `std::optional` for optional values
  - Range-based for loops
  - `auto` where type is obvious

- Avoid:
  - Raw `new`/`delete`
  - C-style casts
  - Manual memory management
  - Global state

### Threading Safety

- Use per-concern mutexes (not global locks)
- Check `m_impl->lifecycleRuntime.shuttingDown` in all callbacks
- Use `std::atomic` with appropriate memory ordering:
  - `memory_order_release` for stores
  - `memory_order_acquire` for loads
  - See `ofxVlc4RingBuffer` for examples

### Error Handling

- Use exceptions for exceptional conditions
- Add bounds validation for user input
- Provide graceful degradation where possible
- Log errors/warnings appropriately using `ofLog` variants

## Testing Guidelines

### Test Structure

All tests use CMake and live in `tests/`:

```
tests/
├── CMakeLists.txt          # Test build configuration
├── test_*.cpp              # Individual test binaries
├── stubs/                  # Minimal OF/GLFW/VLC stubs
└── stubs_gl/               # GL-specific stubs
```

### Writing Tests

1. **Unit Tests** - Test pure logic in isolation:
   ```cpp
   // tests/test_your_feature.cpp
   #include <cassert>
   #include <iostream>

   void testBasicFunctionality() {
       // Arrange
       // Act
       // Assert
       assert(result == expected);
   }

   int main() {
       testBasicFunctionality();
       std::cout << "All tests passed!\n";
       return 0;
   }
   ```

2. **Add to CMakeLists.txt**:
   ```cmake
   add_executable(test_your_feature
       test_your_feature.cpp
       "${SRC_DIR}/support/YourFeature.cpp"
   )

   target_include_directories(test_your_feature PRIVATE
       "${STUBS_DIR}"
       "${SRC_DIR}/support"
   )

   add_test(NAME your_feature COMMAND test_your_feature)
   ```

3. **Run tests**:
   ```bash
   cd /tmp/test-build
   cmake /path/to/ofxVlc4/tests
   make
   ctest --output-on-failure
   ```

### Test Coverage Expectations

- **New features**: Add unit tests covering happy path and edge cases
- **Bug fixes**: Add regression test demonstrating the fix
- **Helper functions**: Must have corresponding test in `tests/test_*_helpers.cpp`
- **NLE logic**: Pure C++ tests in `tests/test_nle_*.cpp`

### Testing Without VLC Runtime

Tests use stubs in `tests/stubs/` to avoid requiring VLC installation:

- `ofMain.h` - Minimal OF types
- `GLFW/glfw3.h` - GLFW stub
- `vlc/vlc.h` - libVLC stub

This allows pure C++17 testing with no external dependencies.

## Documentation

### Code Documentation

- Use Doxygen-style comments for public API:
  ```cpp
  /**
   * @brief Plays the currently loaded media
   * @return true if playback started successfully
   */
  bool play();
  ```

- Add inline comments for complex logic
- Keep comments focused on "why" not "what"

### Documentation Files

When adding features, update relevant docs:

- `README.md` - Main feature list and getting started
- `docs/API_GUIDE.md` - API organization and usage patterns
- `docs/ARCHITECTURE.md` - Internal architecture changes
- `CHANGELOG.md` - User-facing changes

### Example Documentation

Each example should have:
- `README.md` explaining what it demonstrates
- List of API calls used
- Build instructions
- Controls/keyboard shortcuts

## Pull Request Process

### Before Submitting

1. ✅ Tests pass locally (`ctest --output-on-failure`)
2. ✅ Code follows naming conventions (check with clang-tidy)
3. ✅ Documentation updated if needed
4. ✅ Commit messages are clear and descriptive
5. ✅ No unintended files committed (check `.gitignore`)

### PR Template

When creating a PR, include:

```markdown
## Summary
Brief description of what this PR does.

## Motivation
Why is this change needed?

## Changes
- List of specific changes made
- Component affected
- Any API additions/modifications

## Testing
- [ ] Unit tests added/updated
- [ ] Tests pass locally
- [ ] Manually tested with example: [name]

## Documentation
- [ ] README.md updated
- [ ] API_GUIDE.md updated (if API changed)
- [ ] CHANGELOG.md updated
- [ ] Example README updated (if applicable)

## Breaking Changes
List any breaking API changes (or "None")

Fixes #issue-number
```

### Review Process

1. Maintainer will review code and provide feedback
2. Address review comments
3. Once approved, PR will be merged
4. Changes will be included in next release

### After Merge

1. Delete your feature branch
2. Pull latest main:
   ```bash
   git checkout main
   git pull upstream main
   ```

## Project Structure

Understanding the codebase organization:

```
ofxVlc4/
├── src/
│   ├── core/           # Core session, PIMPL, types
│   ├── audio/          # AudioComponent
│   ├── video/          # VideoComponent
│   ├── media/          # MediaComponent, library
│   ├── playback/       # PlaybackController
│   ├── recording/      # ofxVlc4Recorder
│   ├── midi/           # MIDI analysis, bridge, playback
│   ├── nle/            # NLE data model (pure C++17)
│   └── support/        # Pure logic helpers
├── tests/              # CMake-based unit tests
├── docs/               # Architecture, API guides
├── scripts/            # Installation scripts
└── ofxVlc4*Example/    # Example applications
```

## Common Tasks

### Adding a New Public API Method

1. Declare in `src/core/ofxVlc4.h`
2. Add to appropriate component (Audio, Video, Media, Playback)
3. Implement in component `.cpp` file
4. Add forwarding wrapper in top-level `.cpp` (e.g., `ofxVlc4Audio.cpp`)
5. Update `docs/API_GUIDE.md`
6. Add test in `tests/`
7. Update `CHANGELOG.md`

### Adding a New Helper Function

1. Add to appropriate header in `src/support/`
2. Implement (header-only or in corresponding `.cpp`)
3. Add test in `tests/test_*_helpers.cpp`
4. Ensure no OF dependencies (pure C++17)

### Fixing a Bug

1. Create test demonstrating the bug
2. Fix the bug
3. Verify test now passes
4. Add to `CHANGELOG.md` under "Bug Fixes"

## Getting Help

- **Questions**: Open a GitHub Discussion
- **Bugs**: Open a GitHub Issue with reproduction steps
- **Feature Requests**: Open a GitHub Issue describing use case
- **Documentation**: Check `docs/` directory and README.md

## Recognition

Contributors will be acknowledged in:
- `CHANGELOG.md` for their contributions
- GitHub contributor list
- Release notes when applicable

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

---

Thank you for contributing to ofxVlc4!
