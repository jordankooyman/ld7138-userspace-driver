# Wiring Documentation: LD7138 OLED Display ↔ Raspberry Pi 4B

**Status:** Initial bring-up wiring — assembled on breadboard.
**Formal schematic:** Pending (hand-drawn sketch exists; KiCad schematic TBD).
**Last updated:** 2026-03-25

---

## Overview

The LD7138 is a 128(RGB)×64 pixel, 65K-color OLED column/row driver IC communicating
over SPI at up to 10 MHz. The Raspberry Pi 4B controls it via the `spidev` kernel driver
(`/dev/spidev0.0`) for data and `libgpiod` for the two GPIO control lines (A0, RSTB).

---

## ⚠️ Critical Hardware Notes

### VCC_C Must Be Supplied Externally at 8–20V

The LD7138 **has no internal boost converter for VCC_C**. The 4.7 µF capacitor on VCC_C
is a decoupling capacitor only — it stabilizes a supply you must provide; it does not
generate one. Without VCC_C at 8–20V the column driver is completely inoperative and the
display will show nothing regardless of correct SPI communication.

**Current status:** VCC_C supply source not yet confirmed. An external boost converter
(e.g. MT3608, XL6009, or equivalent) converting 5V → 12–16V is required.
Target voltage: **16V** (matching the seller reference schematic and nominal datasheet
operating conditions).

### VCC_R (Row Driver Power)

VCC_R is derived from VCC_C by the LD7138's internal row power regulator (register
`0x30h`). The default ratio is VCC_C × 0.65. A 4.7 µF decoupling capacitor is connected
between VCC_R and GND per the datasheet recommendation. **VCC_R should not be driven
externally when the internal regulator is enabled.**

### RPRE / GPRE / BPRE (Pre-Charge Voltage)

These pins are **not connected to any supply rail.** The correct circuit, per the seller
reference schematic, is a reverse Zener diode (cathode to PRE pin, anode to GND) with no
other connection. There is no external voltage source on these pins.

The Zener acts as a voltage clamp: during the pre-charge period of each scan cycle the
LD7138's internal column driver connects each column output back to the PRE pin through
~300–500Ω of internal resistance. The OLED pixel capacitance discharges through that path
toward the PRE pin, and the Zener absorbs the discharge current, clamping the pin at
approximately 2.4V. This becomes the defined reset level for all column outputs before
new pixel data is loaded for the next row, reducing ghosting and improving grey-scale
uniformity.

The datasheet uses `R/G/BPRE = 0V` for all electrical measurements, meaning 0V (GND
direct, no Zener) is a fully valid operating condition — the display functions correctly,
just without the panel manufacturer's specific grey-scale tuning. Connecting PRE directly
to GND is acceptable for bring-up.

> ⚠️ **Wiring error identified and corrected:** PRE pins were previously and incorrectly
> wired to the RPi 5V rail. That connection has been removed. 5V on PRE actively drove
> the pre-charge level above the Zener clamp point and was not the intended circuit.
> Although 5V is within the datasheet's absolute maximum (7V), it was incorrect and has
> been corrected.

### PSEL = VDD

PSEL is tied to VDD (3.3V). This enables the internal logic power regulator, which
generates VDDL internally. A 1–4.7 µF capacitor between VDDL and VSSD is required by
the datasheet (§11.2).

### Capacitor Types

The datasheet recommends electrolytic capacitors for VCC_C and VCC_R (4.7 µF) due to
their lower ESR at high-current transients in the driver rails. Ceramic capacitors have
been substituted for initial bring-up. This is acceptable for testing but may affect
display stability or brightness uniformity at full operating current.

---

## SPI Signal Mapping

The LD7138 uses a write-only SPI interface (D1 = SDIN, D0 = SCLK) in serial mode
(PS pin tied low, IXS pin tied low). The Raspberry Pi 4B's SPI0 bus is used.

| RPi Physical Pin | BCM GPIO | SPI0 Function | LD7138 Pin | Notes |
|:---:|:---:|:---:|:---:|---|
| 19 | BCM 10 | MOSI (SPI0) | SDIN (D1) | Serial data input |
| 23 | BCM 11 | SCLK (SPI0) | SCLK (D0) | Serial clock |
| 24 | BCM 8  | CE0 (SPI0)  | CSB | Chip select, active low; hardware-controlled by spidev |

---

## GPIO Control Lines

These two lines are managed via `libgpiod` (`/dev/gpiochip0`), not by spidev.

| RPi Physical Pin | BCM GPIO | LD7138 Pin | Idle State | Function |
|:---:|:---:|:---:|:---:|---|
| 18 | BCM 24 | RSTB | HIGH | Hardware reset, active low. |
| 22 | BCM 25 | A0   | HIGH | LOW = command register; HIGH = parameter/pixel data |

