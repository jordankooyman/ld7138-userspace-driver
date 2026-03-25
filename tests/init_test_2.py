#!/usr/bin/env python3
# test_spi_loopback.py
# Hardware: short physical pin 19 (MOSI) to physical pin 21 (MISO) with a jumper
# Expected: all bytes sent are echoed back; any mismatch = SPI hardware problem

import spidev
import sys

SPI_BUS   = 0
SPI_DEV   = 0
SPI_SPEED = 1_000_000   # 1 MHz — conservative for loopback
SPI_MODE  = 0b00        # CPOL=0, CPHA=0 — matches LD7138 serial timing

spi = spidev.SpiDev()
spi.open(SPI_BUS, SPI_DEV)
spi.max_speed_hz = SPI_SPEED
spi.mode = SPI_MODE
spi.bits_per_word = 8

test_bytes = [0x00, 0xFF, 0xA5, 0x5A, 0x01, 0xFE, 0x55, 0xAA]

print(f"SPI loopback test on /dev/spidev{SPI_BUS}.{SPI_DEV} @ {SPI_SPEED//1000} kHz")
print("MOSI (pin 19) must be shorted to MISO (pin 21)\n")

passed = 0
failed = 0
for b in test_bytes:
    rx = spi.xfer2([b])
    status = "PASS" if rx[0] == b else "FAIL"
    if status == "PASS":
        passed += 1
    else:
        failed += 1
    print(f"  TX: 0x{b:02X}  RX: 0x{rx[0]:02X}  [{status}]")

spi.close()
print(f"\nResult: {passed}/{len(test_bytes)} passed")
if failed:
    print("FAIL — check that SPI is enabled in /boot/firmware/config.txt and reboot")
    sys.exit(1)
else:
    print("PASS — SPI hardware is functional. Remove the loopback jumper before next test.")