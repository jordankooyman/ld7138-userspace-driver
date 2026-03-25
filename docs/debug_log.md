# LD7138 Bring-Up Debug Log

**Hardware:** LD7138 128(RGB)×64 OLED display, Raspberry Pi 4B
**Interface:** SPI via `/dev/spidev0.0`, GPIO via libgpiod v2
**VCC_C supply:** MT3608 boost converter from RPi 5V rail
**Test file:** `tests/init_test_4.c`

Note: Some chats were performed simulataneously, so sessions are not be strictly sequential and may show overlapping questions or findings; they may also show findings from between chats to validate the accuracy of the suggestions.

---

# Chat 1 (Claude)
# LD7138 OLED Display — Bring-Up Debug Log

**Project:** LD7138 128×64 RGB OLED userspace display library for Raspberry Pi 4B
**Interface:** SPI serial mode (PS=low, IXS=low)
**Driver IC:** LDT LD7138
**Host:** Raspberry Pi 4B, Raspberry Pi OS (Bookworm, 64-bit)
**Development approach:** Native RPiOS for bring-up iteration; Buildroot migration later

---

## Hardware Configuration

### Pin Mapping

| RPi Physical Pin | BCM GPIO | LD7138 Signal | Method |
|:---:|:---:|:---:|:---:|
| 19 | BCM 10 | SDIN (MOSI) | spidev / bitbang |
| 23 | BCM 11 | SCLK | spidev / bitbang |
| 24 | BCM 8 | CSB (chip select, active low) | spidev / bitbang |
| 22 | BCM 25 | A0 (command/data select) | libgpiod |
| 18 | BCM 24 | RSTB (reset, active low) | libgpiod |

### Power

| Rail | Source | LD7138 Pins |
|:---:|:---:|:---:|
| 3.3V | RPi pin 1/17 | VDD, PSEL |
| GND | RPi pin 20 | VSSA (×2), VSSD |
| 14.8V (boost) | External converter | VCC_C |
| (internal) | Derived from VCC_C via register 0x30h | VCC_R |

### Passive Components

| Node | Component |
|:---:|:---:|
| VCC_C to GND | 4.7 µF ceramic |
| VCC_R to GND | 4.7 µF ceramic |
| VDDL to GND | 4.7 µF ceramic |
| RSTB | 10 kΩ pull-up to 3.3V + 0.1 µF to GND |
| RPRE, GPRE, BPRE | Reverse Zener ~2.4V to GND (cathode to pin) |

---

## Session 1 — Initial Wiring and Stage Tests

### Context

Display wired to RPi. Initial test scripts written to stage-validate hardware before
committing to display code. Operating system and development environment set up via
VS Code Remote SSH.

### Tests Performed

**Stage 1 — Pre-flight shell checks**

```bash
grep -i spi /boot/firmware/config.txt
ls -la /dev/spidev*
gpiodetect
gpioinfo gpiochip0 | grep -E "line 24|line 25"
sudo apt install -y libgpiod-dev libgpiod2 gpiod python3-spidev python3-gpiod
```

**Result:** All checks passed after enabling SPI in config.txt and rebooting.
`gpioinfo` did not accept the `gpiochip0` argument directly (minor CLI version
difference); worked with alternative syntax.

**Stage 2 — SPI loopback (MOSI shorted to MISO, physical pins 19 and 21)**

Python script using `python3-spidev`, sending known bytes and verifying echo.

**Result:** All bytes echoed correctly. SPI hardware confirmed functional.

**Stage 3 — GPIO toggle test**

Python script toggling BCM 24 (RSTB) and BCM 25 (A0), verified with multimeter.

**Result:** Several library API changes needed for the installed libgpiod version.
Measured HIGH = 2.88V, LOW = −390mV. The negative low reading is a measurement
artefact from leakage through the RSTB pull-up resistor network; output is correct.

**Stage 4 — LD7138 display init + solid colour fill (C, spidev + libgpiod v1)**

First attempt at full init sequence and 128×128 framebuffer write.

**Result:** Compilation errors — code used libgpiod v1 API; system has v2 installed.
After correcting to v2 API, program ran without errors but display showed nothing.

### Issues Identified / Corrected in This Session

1. SPI not enabled in config.txt — corrected.
2. libgpiod v1 API used in code — corrected to v2.
3. Display dimensions coded as 128×128 — display is actually **128×64**.
4. VCC_C had no high-voltage supply — identified as missing requirement.
5. PRE pins incorrectly connected to 5V — identified for correction.

### Next Steps Identified

- Supply VCC_C at 8–20V via external boost converter.
- Correct PRE pin wiring.
- Rewrite code for libgpiod v2 and correct display geometry.

---

## Session 2 — Hardware Corrections and Voltage Investigation

### Context

Two wiring errors identified and investigated before proceeding with code.

### Issue 1 — VCC_C Supply

**Finding:** The LD7138 has no internal boost converter for VCC_C. The 4.7 µF
capacitor on VCC_C is a decoupling cap only. The seller reference schematic showing
a cap to GND with no source shown represented a module where the boost circuit is
on the PCB, not visible in the IC-level schematic. VCC_C must be externally supplied
at 8–20V. Without it, the column drivers are entirely inoperative.

**Action:** External boost converter connected, providing **14.8V** to VCC_C.

### Issue 2 — PRE Pin Wiring (RPRE, GPRE, BPRE)

**Initial (incorrect) understanding:** PRE pins were believed to need a 5V supply,
based on mixing up VCC_C and PRE in the reference schematic.

**Actual circuit (from schematic):** No supply is connected to PRE. The schematic
shows only a reverse Zener diode (cathode to PRE pin, anode to GND, Vz ≈ 2.4V).
There is no external voltage source on these pins.

**How it works:** During each scan period, the LD7138 internally connects column
outputs back to the PRE pins through ~300–500 Ω internal resistance. Pixel
capacitance discharges through this path. The Zener clamps the PRE pin at 2.4V,
defining the pre-charge reset level for ghost reduction and grey-scale uniformity.
The datasheet uses `R/G/BPRE = 0V` for all measurement conditions, confirming GND
direct (no Zener) is a valid operating alternative.

