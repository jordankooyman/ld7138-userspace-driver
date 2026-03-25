#!/usr/bin/env python3
# test_gpio.py
# Toggles RSTB (BCM 24) and A0 (BCM 25) and reports state
# Use a multimeter on physical pins 18 and 22 to confirm

import gpiod

CHIP      = "/dev/gpiochip0"
RSTB_LINE = 24   # physical pin 18
A0_LINE   = 25   # physical pin 22

chip = gpiod.Chip(CHIP)

# Request lines in batch with output direction and default high
config = {
    RSTB_LINE: gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=gpiod.line.Value.ACTIVE,
    ),
    A0_LINE: gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=gpiod.line.Value.ACTIVE,
    ),
}
request = chip.request_lines(consumer="test_gpio", config=config)

print("GPIO test — measure physical pins 18 (RSTB) and 22 (A0) with a multimeter\n")

for label, line_offset, pin in [("RSTB", RSTB_LINE, 18), ("A0  ", A0_LINE, 22)]:
    print(f"  {label} (pin {pin}): setting HIGH → expect ~3.3V")
    request.set_value(line_offset, gpiod.line.Value.ACTIVE)
    input("    Press Enter after measuring HIGH...")
    print(f"  {label} (pin {pin}): setting LOW  → expect ~0V")
    request.set_value(line_offset, gpiod.line.Value.INACTIVE)
    input("    Press Enter after measuring LOW...")
    request.set_value(line_offset, gpiod.line.Value.ACTIVE)   # leave high (idle state for both)
    print(f"  {label} (pin {pin}): restored HIGH\n")

request.release()
chip.close()
print("GPIO test complete. Both pins left HIGH (RSTB deasserted, A0 = data mode).")