### RSTB RC Filter

A 10 kΩ pull-up resistor to 3.3V and a 0.1 µF capacitor to ground form an RC filter
on RSTB (time constant ≈ 1 ms). This ensures RSTB rises cleanly at power-on before
the GPIO driver asserts control, preventing a spurious reset glitch. The RC filter is
consistent with common practice for OLED reset lines.

---

## Power Connections

| RPi Physical Pin | Voltage | LD7138 / Circuit Node | Notes |
|:---:|:---:|:---:|---|
| 1 or 17 | 3.3V | VDD | Interface and analog power for LD7138 logic |
| 1 or 17 | 3.3V | PSEL | Tied to VDD to enable internal VDDL regulator |
| 2 or 4  | 5V   | Boost converter input | Input to external boost converter for VCC_C |
| External boost output | 16V | VCC_C | ⚠️ **Not yet connected — required for display operation** |
| 6, 9, 14, 20, 25, 30, 34, 39 | GND | VSSA (×2), VSSD | All ground pins tied to common ground |

---

## Decoupling Capacitors

| Node | Capacitance | Type (as built) | Recommended type | Ref |
|:---:|:---:|:---:|:---:|---|
| VCC_C to VSSA | 4.7 µF | Ceramic (0805) | Electrolytic | Datasheet §3.1 |
| VCC_R to VSSA | 4.7 µF | Ceramic (0805) | Electrolytic, 4.7 µF | Datasheet §3.1 |
| VDDL to VSSD  | 4.7 µF | Ceramic (0805) | 1–4.7 µF, ≥5V      | Datasheet §11.2 |

---

## PRE Pin Passive Components

| LD7138 Pin | Component | Connection | Purpose |
|:---:|:---:|:---:|---|
| RPRE | Zener diode, ~2.4V | Cathode → RPRE, Anode → GND | Clamps pre-charge voltage to 2.4V; absorbs pixel capacitance discharge |
| GPRE | Zener diode, ~2.4V | Cathode → GPRE, Anode → GND | Same as above for green channel |
| BPRE | Zener diode, ~2.4V | Cathode → BPRE, Anode → GND | Same as above for blue channel |

No supply voltage is connected to any PRE pin. For bring-up without the Zener diodes,
connecting all three PRE pins directly to GND is acceptable (equivalent to 0V pre-charge,
which is the datasheet's own measurement reference condition).

---

These LD7138 pins select the communication interface and must be tied to fixed logic
levels. In SPI mode: PS = low, IXS = low.

| LD7138 Pin | Connection | Reason |
|:---:|:---:|---|
| PS  | GND (VSSD) | LOW = Serial interface selected |
| IXS | GND (VSSD) | LOW = SPI selected (not I2C) |
| C80 | GND (VSSD) | LOW = 80-series parallel mode (ignored in serial mode) |
| FSYNC | Floating | Frame sync output; not used in this application |

---

## Software Interface

| Layer | Mechanism | Device Node |
|:---:|:---:|:---:|
| SPI data (SDIN, SCLK, CSB) | Linux `spidev` via `ioctl(SPI_IOC_MESSAGE)` | `/dev/spidev0.0` |
| GPIO control (RSTB, A0) | `libgpiod` v2 (`gpiod_chip_request_lines`) | `/dev/gpiochip0` |

**SPI configuration used:**
- Mode: SPI_MODE_0 (CPOL=0, CPHA=0)
- Speed: 5 MHz (datasheet max write: 10 MHz; conservative for bring-up)
- Bits per word: 8
- Bit order: MSB first

**Enable SPI on RPiOS:**
```
# /boot/firmware/config.txt  (Bookworm)
dtparam=spi=on
```

---

## Known Issues / Open Items

| # | Issue | Status |
|:---:|---|:---:|
| 1 | VCC_C has no 8–20V supply; display column driver is inoperative | **Open — blocks display function** |
| 2 | Boost converter module (MT3608 or equivalent) not yet sourced/wired | Open |
| 3 | Ceramic caps used for VCC_C / VCC_R instead of electrolytic | Low priority; re-evaluate after bring-up |
| 4 | PRE pins were incorrectly wired to 5V; connection removed — see PRE Pin notes | **Corrected** |
| 5 | Formal KiCad schematic not yet created | Planned |

---

## Revision History

| Rev | Date | Description |
|:---:|:---:|---|
| 0.1 | 2026-03-25 | Initial wiring documented from hand-drawn sketch and bring-up session notes |
| 0.2 | 2026-03-25 | Corrected PRE pin wiring: removed incorrect 5V supply connection; documented Zener clamp circuit and bring-up GND alternative. Closed open item #4. |