**Diode substitution question:** Rectifier, switching, fast recovery, and Schottky
diodes cannot substitute for the Zener. They have no defined reverse breakdown
voltage and would leave the pin floating or fail destructively. Valid alternatives:
same-voltage Zener, LDO regulator set to 2.4V, or resistor divider with decoupling
cap. For bring-up, PRE connected directly to GND is acceptable.

**Action:** 5V connection to PRE pins removed. PRE pins connected to GND for
bring-up testing.

### wiring.md Updated

Wiring documentation created and corrected to reflect:
- VCC_C external supply requirement and status
- Corrected PRE pin circuit description (Zener clamp, no supply)
- Wiring error flagged and marked corrected

---

## Session 3 — Code Revision 1: libgpiod v2 + 128×64 + Init Fix

### Code File: `test_ld7138.c` (spidev + libgpiod v2)

### Changes from Original

**1. libgpiod v1 → v2 API rewrite**

v2 replaced individual line handles with a request-based model:
- `gpiod_chip_get_line()` → `gpiod_chip_request_lines()`
- `gpiod_line_request_output()` → `gpiod_line_settings_set_direction()`
- `gpiod_line_set_value()` → `gpiod_line_request_set_value()`

**2. Display size corrected to 128×64**

DispSize command `0x07h` Y-end corrected from `0x7F` (127) to `0x3F` (63).
Framebuffer size corrected from 32,768 bytes to **16,384 bytes** (128×64×2).

**3. Oscillator wake-up added**

Register `0x03h` (DSTBYON/OFF) defaults to `0x01` after reset — oscillator halted,
standby active. Command `write_cmd(0x03); write_data_byte(0x00)` added to start
the oscillator. Without this, no scanning occurs.

**4. GRAM write command corrected (0x08 → 0x0A + 0x0C)**

The previous code called `write_cmd(0x08)` intending to begin a pixel write.
`0x08` is `IF_BUS_SEL` (bus width selector), not a GRAM write trigger.

Correct two-step GRAM write sequence:
- `0x0A` MBOXSize — defines X/Y read/write address window in GRAM and resets
  the write pointer. Distinct from `0x07` DispSize (which configures driver
  output range, not GRAM addressing).
- `0x0C` DataWrite/Read — opens pixel stream; GRAM pointer auto-increments
  until MBox boundary.

**Test result:** Display still showed nothing.

---

## Session 4 — Code Revision 2: Column Drive Current

### Finding

Registers `0x0Eh` (DotCurrent) and `0x0Fh` (PeakCurrent) both default to `0x00`
= 0.0 µA. The LD7138 is a **constant-current driver**. At zero current, OLED
elements cannot emit light regardless of GRAM data, addressing, or scan timing
being correct.

These registers had not been written anywhere in the init sequence.

### Commands Added to Init

**`0x0Eh` — DotCurrent** (sustained pixel drive current)

Each channel's 8-bit value is split across two nibble-wide parameter bytes:
- Param 2N+0: D[3:0] = I[7:4] (high nibble)
- Param 2N+1: D[3:0] = I[3:0] (low nibble)

Set to 100 µA per channel (0x64 → `0x06, 0x04` per channel):

```
0x0E → 0x06, 0x04, 0x06, 0x04, 0x06, 0x04  (R, G, B)
```

**`0x0Fh` — PeakCurrent** (peak boost drive during scan peak-boot period)

Single 6-bit byte per channel, 16 µA per step. Set to 0x10 = 256 µA:

```
0x0F → 0x10, 0x10, 0x10  (R, G, B)
```

**Test result:** Display still showed nothing.

---

## Session 5 — Root Cause Identified: CSB Protocol Violation (spidev)

### Finding — Fundamental Protocol Violation

The LD7138 serial interface specification (datasheet §4.3, §11.1) states:

> "The high level of CSB signal clears the internal buffer of SDA and SCL Counter."

The correct protocol requires **CSB to remain LOW** for the entire transaction:
the command byte AND all following parameter bytes. CSB going high at any point
resets the IC's internal SCL counter, causing it to discard any in-progress
command.

**The Linux `spidev` driver deasserts CE0 (CSB goes HIGH) after every `ioctl()`
call.** This is not a bug — it is the designed behaviour of the driver. Since each
command byte and each parameter byte was sent as a separate `ioctl()` call, CSB
pulsed high between every byte. The IC never received a single complete command.

**This protocol violation was present from the very first test and explains why
no combination of init sequence changes had any effect — the IC has remained in
its post-reset default state throughout all testing.**

### Why spidev Cannot Fix This

The `spidev` driver does support `cs_change = 0` to keep CS asserted across
transfers within a single `ioctl()` using the multi-transfer array form, but this
only works within a single `ioctl(SPI_IOC_MESSAGE(n))` call with n transfers.
Constructing a single ioctl that spans a variable-length command header plus up to
16,384 pixel bytes, with A0 toggling between them, is not practical with spidev's
fixed transfer structure. Bitbanging is the correct solution.

### Solution: Bitbanged SPI (new file `test_ld7138_bb.c`)

All five signals (MOSI, SCLK, CSB, A0, RSTB) controlled via libgpiod. SPI
protocol implemented in software with a transaction model matching the datasheet:

```
tx_begin(cmd)     → CSB low, A0 low, send command byte
tx_param(p)       → A0 high (CSB still low), send one parameter byte
tx_params(buf, n) → A0 high (CSB still low), send n parameter bytes
tx_end()          → CSB high — transaction complete
```

CSB stays low for the entire command + all its parameters. The pixel fill extends
this same transaction from the `0x0C` command byte through all 16,384 data bytes
before CSB is released.

### One-Time Config Change Required

Because spidev claims BCM 8/10/11 when enabled, it must be disabled to use these
pins as plain GPIO:

```bash
# In /boot/firmware/config.txt, comment out or remove:
# dtparam=spi=on

# Then reboot:
sudo reboot
```

### Build and Run

```bash
gcc -O2 -o test_ld7138_bb test_ld7138_bb.c -lgpiod
sudo ./test_ld7138_bb
```

**Expected:** Screen fills solid RED (2s) → solid GREEN (2s) → solid BLUE (2s).

**Test result:** Pending.

---

## Complete Init Sequence (Current — `test_ld7138_bb.c`)

