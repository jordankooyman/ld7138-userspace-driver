# LD7138 Linux Userspace Driver

**Userspace display library for the LDT LD7138 128x128 OLED Controller**

[![Project](https://img.shields.io/badge/ECEN_5713-Final_Project-blue)](https://github.com/cu-ecen-aeld/final-project-assignment-jordankooyman)
[![Platform](https://img.shields.io/badge/Platform-Raspberry_Pi_4B-red)](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/)
[![Build](https://img.shields.io/badge/Build_System-Buildroot-orange)](https://buildroot.org/)

## Overview

This library provides a complete userspace driver for the **LDT LD7138**, a 128(RGB)×128 pixel 65K-color OLED display controller/driver IC. The library uses Linux's `spidev` kernel driver for SPI communication and `libgpiod` for GPIO control, enabling display operation entirely from userspace without requiring a custom kernel module.

**Part of:** [AESD Final Project - LD7138 OLED Driver](https://github.com/cu-ecen-aeld/final-project-assignment-jordankooyman)

## Features (Planned)

- ✅ **Full LD7138 initialization** - Hardware reset, oscillator config, power-on sequence
- ✅ **RGB565 framebuffer** - Software 128×128×16bpp framebuffer with efficient bulk writes
- ✅ **Graphics primitives** - Fill screen, draw pixel, draw rectangle
- ✅ **Portable architecture** - Clean HAL boundary allows porting to Arduino/ESP32
- ✅ **No kernel dependencies** - Uses standard `spidev` and `libgpiod` interfaces

## Hardware Support

### Display IC: LDT LD7138
- Resolution: 128(RGB) × 128 pixels
- Color depth: 65K (RGB565)
- Interface: SPI-compatible serial (up to 10MHz write)
- GRAM: 262,144-bit internal graphics RAM
- Frame rate: 60-120Hz (adjustable)

### Tested Platform
- **Raspberry Pi 4 Model B** (BCM2711 SoC)
- **OS:** Raspberry Pi OS (64-bit) / Buildroot
- **Kernel:** Linux 6.1+ with `spi-bcm2835` and `spidev` enabled

## Repository Structure

```
ld7138-userspace-driver/
├── README.md                    # This file
├── Makefile                     # Build system
├── include/
│   ├── ld7138_hal.h             # HAL layer API
│   ├── ld7138_spi.h             # SPI/GPIO abstraction API
│   └── ld7138_gfx.h             # Graphics layer API
├── src/
│   ├── ld7138_hal.c             # Command encoder, init sequence, GRAM writes
│   ├── ld7138_spi_linux.c       # Linux spidev + libgpiod implementation
│   └── ld7138_gfx.c             # Drawing primitives, framebuffer management
├── demo/
│   └── main.c                   # Example application
├── docs/
│   ├── wiring.md                # RPi4B GPIO pinout
│   └── LD7138_datasheet.pdf     # IC datasheet (reference)
└── tests/
    └── test_colors.c            # Solid color fill test
```

## Quick Start

### 1. Hardware Wiring

Connect the LD7138 to Raspberry Pi 4B SPI0:

| RPi4B Pin | GPIO | Function | LD7138 Pin |
|-----------|------|----------|------------|
| 19        | GPIO10 | MOSI   | SDA (D1)   |
| 23        | GPIO11 | SCLK   | SCL (D0)   |
| 24        | GPIO8  | CE0    | CSB        |
| 22        | GPIO25 | GPIO   | A0 (D/C)   |
| 18        | GPIO24 | GPIO   | RSTB       |
| 1         | 3.3V   | Power  | VDD        |
| 6         | GND    | Ground | VSSD       |

See [`docs/wiring.md`](docs/wiring.md) for complete pinout and power supply details.

### 2. Enable SPI on Raspberry Pi OS

```bash
# Enable SPI interface
sudo raspi-config
# Interface Options → SPI → Yes

# Or edit /boot/config.txt:
echo "dtparam=spi=on" | sudo tee -a /boot/config.txt
sudo reboot

# Verify SPI device exists
ls -l /dev/spidev0.0
```

### 3. Install Dependencies

```bash
# Raspberry Pi OS
sudo apt update
sudo apt install -y build-essential libgpiod-dev git

# Verify libgpiod
pkg-config --modversion libgpiod
```

### 4. Build the Library

```bash
git clone https://github.com/jordankooyman/ld7138-userspace-driver.git
cd ld7138-userspace-driver

# Build library and demo
make

# Output:
# - libld7138.a (static library)
# - demo (example application)
```

### 5. Run the Demo

```bash
# Run as root (required for /dev/spidev and GPIO access)
sudo ./demo

# Expected output:
# [LD7138] Initializing display...
# [LD7138] Hardware reset
# [LD7138] Software reset
# [LD7138] Display ON
# Demo running...
```

## API Usage

### Basic Example

```c
#include "ld7138_hal.h"
#include "ld7138_gfx.h"

int main(void) {
    // Initialize the display
    if (ld7138_init("/dev/spidev0.0", 25, 24) != 0) {
        fprintf(stderr, "Display init failed\n");
        return 1;
    }

    // Fill screen with red
    gfx_fill(RGB565(255, 0, 0));
    ld7138_write_framebuffer();

    // Draw a green rectangle
    gfx_draw_rect(32, 32, 64, 64, RGB565(0, 255, 0));
    ld7138_write_framebuffer();

    // Cleanup
    ld7138_close();
    return 0;
}
```

### Graphics Functions

```c
// Fill entire screen
void gfx_fill(uint16_t color);

// Draw single pixel
void gfx_draw_pixel(int x, int y, uint16_t color);

// Draw filled rectangle
void gfx_draw_rect(int x, int y, int w, int h, uint16_t color);

// RGB565 color packing
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
```

## Cross-Compilation for Buildroot

This library is packaged as a Buildroot external package. See the main project repository for build instructions:

[buildroot-external/package/ld7138/](https://github.com/cu-ecen-aeld/final-project-assignment-jordankooyman/tree/main/buildroot-external/package/ld7138)

## Architecture

The library uses a three-layer architecture for portability:

```
┌─────────────────────────────────────┐
│   Application / Demo (main.c)      │
├─────────────────────────────────────┤
│   Graphics Layer (ld7138_gfx.c)    │  ← Portable
│   - Framebuffer management          │
│   - Drawing primitives               │
├─────────────────────────────────────┤
│   HAL Layer (ld7138_hal.c)         │  ← Portable
│   - LD7138 command encoding         │
│   - Initialization sequence          │
│   - GRAM writes                      │
├─────────────────────────────────────┤
│   SPI/GPIO Abstraction              │  ← Platform-specific
│   (ld7138_spi_linux.c)              │     (Linux spidev + libgpiod)
└─────────────────────────────────────┘
```

Only `ld7138_spi_linux.c` is platform-specific. To port to Arduino/ESP32, replace this file with an equivalent implementation for that platform's SPI and GPIO APIs.

## Troubleshooting

### Display shows nothing / stays dark

1. **Check power supply:** LD7138 requires VDD (1.65-3.6V) AND VCC_C/VCC_R (8-20V). The 3.3V rail powers logic; a separate supply is needed for the OLED panel.
2. **Verify SPI bus:** Run `sudo ./tests/spi_loopback` to confirm SPI is working
3. **Check wiring:** Verify MOSI, SCLK, CSB, A0, RSTB connections
4. **Increase init delay:** Edit `ld7138_hal.c` and increase reset pulse width or post-init delays

### "Permission denied" opening /dev/spidev0.0

- Run with `sudo`, or add user to `spi` group:
  ```bash
  sudo usermod -a -G spi $USER
  # Log out and back in
  ```

### "Failed to open GPIO chip"

- Ensure `/dev/gpiochip0` exists and is accessible
- `libgpiod` version must be ≥1.6
- Run with `sudo` or configure udev rules for GPIO access

## Development Status

| Sprint | Status | Completion |
|--------|--------|------------|
| Sprint 1 | In Progress | ▓░░░░░░░░░░ 10% |
| Sprint 2 | Planned | ░░░░░░░░░░ 0% |
| Sprint 3 | Planned | ░░░░░░░░░░ 0% |

See the [Project Schedule](https://github.com/cu-ecen-aeld/final-project-assignment-jordankooyman/wiki/Schedule) for detailed task breakdown.

## Contributing

This is a solo course project and is not accepting external contributions. However, feedback and bug reports are welcome via [Issues](https://github.com/jordankooyman/ld7138-userspace-driver/issues).

## References

- [LD7138 Datasheet](docs/LD7138_datasheet.pdf) - LDT Inc., Version 1.3
- [Linux spidev Documentation](https://www.kernel.org/doc/Documentation/spi/spidev)
- [libgpiod Documentation](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/)

## License

This project is submitted as coursework for ECEN 5713, University of Colorado Boulder. See course policies for usage terms.

## AI Assistance Disclosure

AI tools (Claude by Anthropic) were used in planning this project's architecture and drafting documentation. All code implementation is original work. See the [AI Assistance Log](https://github.com/cu-ecen-aeld/final-project-assignment-jordankooyman/wiki/Project-Overview#ai-assistance-log) for conversation links.

---

**Author:** Jordan Kooyman ([@jordankooyman](https://github.com/jordankooyman))  
**Last Updated:** March 9, 2026
