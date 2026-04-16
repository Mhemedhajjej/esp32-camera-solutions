# ESP32 Camera Solutions v0.1

A modular, configurable ESP32-CAM firmware framework for various camera applications. The architecture supports feature-driven development with configuration-based builds for different use cases.

## Architecture

The framework is built around modular components that can be independently enabled/disabled:

- **Power Manager** (v0.1): Automatic idle deep sleep and external wakeup
- **Camera Service** (planned): Image capture and video streaming
- **WiFi Manager** (planned): Network connectivity and remote control
- **Storage** (planned): SD card and cloud integration

## Configuration-Based Builds

Different binaries are generated from the same codebase via `Kconfig` options:

```bash
idf.py menuconfig
```

Select your use case and required features. The build system includes/excludes components accordingly.

## Quick Start

### Setup

1. Set the ESP-IDF environment:
   ```bash
   . /path/to/esp-idf/export.sh
   ```

2. Enter the project directory:
   ```bash
   cd /path/to/esp32-camera-solutions
   ```

3. Configure for your use case:
   ```bash
   idf.py menuconfig
   ```
   - Select power manager options (GPIO, timeout)
   - More options will appear as components are added

4. Build and flash:
   ```bash
   idf.py build
   idf.py flash
   idf.py monitor
   ```

## Development Roadmap

**v0.1 (Current)**
- Power manager with configurable idle sleep and external wakeup

**v0.2**
- Camera service with image capture
- Basic HTTP API for image retrieval

**v0.3**
- WiFi manager with network connectivity
- Remote control interface

**v0.4**
- Storage service (SD card support)
- Local file management

**v1.0**
- Advanced features and optimizations
- Fully composable configuration system

## Project Layout

```
.
├── main/                    # Application entry point
│   ├── app_main.cpp
│   ├── Application.h
│   ├── Application.cpp
│   └── CMakeLists.txt
├── components/
│   ├── power_manager/       # Power and sleep management
│   │   ├── include/
│   │   ├── src/
│   │   ├── CMakeLists.txt
│   │   └── Kconfig
│   └── disabled/            # Inactive components (for future use)
│       ├── camera_service/
│       ├── wifi_manager/
│       └── storage_sdcard/
├── CMakeLists.txt
├── Kconfig
├── sdkconfig.defaults
└── README.md
```

## Component Management

### Adding a new component for v0.2+

```bash
# Restore a disabled component
mv components/disabled/component_name components/

# Update Kconfig to include it
# Update main/CMakeLists.txt REQUIRES list
```

### Creating a new component

Each component must have:
- `include/component_name.h` - Public API
- `src/component_name.cpp` - Implementation
- `CMakeLists.txt` - Build configuration
- `Kconfig` - Configuration options

## Versioning & Releases

Each version adds new components and features:
- v0.x: Beta phase, individual component rollout
- v1.0+: Feature-complete with full configuration flexibility

GitHub releases will include pre-built binaries for different configurations.