| Step | Command | Description |
|:---:|:---:|---|
| 1 | `0x01` | Software Reset — reinitialise all registers |
| 2 | `0x03, 0x00` | Exit standby, start oscillator (P0=0) |
| 3 | `0x02, 0x00` | Display OFF while configuring |
| 4 | `0x04, 0x02` | OSC Control — internal RC, 90Hz frame rate |
| 5 | `0x07, [8 bytes]` | DispSize — 128 cols × 64 rows |
| 6 | `0x05, 0x00` | Write Direction — RGB order, L→R T→B |
| 7 | `0x06, 0x00` | Scan Direction — row address min→max |
| 8 | `0x30, 0x04` | Internal row regulator — EN=1, VCC_R = VCC_C × 0.65 |
| 9 | `0x0E, [6 bytes]` | DotCurrent — 100 µA per R/G/B channel |
| 10 | `0x0F, [3 bytes]` | PeakCurrent — 256 µA peak per R/G/B channel |
| 11 | `0x02, 0x01` | Display ON (P0=1) |

### Pixel Write Sequence (per frame)

| Step | Command | Description |
|:---:|:---:|---|
| 1 | `0x0A, [8 bytes]` | MBOXSize — set GRAM write window (0,0)→(127,63) |
| 2 | `0x0C, [16384 bytes]` | DataWrite — pixel stream, RGB565, MSB first, CSB held low throughout |

---

## Errors and Corrections Summary

| # | Error | Effect | Corrected |
|:---:|---|---|:---:|
| 1 | SPI not enabled in config.txt | `/dev/spidev0.0` absent | Session 1 |
| 2 | libgpiod v1 API in code (v2 installed) | Compile errors | Session 1 |
| 3 | Display coded as 128×128, actual is 128×64 | Wrong DispSize, double framebuffer | Session 3 |
| 4 | VCC_C had no supply | Column drivers inoperative | Session 2 |
| 5 | PRE pins wired to 5V | Incorrect pre-charge level | Session 2 |
| 6 | Oscillator not started after reset (`0x03`) | No scan output | Session 3 |
| 7 | `0x08` (IF_BUS_SEL) used instead of `0x0A` + `0x0C` | Pixel data never written to GRAM | Session 3 |
| 8 | DotCurrent and PeakCurrent left at 0 µA default | Zero column drive current, no light | Session 4 |
| 9 | spidev deasserts CSB between every byte | IC's SCL counter reset; no command ever landed | Session 5 |

---

## Open Items

| # | Item | Priority |
|:---:|---|:---:|
| 1 | Confirm bitbang test produces visible output | **Immediate** |
| 2 | If display lights up but colours wrong, tune R/G/B current levels in `0x0Eh` independently | After bring-up |
| 3 | Determine whether `spidev` can be recovered for production use (e.g. single large ioctl with cs_change), or if bitbang / a thin kernel shim is required long-term | After bring-up |
| 4 | Migrate confirmed working init sequence into `ld7138_spi_linux.c` and `ld7138_hal.c` per project architecture | Sprint 1 Issue #3/#4 |
| 5 | Document final confirmed init sequence and any deviations from datasheet in code comments | Sprint 1 Issue #4 DoD |
| 6 | Formal KiCad schematic | Planned |
| 7 | Replace ceramic caps on VCC_C/VCC_R with electrolytic if stability issues arise | Low priority |
| 8 | Add Zener diodes to PRE pins once bring-up is confirmed | Low priority |

---

## File Inventory

| File | Description | Status |
|---|---|:---:|
| `test_ld7138.c` | spidev + libgpiod v2 test (all revisions) | Superseded by CSB finding |
| `test_ld7138_bb.c` | Bitbang SPI test — correct CSB protocol | **Current** |
| `wiring.md` | Hardware wiring documentation | Rev 0.2 |
| `debug_log.md` | This file | Living document |


---

# Chat 2 (GitHub Copilot, GPT-5.3-Codex)

## Session 1 — Source and Bring-Up Path Review

### Context

After repeated "no display activity" results, the code path and test coverage were
re-checked to ensure debugging was focused on the true blocker.

### Files/Tests Reviewed

- `tests/init_test_4.c` (active bring-up test)
- `tests/init_test_1.sh` (pre-flight hardware/software checks)
- `tests/init_test_2.py` (SPI MOSI↔MISO loopback)
- `tests/init_test_3.py` (GPIO A0/RSTB toggle check)
- `docs/wiring.md` (wiring assumptions and rail notes)

### Findings

1. Staged test flow is reasonable and covers foundational hardware checks.
2. Current C test code structure is broadly coherent for bring-up.
3. Previously identified mismatch risk (MBox window vs payload length) was noted as a
  likely failure contributor in earlier revisions and should always be validated.

### Result

No single software syntax/runtime failure explained a fully dark display. Focus shifted
to analog rail behavior and register-effect validation.

### Suggested Next Steps (from this phase)

1. Keep deterministic rebuild/run procedure for every change.
2. Keep GRAM window and byte count exactly matched in every test variant.
3. Verify that critical power-control commands produce measurable rail changes.

---

## Session 2 — VCC_R Behavior Investigation

### User Measurements Reported

- `VCC_C = 16.0V`
- `VCC_R` observed around `1.31V` to `1.44V`, then slowly decaying
- No visible activity at any point on the display

### Interpretation

With internal row regulator enabled and ratio set to 0.65, expected:

`VCC_R ≈ 0.65 × VCC_C = 10.4V` (for `VCC_C = 16.0V`)

Measured `VCC_R ≈ 1.3–1.4V` strongly suggests row rail is not being regulated to target
under real operating conditions.

### Primary Hypotheses Raised

1. Register writes are not being accepted as intended on the actual bus transaction level.
2. Interface mode pins (`PS`, `IXS`, `C80`) are not at required logic levels at runtime.
3. `VCC_R` node is miswired, excessively loaded, or probed at a non-equivalent point.
4. Module pinout may differ from bare-IC assumptions.

### Suggested Next Steps (from this phase)

1. Validate mode pins at runtime with a meter:
  - `PS = LOW`, `IXS = LOW`, `C80 = LOW`
