# Building and Testing {#building_guide}
\page building_guide Building and Testing
\ingroup guides

## Prerequisites

### Linux
```bash
# Install build dependencies
sudo apt-get update && sudo apt-get install -y \
    tzdata zip unzip curl tar git ca-certificates \
    linux-libc-dev build-essential pkg-config \
    yasm nasm autoconf automake autoconf-archive \
    autotools-dev python3 python3-jinja2 gcc g++ \
    make libtool libtool-bin astyle
```

### macOS
```bash
# Install build dependencies via Homebrew
brew update && brew install \
    cmake ninja pkg-config yasm nasm \
    autoconf automake libtool python3 astyle
```

### Windows (MSYS2)
```bash
# Install MSYS2 dependencies
pacman -Syu --noconfirm
pacman -S --noconfirm \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-pkg-config \
    mingw-w64-x86_64-nasm \
    mingw-w64-x86_64-yasm \
    mingw-w64-x86_64-autotools \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-libtool \
    mingw-w64-x86_64-python3 \
    mingw-w64-x86_64-astyle
```

## Building

### Step 1: Clone Repository
```bash
git clone --recurse-submodules https://github.com/MotionSpell/signals.git
cd signals
```

### Step 2: Setup vcpkg
```bash
# Linux/macOS
./vcpkg/bootstrap-vcpkg.sh

# Windows
.\vcpkg\bootstrap-vcpkg.bat
```

### Step 3: Configure and Build

#### Linux
```bash
# Configure
cmake --preset linux-production

# Build
cmake --build build --preset linux-production --parallel $(nproc)
```

#### macOS
```bash
# Configure
cmake --preset mac-production

# Build
cmake --build build --preset mac-production --parallel $(sysctl -n hw.logicalcpu)
```

#### Windows (MSYS2)
```bash
# Configure
cmake --preset mingw-production

# Build
cmake --build build --preset mingw-production --parallel $(nproc)
```

## Running Tests

### Linux
```bash
# Build and run tests
cmake --build build --preset linux-production --target unittests
./build/lib/unittests.exe
```

### macOS
```bash
# Build and run tests
cmake --build build --preset mac-production --target unittests
./build/lib/unittests.exe
```

### Windows
```bash
# Build and run tests
cmake --build build --preset mingw-production --target unittests
.\build\lib\unittests.exe
```

## Available CMake Presets

- `linux-production`: Release build for Linux
- `linux-develop`: Debug build for Linux
- `mac-production`: Release build for macOS
- `mac-develop`: Debug build for macOS
- `mingw-production`: Release build for Windows using MSYS2/MinGW
- `mingw-develop`: Debug build for Windows using MSYS2/MinGW

## Troubleshooting

### Common Issues

1. **Missing Dependencies**
   - Ensure all required packages are installed
   - Check vcpkg integration

2. **Build Errors**
   - Check CMake configuration logs
   - Verify compiler version requirements
   - Review platform-specific dependencies

3. **Test Failures**
   - Check test environment setup
   - Verify library paths
   - Review test logs

### Build Logs
Build logs are available in:
- `build/CMakeFiles/CMakeOutput.log`
- `build/CMakeFiles/CMakeError.log`
- `build/vcpkg/buildtrees/**/*.log`