2. Scope/LA capture of `RSTB`, `A0`, `CSB`, `SCLK`, `MOSI` during init.
3. Specifically verify `0x30` command + `0x14` parameter on bus with correct `A0` states.
4. Power-off resistance checks from `VCC_R` to GND with panel connected/disconnected.
5. Continuity check from physical panel `VCC_R` pin to probe point and decoupling node.

---

## Session 3 — External VCC_R Question

### Question

Could `VCC_R` be supplied externally instead of using the internal regulator?

### Guidance Provided

Yes, **only** if datasheet mode explicitly supports external `VCC_R` and internal row
regulator is disabled first.

### Safety Constraints

1. Never drive `VCC_R` externally while internal regulator is enabled.
2. Stay within datasheet operating limits (and below abs max at all times).
3. Use current-limited bench supply for first injection tests.
4. Ramp voltage slowly while monitoring current and IC temperature.

### Suggested A/B Isolation Test

1. Run one test with internal regulator enabled and no external `VCC_R`.
2. Run one test with internal regulator disabled and controlled external `VCC_R`.
3. Compare `VCC_R` stability, current draw, and display behavior to isolate root cause.

---

## Session 4 — Consolidated Technical Position (Copilot)

### What Was Confirmed

1. User is manually compiling each run (latest binary concern dismissed).
2. `VCC_C` supply is present at expected high-voltage level.
3. The key unresolved symptom is `VCC_R` far below expected regulated value.

### What This Implies

The dominant blocker is likely in one of these categories:

1. Command acceptance/protocol-level transaction integrity.
2. Row-rail loading/short/miswire/damaged-path condition.
3. Module pinout mismatch versus assumed net names.

### Immediate Priorities

1. Complete static electrical checks on `VCC_R` before further long runtime.
2. Validate actual command transactions electrically (not only in source code).
3. Re-test only after row rail path is confirmed stable and within expected range.

---

## Chat 2 Test Summary Table

| Area | Test / Check | Result | Next Step |
|---|---|---|---|
| Build process | Manual GCC rebuild each run | Confirmed by user | Continue deterministic rebuild/run |
| Pre-flight scripts | `init_test_1.sh`, `init_test_2.py`, `init_test_3.py` reviewed | Foundational checks appropriate | Keep as baseline sanity suite |
| Init logic review | `init_test_4.c` inspected for command flow | No single compile/runtime blocker found | Verify physical command acceptance on bus |
| VCC_C rail | Metered on hardware | Present at target high voltage | Maintain within safe debug envelope |
| VCC_R rail | Metered on hardware | ~1.3–1.4V, decaying, no display | Treat as primary fault indicator |
| External VCC_R feasibility | Design/safety analysis | Conditionally possible | Use strict A/B test with regulator mode separation |

---

## Current Open Items From Chat 2

1. Confirm mode pins are fixed at correct logic levels during operation.
2. Confirm `0x30/0x14` transaction on bus with correct command/data framing.
3. Eliminate `VCC_R` short/load/pin-mapping issues via resistance and continuity tests.
4. Only after rail stability is confirmed, resume display output bring-up tests.


---

# Chat 3 (Claude)

## Context

Picked up from state left by Chat 1 and Chat 2. Basic code infrastructure was in place:
libgpiod v2 API, SPI via `spidev`, init sequence including DSTBY clear (`0x03/0x00`),
DotCurrent (`0x0E`), and PeakCurrent (`0x0F`). Display still showing no output.
VCC_C not yet connected. PRE pins incorrectly wired.

---

## Session 1 — Documentation Corrections (wiring.md)

### Changes Made

**VCC_C section:**
Updated from "not yet connected" to reflect MT3608 boost converter supplying **14.8V**
(later raised to 16V, then reduced to 10V for second module bring-up).

**PRE pins section:**
Added explicit documentation: all three PRE pins (RPRE, GPRE, BPRE) connected directly
to GND. Zener diode clamp circuit planned but not yet installed.

**Power connections table:**
Row updated from `⚠️ Not yet connected` → `MT3608 output | 14.8V | VCC_C`.

**Known issues:**
Items for VCC_C-no-supply and boost-converter-not-sourced marked **Resolved**.
New item added: PRE pins at GND direct, Zener clamp pending.

### Suggested Next Steps

- Connect MT3608 output to VCC_C and confirm voltage at the ZIF breakout pads.
- Confirm PRE pins at GND before next power-on.

---

## Session 2 — CSB Protocol Claim Investigation

### Claim (from external AI session)

> CSB must remain LOW for an entire multi-byte transaction. `spidev` deasserts CSB
> after every `ioctl()` call, resetting the IC's SCL counter and destroying every
> command. All previous test runs failed entirely due to this protocol violation. The
> fix requires manual GPIO-controlled CSB held low across each command + parameters.

### Investigation

Cross-referenced against the LD7138 datasheet AC characteristics table. The datasheet
defines the `Tcsbh` parameter: **CSB high pulse width, minimum 30 ns**. This parameter
explicitly specifies the timing for CSB going HIGH between bytes — it would not exist
if CSB were required to remain low for an entire transaction.

The IC uses the **A0 line** — not CSB state — to distinguish command bytes
(A0 LOW) from parameter/data bytes (A0 HIGH). CSB going high between bytes is
completely benign provided the minimum 30 ns pulse width is met; `spidev` at 5 MHz
holds CSB high for many microseconds between bytes, comfortably exceeding this.

### Result

**Claim was incorrect and refuted.** No code change made. The existing `spidev`
per-byte CSB toggling is correct and fully compliant with the datasheet. This claim
did not identify the root cause of the blank display.

---

## Session 3 — VCC_R Voltage Investigation

### Tests and Measurements

| Config | GPIO_RSTB | GPIO_A0 | VCC_R behaviour | IC temperature |
|--------|-----------|---------|-----------------|----------------|
| A (original) | BCM 24 | BCM 25 | Jumps ~1.44V during code, settles ~1.36V, slow decay after code stops | Cool |
| B (swapped) | BCM 25 | BCM 24 | Continuously decays during and after code, no peak | Gets warm |

- VCC_C confirmed **16.08V** at ZIF breakout pads with MT3608 connected.
- VCC_R expected at VCC_C × 0.65 = **10.4V**. Actual maximum observed: **~1.44V**.
- After VCC_C raised to 16V: VCC_R peaked at **1.44V** then settled to ~1.36V.
- Ribbon cable reseated once: VCC_R briefly spiked to **~2V** then decayed to 0V.
- Displayed continued blank throughout all tests.

### Key Observation

Config B produces autonomous IC activity **after the test code stops** — VCC_R
continues to drain slowly without any SPI activity. This proves the IC's oscillator is
running and the IC is scanning rows, powered entirely from its own supplies.

Config A shows no autonomous activity after code stops — IC is in standby throughout.

### Interpretation

Config A: every `write_cmd()` call pulses RSTB (via misassigned BCM25=A0), resetting
the IC on every command byte. DSTBY is never cleared. IC stays in standby, row
regulator never enables, IC stays cool.

Config B: A0 and RSTB are correctly assigned. Commands reach the IC, DSTBY is cleared,
oscillator starts — but VCC_R never reaches operating voltage because the row regulator
register was set incorrectly (see Session 7).

---

## Session 4 — GPIO A0 / RSTB Physical Swap

### Root Cause

Physical wiring had **BCM24 → A0** and **BCM25 → RSTB**, opposite to the original
code definitions (`GPIO_RSTB=24, GPIO_A0=25`).

In Config A (original), `write_cmd()` set `GPIO_A0` (=BCM25, physically RSTB) LOW
before each SPI byte, asserting hardware reset on the IC with every command. No
command sequence could complete.

### Fix Applied

```c
#define GPIO_RSTB   25   /* BCM 25 = physical pin 22 */
#define GPIO_A0     24   /* BCM 24 = physical pin 18 */
```

Note: the `printf` label on line 414 still reads "pin 18" for RSTB and "pin 22" for
A0 — this is a cosmetic error in the log output only, not a functional defect.

### Suggested Next Steps

- Confirm RSTB/A0 physical wiring in `wiring.md` and update GPIO defines there.
- Continue diagnosis with Config B (correct mapping).

---

## Session 5 — Ribbon Cable and ZIF Connector Investigation

### Ribbon Cable Pinout (confirmed by user)

| Pin | Signal  | Pin | Signal |
|-----|---------|-----|--------|
| 1   | VSSA    | 9   | SCLK   |
| 2   | VDDL    | 10  | SDIN   |
| 3   | VDD     | 11  | FSYNC  |
| 4   | PSEL    | 12  | PRE    |
| 5   | VSSD    | 13  | VCC_C  |
| 6   | RSTB    | 14  | VCC_R  |
| 7   | CSB     | 15  | VSSA   |
| 8   | A0      |     |        |

### Resistance Measurements (power off, to breadboard GND)

| Pin | Signal | Measured | Interpretation |
|-----|--------|----------|----------------|
| 13  | VCC_C  | ~100 kΩ  | Decoupling cap + IC internal path |
| 14  | VCC_R  | 5.9 MΩ   | Regulator output, off-state — normal |
| 15  | VSSA   | ~15 Ω    | Initially appeared alarming |

### Investigation — Pin 15 (VSSA) ~15 Ω

The ~15 Ω reading was initially suspected to indicate a short on a power pin.
After obtaining the ribbon cable pinout, pin 15 = VSSA (analog ground). The 15 Ω is
entirely accounted for by DuPont wire + breadboard contact resistance: when pins 1
(also VSSA) and 15 were re-measured via the ZIF socket at the breakout board headers,
both read ~4.7–5.5 Ω against a random ground wire on the breadboard measuring ~8 Ω.
All three readings are consistent — the ZIF contact quality is uniform and good.

**There is no short on any power pin.** The ZIF connector contact is not the fault.

### VCC_R Behaviour with Cap Disconnected from GND

With the 4.7 µF VCC_R decoupling cap removed from the GND connection:
- Config A: VCC_R minimum **~0.07V** (standby, no regulator activity)
- Config B: VCC_R maximum **~1.2V** (regulator trying to run, no cap storage)

At 1.2V maximum with no cap, the row regulator is current-limited before reaching
its target (6.5V). The lack of cap is not the cause — a healthy regulator would
reach 6.5V on an unloaded output almost instantly.

### Capacitor Type Test

Decoupling caps swapped from ceramic to electrolytic (matching a reference schematic).
**No change in circuit behaviour.** Cap type is not relevant to the fault.

### Breadboard Fault Verification

With display disconnected from ZIF:
- VCC_R breadboard node measured **open circuit** with cap leg disconnected from GND.
- With cap reconnected: 3–14 MΩ — normal ceramic/electrolytic self-leakage (τ ≈ 10 MΩ
  × 4.7 µF ≈ 47 s). The breadboard has no wiring fault on the VCC_R net.

The VCC_R decay with display disconnected is cap self-discharge, not a fault.

### Suggested Next Steps

- Root cause of VCC_R not reaching operating voltage must be in the IC register
  configuration, not in the ZIF connector or breadboard wiring.
- Verify register 0x30h (VCC_R_SEL) value and bit layout against the full datasheet
  description (§5.2 command description, register 20).

---

## Session 6 — IF_BUS_SEL Register (Independent Bug)

### Finding

Register 0x08h (IF_BUS_SEL) defaults to `0x00` = 6-bit interface bus mode. The code
sends pixel data as 2 bytes per pixel (RGB565). In 6-bit mode the IC interprets this
as 3 bytes per pixel, corrupting every pixel and misaligning the entire GRAM write.

### Fix Applied

Added to init sequence immediately after DSTBY exit:

```c
write_cmd(0x08);
write_data_byte(0x01);   /* 8-bit I/F bus */
```

This bug would have prevented correct display output even if VCC_R were working.

---

## Session 7 — VCC_R_SEL Register EN Bit (Root Cause of No VCC_R)

### Register 0x30h Full Description (datasheet p. 34)

| Bit | 7 | 6 | 5 | **4** | 3 | 2  | 1  | 0  | Default |
|-----|---|---|---|-------|---|----|----|----|---------|
|     | — | — | — | **EN**| — | D2 | D1 | D0 | **04h** |

D[2:0] voltage ratio table:

| D[2:0] | VCC_R output |
|--------|-------------|
| 000    | VCC_C × 0.85 |
| 001    | VCC_C × 0.80 |
| 010    | VCC_C × 0.75 |
| 011    | VCC_C × 0.70 |
| 100    | VCC_C × 0.65 |

Default `0x04` = `0b00000100` → **EN=0 (disabled)**, D=100 (0.65× ratio selected but
regulator off).

The code was sending `write_data_byte(0x04)` with a comment incorrectly stating
"EN=1". Bit 4 is EN, not bit 2. The row regulator was **never enabled** across all
previous test runs.

Consequence: with DSTBY=0 (oscillator running, row scanning active) but EN=0, the
datasheet requirement *"VCC_R must be connected to an external high voltage source"*
was violated on every Config B run. The row driver was scanning with no VCC_R supply,
stressing the row driver transistors over repeated runs.

### Fix Applied

```c
write_cmd(0x30);
write_data_byte(0x14);   /* EN=1 (bit4=1), D[2:0]=100 (bit2=1) → VCC_R = VCC_C × 0.65 */
```

### Init Ordering Fix (applied simultaneously)

Row regulator enable moved to **immediately after DSTBY exit**, before any other
registers, with a 50 ms wait. This ensures VCC_R is at operating voltage before the
IC begins scanning:

```c
write_cmd(0x03); write_data_byte(0x00); sleep_ms(10);  // exit standby

write_cmd(0x30); write_data_byte(0x14); sleep_ms(50);  // enable row reg FIRST

// ... all other registers follow ...
```

Without this ordering, each init cycle briefly runs with the oscillator active and
no VCC_R, violating the datasheet requirement.

---

## Session 8 — First IC Failure (Hardware Event)

### Setup

0x30 parameter changed from `0x04` → `0x14`. VCC_C = 16V. All previous Config B
test runs had been run with EN=0 and no external VCC_R supply.

### Observed

- VCC_R rose to ~6V, continued climbing to **~16V** (= VCC_C).
- IC became very hot and **sparked**. Burned out.
- Display remained blank throughout.

### Failure Analysis

Prior Config B runs had already damaged the row regulator's output transistor by
running the row driver with no VCC_R supply (violating datasheet requirement). When
EN=1 was finally written, the pre-damaged transistor failed **shorted** (drain-source),
directly connecting VCC_R to VCC_C. VCC_R rose to 16V. The row driver dissipated
(16V − OLED_Vf) × scan_current across all row paths simultaneously and burned out.

**Note:** 16V is within the datasheet absolute maximum for VCC_R (20V). The failure
was caused by a degraded transistor, not a direct single-event spec violation.

### Corrective Actions

- Replaced display module (new IC + new panel).
- VCC_C reduced from 16V to **10V** for reduced fault energy during bring-up.
- Init ordering fix applied (Session 7).

---

## Session 9 — Second Module: VCC_R = 4.0V, Immediate Overheating

### Setup

- New LD7138 COG module (new IC + new OLED panel).
- VCC_C = 10V (MT3608 setpoint reduced from 16V).
- Code: 0x30 = `0x14`, row reg enabled immediately after DSTBY.

### Measurements

| Measurement | Value | Expected | Notes |
|-------------|-------|----------|-------|
| VCC_C (at ZIF pad, IC running) | **10.0V stable** | 10V | MT3608 not sagging under load |
| VCC_R (settled) | **4.0V** | 6.5V | 0.40× ratio — no valid D[2:0] matches 0.40× |
| VCC_R (peak at startup) | **4.2V** | 6.5V | Decays as IC load increases |
| IC temperature | **Very hot** | Warm | Heats immediately when 0x30=0x14 is written |

### Key Observations

1. **VCC_C does not sag.** Previously suspected; confirmed not the cause when user
   measured VCC_C under load. MT3608 holds 10V stably.

2. **Heating is immediate.** IC heats as soon as the row regulator is enabled
   (`write_cmd(0x30); write_data_byte(0x14)`), not when display turns ON or pixel
   data is sent. The fault exists before the display is scanning pixels.

3. **Heating persists after code stops.** The IC continues to be hot in the idle
   state after the test program exits. This means the fault is a sustained static
   load, not a transient from the init sequence.

4. **Only full power removal resets the IC.** Disconnecting and reconnecting VDD
   (3.3V) alone did not clear the IC state. Removing both VDD and GND (i.e., all
   power including VCC_C path) was required. This is consistent with VCC_C powering
   internal circuits independently of VDD.

5. **4.0V / 10.0V = 0.40× ratio.** No D[2:0] setting produces 0.40×. The regulator
   is in current-limited dropout, not regulating. The load on VCC_R exceeds the
   regulator's current capability.

### Power Dissipation Estimate

```
P_regulator = (VCC_C − VCC_R) × I_load = (10V − 4V) × I_load = 6V × I_load
```

For the IC to heat immediately to "very hot," I_load ≈ 50–100 mA, implying a
~40–80 Ω resistive path from VCC_R to GND internal to the display module.

### Possible Causes

| Hypothesis | Evidence For | Evidence Against | Status |
|------------|-------------|-----------------|--------|
| Breadboard VCC_R wiring fault | (none) | Breadboard previously verified clean; VCC_R = open circuit with display disconnected | **Eliminated** |
| MT3608 sagging under load | (none) | User confirmed VCC_C stable at 10.0V during IC operation | **Eliminated** |
| ZIF connector contact fault | (none) | Pin 1/15 resistance confirms uniform contact quality; IC receives SPI commands correctly | **Eliminated** |
| New module internal fault (IC or panel) | Heating starts at EN=1 before display ON; only full power reset works; 5.9 MΩ measurement was on old module, not new | (unconfirmed) | **Primary suspect** |

### Suggested Next Steps

1. **Power off completely. Do not run code again until fault is isolated.**

2. **Resistance check with power off — compare display connected vs disconnected:**

   | State | Measure | Healthy result | Fault result |
   |-------|---------|----------------|--------------|
   | Display disconnected from ZIF | Pin 14 (VCC_R header) to GND | ~MΩ (cap leakage) | — |
   | Display connected, power off | Pin 14 (VCC_R header) to GND | ~MΩ (unchanged) | < 100 kΩ → internal short |

   Any significant drop in resistance when display is connected implicates the module.

3. **If module is confirmed faulty:** Return/replace. Source from a different batch if
   possible.

4. **Before powering any future replacement:** Always measure VCC_R → GND resistance
   with display connected and power off first. < 100 kΩ = do not power.

5. **Once a healthy module is confirmed (VCC_R resistance normal):** Run test code.
   VCC_R should reach ~6.5V within the 50 ms wait after writing 0x30. Confirm before
   proceeding to display ON.

6. **After display produces output:** Raise VCC_C to ~13V so VCC_R = 8.45V (above the
   8V operating minimum). Then narrow diagnostic settings back to 64-row panel range.

---

## Register Corrections Summary

| Register | Parameter | Wrong value sent | Correct value | Effect of error |
|----------|-----------|-----------------|---------------|-----------------|
| `0x08h` IF_BUS_SEL | Bus width | `0x00` (default, 6-bit) | `0x01` (8-bit) | All pixel data corrupted; stream misaligned |
| `0x30h` VCC_R_SEL | EN bit (bit 4) | `0x04` (EN=0) | `0x14` (EN=1) | Row regulator never enabled; VCC_R collapses; datasheet requirement violated on every run |

---

## Hardware Incidents

| # | Date | Description | Root Cause |
|---|------|-------------|------------|
| 1 | 2026-03-25 | First IC burned out (VCC_R → 16V, sparked) | Row regulator transistor pre-damaged by repeated EN=0 runs violating datasheet VCC_R supply requirement; transistor failed shorted when EN=1 first applied |

---

## Current Code State

| Item | Value | Notes |
|------|-------|-------|
| `GPIO_RSTB` | BCM 25 (physical pin 22) | Corrected from original BCM 24 |
| `GPIO_A0` | BCM 24 (physical pin 18) | Corrected from original BCM 25 |
| `DISPLAY_H` | 128 | Diagnostic — covers full IC row range pending panel row identification |
| DispSize Yend | 0x7F (127) | Diagnostic |
| MBoxSize Yend | 0x7F (127) | Diagnostic |
| IF_BUS_SEL | 0x01 (8-bit) | Fixed |
| VCC_R_SEL | 0x14 (EN=1, D=100, ×0.65) | Fixed; moved to immediately after DSTBY exit |
| DotCurrent | 100 µA per channel | |
| PeakCurrent | 256 µA per channel | |

---

# Chat 4 (Claude)

## Session 1 — Initial Bring-Up Attempt

### Observed Symptoms

- No light or activity on the display at any point.
- `VCC_R` measured at the breadboard: **~1.32V idle**, spiking to **~1.44V** at the start of
  code execution, dropping back to **~1.36V** by the time `Filling: GREEN` is printed.
- When `GPIO_RSTB` and `GPIO_A0` pin definitions were swapped in the code: **no spike**,
  VCC_R only drifted downward at a rate consistent with capacitor self-discharge.

### Hypothesis 1 — Reversed Electrolytic Capacitors

**Reasoning:** Electrolytic capacitors are polarised; ceramics are not.  The two decoupling
caps on VCC_C and VCC_R had been changed from ceramic to electrolytic.  A reversed
electrolytic would short the rail through its forward-biased junction, dragging the voltage
down.

**Test:** The caps were inspected and swapped.

**Result:** Behaviour was **identical** before and after the change — VCC_R remained at
~1.32V regardless of cap orientation.  The capacitor change did not affect the symptom,
ruling out cap polarity as the root cause at this stage.

### Hypothesis 2 — VCC_C Not Reaching the IC

**Reasoning:** `VCC_R = VCC_C × ratio`.  If the ratio is 0.65 (the code's configured value),
then `VCC_R = 1.32V` implies `VCC_C ≈ 1.32 / 0.65 ≈ 2.0V` — nowhere near the expected
14.8–16V from the MT3608.

**Test:** Measure VCC_C directly at the ZIF connector pads.

**Result:** VCC_C confirmed at **16.08V** at the ZIF pads. MT3608 is delivering correct
voltage. VCC_C is not the problem.

**Follow-on observation:** When VCC_R was pre-charged to 16V externally and the code was run
with correct pin ordering, VCC_R **actively dropped** to ~1.36V. With RSTB/A0 swapped, the
cap only drifted down at the passive leakage rate. This confirmed the IC is receiving SPI
commands and is actively modifying VCC_R behaviour.

### Root Cause Found — Register 0x30h EN Bit

**Analysis of register 0x30h (VCC_R_SEL) from the datasheet (p. 19, p. 34):**

| Bit | 7 | 6 | 5 | **4** | 3 | 2 | 1 | 0 | Default |
|-----|---|---|---|-------|---|---|---|---|---------|
| Name | - | - | - | **EN** | - | D2 | D1 | D0 | **0x04** |

- **EN = 1**: internal row scan regulator enabled (derives VCC_R from VCC_C).
- **EN = 0**: internal row scan regulator **disabled**. Datasheet states: *"VCC_R pin must be
  connected to an external voltage source or VCC_C."*
- Default value `0x04` = `0b00000100` = **EN=0** (disabled), D[2:0]=100 (ratio 0.65).
- The code was sending `0x04`, which **explicitly disabled the internal regulator**.
- The code comment incorrectly stated "Default (0x04) is already correct; EN=1" — this was
  wrong; bit 4 is EN, not bit 2.
- Correct value: `0x14` = `0b00010100` = EN=1, D[2:0]=100 → `VCC_R = VCC_C × 0.65`.
- Additional datasheet note: *"When DSTBON/OFF = 1, internal scan regulator is disabled
  regardless of EN."* The init sequence already handles this correctly — 0x03/0x00 (exit
  standby) is issued before 0x30/0x14.

**Fix applied:** `write_data_byte(0x04)` → `write_data_byte(0x14)` in `ld7138_init()`.

The comment in the code was also corrected to accurately document the bit layout.

---

## Session 2 — First IC Failure

### Setup

- Code fix applied: 0x30 parameter changed from `0x04` → `0x14`.
- VCC_C = 16V (unchanged from Session 1).
- Electrolytic capacitors in place on VCC_C and VCC_R (polarity assumed correct at this
  point).

### Observed Symptoms

- VCC_R rose to **~6V**, then climbed to **~16V**.
- IC became very hot, **sparked, and failed** (burned out).
- Nothing appeared on the display before failure.

### Failure Analysis

**Sequence of events reconstructed:**

1. EN=1 activated; internal regulator starts driving VCC_R toward target (16V × 0.65 = 10.4V).
2. VCC_R stalls at ~6V — the VCC_R electrolytic capacitor was installed with **reversed
   polarity**. In reverse, the cap conducts heavily (low-impedance path to GND), holding
   VCC_R below target while dissipating large current.
3. Reverse current heats the cap until it **fails open** (internal rupture).
4. With the cap now open-circuit, VCC_R is unloaded and rises toward VCC_C (16V) — the
   regulator loses its feedback loop.
5. The row driver is exposed to 16V on VCC_R with no current limiting. IC overheats and
   fails.

**Note:** During the initial ceramic-cap testing the reversed electrolytic was not yet
installed, so this failure mode was not observable in Session 1. The cap polarity was never
validated under real operating voltage (VCC_R stayed near 0V throughout Session 1 due to the
EN=0 bug).

### Corrective Actions Before Session 3

- Both electrolytic capacitors (VCC_C and VCC_R) inspected and confirmed correct polarity:
  **positive lead to the positive rail, negative (stripe) lead to GND**.
- VCC_C lowered to **10V** on the MT3608 to reduce energy available in any subsequent fault.
- Replacement IC and display sourced.

---

## Session 3 — Second IC Test (Current State)

### Setup

- New LD7138 IC and new display panel.
- VCC_C set to **10V**.
- Electrolytic cap polarity confirmed correct.
- Code: 0x30 sends `0x14` (EN=1, D[2:0]=100).
- Additional code change: register 0x30 was moved earlier in the init sequence (now step 3,
  immediately after DSTBY exit at step 2), with a 50ms delay to allow VCC_R to ramp before
  subsequent configuration commands.

### Observed Symptoms

- VCC_R rose to **~4.2V**, then stepped down to settle at **~4.0V**.
- Expected VCC_R: 10V × 0.65 = **6.5V** — actual is significantly below target.
- Both 4.0V and 6.5V are **below the datasheet minimum VCC_R operating voltage of 8V**.
- IC is **noticeably hot**.
- Nothing appeared on the display.

### Analysis

VCC_R is being pulled below the regulator's target, indicating a substantial external current
sink on the VCC_R node. The IC heats up because the internal linear regulator drops
`(VCC_C − VCC_R) = 6V` across its pass element at whatever current the load is drawing.
The display cannot operate with VCC_R = 4V (below 8V minimum).

`4.0 / 10.0 = 0.40` — this does not correspond to any valid D[2:0] ratio (0.65, 0.70, 0.75,
0.80, 0.85), confirming the regulator is in current-limited dropout, not regulating normally.

### Possible Causes (Not Yet Eliminated)

| # | Hypothesis | Diagnostic |
|---|---|---|
| A | **VCC_R short/misconnection on breadboard** — a wire added or moved during debugging creates a low-resistance path from VCC_R to GND or another rail. | Power off. Measure resistance from VCC_R ZIF pad to GND. Should show cap charging (rising R), not a fixed low value. |
| B | **OLED panel damaged by Session 2 burnout** — during the first IC failure, VCC_R reached 16V with VCC_C at 16V. If this was the same physical panel, OLED pixels may have sustained reverse-voltage damage creating widespread internal shorts. | Power off. Measure resistance from VCC_R pad to GND with ribbon disconnected from ZIF. Compare result to with ribbon connected. A difference implicates the panel. Also confirm whether the panel is genuinely new or was connected during the Session 2 failure. |
| C | **VCC_R decoupling cap still wrong** — user confirmed polarity, but verify once more. | Visual inspection: stripe/band = negative = GND side. |

### Suggested Next Steps

1. **Power off the circuit completely** before any probing.
2. **Resistance check:** probe from VCC_R ZIF pad to GND with a multimeter in resistance mode.
   - A good circuit shows a rising reading (capacitor charging from meter current) — not a
     fixed low value.
   - A fixed reading below ~1 kΩ indicates a short that must be found and removed.
3. **Isolate the panel:** disconnect the ribbon cable from the ZIF connector and repeat the
   resistance check on the ZIF pad side. Then check the ribbon side separately. If the short
   is only present with the ribbon connected, the panel itself is the source.
4. **Verify breadboard wiring** around VCC_R visually — trace every wire on that net.
5. **Do not power up again until the resistance check is clean.** Running the IC against a
   shorted VCC_R will destroy it the same way.
6. Once VCC_R resistance is confirmed clean, power on and measure VCC_R before running any
   code — it should be near 0V at rest (cap discharged). Then run the code and confirm VCC_R
   rises to ~6.5V and stabilises there.
7. If VCC_R stabilises at 6.5V (still below the 8V minimum), increase VCC_C on the MT3608
   to ~13V so that VCC_R reaches ~8.45V, which is within spec.

---

## Register Corrections Summary

| Register | Parameter | Incorrect Value | Correct Value | Effect of Error |
|---|---|---|---|---|
| `0x30h` VCC_R_SEL | EN bit (bit 4) | `0x04` (EN=0) | `0x14` (EN=1) | Internal row regulator disabled; VCC_R collapses under any load |

---

## Hardware Incidents

| # | Date | Description | Root Cause |
|---|---|---|---|
| 1 | 2026-03-25 | IC burned out (sparked) | VCC_R electrolytic cap installed with reversed polarity; cap failed open causing VCC_R to rise to VCC_C (16V); IC row driver destroyed |

---

## Open Issues

| # | Issue | Status |
|---|---|---|
| 1 | VCC_R = 4.0V instead of 6.5V; IC hot; display dark | **Under investigation** — suspected VCC_R short on breadboard or damaged panel |
| 2 | Display has never produced any visible output | Blocked by issue #1 |
| 3 | Correct row subset (0–63 or 64–127) for 64-row panel not yet determined | Pending — code currently scans all 128 rows as diagnostic measure |
| 4 | VCC_C should be raised to ≥12.5V (so VCC_R ≥ 8.1V) once VCC_R issue resolved | Pending |
| 5 | PRE pin Zener clamp circuit not yet installed (currently GND direct) | Planned — after basic display operation confirmed |
| 6 | Formal KiCad schematic not created | Planned |
