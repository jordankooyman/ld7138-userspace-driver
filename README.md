# LD7138 OLED Display Bring-Up: Final Diagnostic Report (Revised v2)

**Status:** ❌ **NOT FUNCTIONAL** — Display never produced visible output

**Project:** ECEN 5713 Advanced Embedded Software Development — Final Project

**Date:** March 27, 2026

**Author:** Jordan Kooyman

---

## Executive Summary

This report documents the complete bring-up attempt for an LD7138-based 128×64 RGB OLED display, intended as the primary output device for a Buildroot-based embedded Linux project. Despite seven weeks of systematic hardware verification, software development, and iterative debugging—including identifying and correcting multiple independent hardware and software faults—the display never produced visible output.

The investigation revealed a root cause that prevents correct operation when using Linux's `spidev` driver: the LD7138 requires multi-parameter commands (such as `0x0Eh DotCurrent` with six parameters) to be transmitted with **CSB held LOW continuously** across the entire command + all parameters. The `spidev` driver deasserts CSB (CE0) between every `ioctl()` call, resetting the IC's internal SCL counter after each byte. Consequently, every multi-parameter write failed silently, leaving column drive current at its default value of **0 µA**—a state where the display cannot emit light regardless of all other correct settings.

**Important:** While the CSB protocol mismatch is strongly suspected, it has not been definitively verified against the datasheet timing diagram with a logic analyzer. Alternative solutions, such as using a dedicated GPIO for CSB and rewiring the board, may be necessary and are discussed below.

This document serves as a comprehensive diagnostic record for future work, documenting what was learned, what remains unknown, and why the project is pivoting to a fallback display solution. The work performed is valuable as hardware/software bring-up documentation for an extremely poorly documented IC, and provides a clear starting point for anyone resuming this effort.

All detailed debugging logs, including the complete AI chat transcripts used throughout this process, are available in [`docs/debug_log.md`](docs/debug_log.md) in the repository.

Much of the debugging work was done with much AI assistance in order to significantly speed the process up. This results in this summary, based on the AI chat logs found in [`docs/debug_log.md`](docs/debug_log.md), making strong assertions that are not fully verified (e.g., the CSB root cause) but are the conclusions that the AI tool made with confidence from the available evidence. The AI tools were instrumental in guiding the investigation, interpreting datasheet sections, and suggesting next steps. The author made suggestions and frequently attempted to validate certain AI claims, but due to limited time, leaned heavily on the AI for accuracy based on the provided documentation that would take many hours to fully verify manually. The AI's conclusions are presented here as the most likely explanations based on the evidence, but they should be treated as hypotheses rather than confirmed facts until verified with additional testing (e.g., logic analyzer captures of the SPI signals), **despite the confidence they are stated with**.

---

## Project Goal (Original)

The original goal was to develop a complete userspace display library for the LD7138 128×128 RGB OLED controller/driver IC, targeting the Raspberry Pi 4B with:
- SPI communication via Linux `spidev`
- GPIO control via `libgpiod`
- RGB565 framebuffer with graphics primitives
- Integration into a Buildroot external package for the AESD final project

**Fallback Path:** After concluding this investigation, the project will continue with a Touch LCD display using a different controller IC (still SPI interface), ensuring a working deliverable for the final project demonstration.

---

## What Was Accomplished

Despite the display never lighting, significant work was completed that provides value:

### 1. Hardware Wiring Documentation (Complete)
- Full pin mapping between Raspberry Pi 4B and LD7138 15-pin ribbon cable (see [`docs/wiring.md`](docs/wiring.md) in the repository)
    - This is the best attempt at the circuit design based on the very limited available information and my own personal knowledge. It is likely that this circuit may have some small issues in it, leading to some of the issues observed in this testing.
    - A textual description of the circuit is included in [`docs/wiring.md`](docs/wiring.md), and a sketch of the schematic can be found in [`docs/LD7138_schematic_sketch.jpg`](docs/LD7138_schematic_sketch.jpg) in the repository. 
    - The least certain parts of the circuit are the power supplies, particularly VCC_C and VCC_R. Testing started with using the internal regulator for VCC_R, but later transitioned to an external supply with the internal regulator disabled to limit potential IC damage while attempting to resolve configuration issues with current limiting the supply. Proper regression testing has not been conducted to confirm that VCC_R cannot be supplied from the internal regulator, which may represent a good starting point for future work resuming this project.
- Power supply configuration: MT3608 boost converter for VCC_C (10–15V), bench supply for VCC_R (9V, 150mA)
- Zener diode clamp circuit for PRE pin (2.4V clamp, cathode to pin 12, anode to GND) — installed and operational near end of testing sequence
- Decoupling capacitor selection and placement
- Power sequencing procedure documented and enforced
- Complete resistance checks verifying no short circuits on any rail

### 2. Software Infrastructure (Complete)
- `libgpiod` v2 API implementation for GPIO control (RSTB, A0)
- SPI communication via `spidev` with proper configuration (5MHz, Mode 0)
- Complete LD7138 init sequence with all known required registers:
  - `0x01` Software Reset
  - `0x03` Exit standby, start oscillator
  - `0x30` Row regulator configuration
  - `0x08` Interface bus width (8-bit)
  - `0x04` OSC control (90Hz)
  - `0x07` Display size (128×128 diagnostic)
  - `0x05` Write direction
  - `0x06` Scan direction
  - `0x0E` Dot current (attempted)
  - `0x0F` Peak current (attempted)
- 128×128 RGB565 framebuffer fill routine with MBoxSize (`0x0A`) + DataWrite (`0x0C`) sequence
- Build system (Makefile) and demo application

### 3. Debugging and Fault Isolation (Complete)
- Staged test suite: SPI loopback, GPIO toggling, progressive init
- Identification and correction of **eight independent hardware/software errors** (see table below)
- VCC_R voltage and current monitoring under various configurations
- Capacitor selection verification (electrolytic vs ceramic, polarity confirmed correct throughout)
- ZIF connector resistance measurements to rule out contact faults
- Power-on resistance checks confirming panel electrical health

### 4. Root Cause Identification (Complete)
- Determined that `spidev`'s CSB behavior is incompatible with LD7138's multi-parameter command protocol
- Verified that single-parameter commands (`0x01`, `0x03`, `0x02`, etc.) work correctly
- Confirmed row scanning is active (VCC_R self-charges to 3.45V with supply disconnected, indicating pixel current paths)
- Concluded that DotCurrent and PeakCurrent remain at **0 µA** default, making light emission impossible

---

## Investigation Narrative (By Phase)

The following sections chronologically document the bring-up effort, with key findings and dead ends.

### Phase 1: Initial Hardware and Pre-flight Checks

**Status:** Python scripts `init_test_1.sh`, `init_test_2.py`, `init_test_3.py` all passed.

**Findings:**
- SPI enabled in `/boot/firmware/config.txt`, `/dev/spidev0.0` present
- MOSI loopback to MISO confirmed SPI hardware functional
- GPIO toggling on BCM 24/25 measured correctly with multimeter
- `libgpiod` v2 API identified (system had v2, code initially written for v1)

**Errors Corrected:**
| Error | Correction |
|-------|------------|
| libgpiod v1 API used | Rewrote to v2 (gpiod_chip_request_lines, etc.) |

---

### Phase 2: Hardware Power Supply Corrections

**Status:** VCC_C connected, VCC_R measured, but display still dark.

**Findings:**
- **Critical:** LD7138 has **no internal boost converter** for VCC_C—must be externally supplied (8–20V). The 4.7 µF capacitor on VCC_C is decoupling only.
- **Critical:** VDD (pin 3) was **not connected to 3.3V**—only a decoupling cap and jumper to PSEL were present. The IC's logic section was unpowered for all prior runs.
- PRE pins correctly wired: connected to GND via a reverse Zener diode (cathode to pin 12, anode to GND, Vz ≈ 2.4V) per the reference schematic.
- Power sequencing requirement discovered: VDD must be stable before VCC_C/VCC_R are applied (datasheet §10.4). Violating this causes uncontrolled current draw and IC heating.

**Errors Corrected:**
| Error | Correction |
|-------|------------|
| VCC_C missing supply | Added MT3608 boost converter (10–15V) |
| VDD unpowered | Added 3.3V jumper from breadboard rail to pin 4 (tied to pin 3) |
| Power sequencing ignored | Enforced: GND → 3.3V → VCC_C → VCC_R → run code, ≥1s delays |

**Resistance Checks (power off):**
- VCC_C pin 13: 0.75–1.0 kΩ with module connected (MT3608 feedback divider), 8 MΩ+ disconnected — normal
- VCC_R pin 14: 1.5 MΩ with module connected — normal (regulator output in off-state)
- No shorts detected on any power rail

---

### Phase 3: Software Init Sequence Corrections

**Status:** VCC_R now responds to code (rises from 0V to 4.88V at 20mA limit), but display dark.

**Findings:**
- GPIO pin mapping was swapped in hardware: BCM24 was physically connected to A0, BCM25 to RSTB, opposite of code defines. Corrected in code.
- After reset, `0x03h` DSTBYON/OFF defaults to `0x01` (oscillator stopped). Added `0x03, 0x00` to start oscillator.
- `0x08h` IF_BUS_SEL defaults to `0x00` (6-bit I/F). Added `0x08, 0x01` to set 8-bit bus; without this, pixel data is misaligned.
- `0x30h` VCC_R_SEL: Default `0x04` = EN=0 (regulator disabled). Datasheet requires external VCC_R or EN=1. Changed to `0x14` (EN=1, ratio 0.65). **This was incorrectly commented as "EN=1" in earlier code**—the error was not in the value but in the understanding of bit 4 as EN.
- **Critical:** `0x0Eh` DotCurrent and `0x0Fh` PeakCurrent default to 0x00 (0 µA). Added writes to set 100 µA dot, 256 µA peak. **These multi-parameter writes later found to be failing silently.**

**Errors Corrected:**
| Error | Correction |
|-------|------------|
| GPIO_RSTB/A0 swapped in code | Changed defines: GPIO_RSTB=25, GPIO_A0=24 |
| Oscillator not started after reset | Added `0x03, 0x00` |
| Bus width default 6-bit | Added `0x08, 0x01` (8-bit) |
| Row regulator disabled (EN=0) | Changed `0x30` param from 0x04 → 0x14 |
| Dot/Peak current at 0 µA | Added `0x0E` and `0x0F` writes |

---

### Phase 4: Current Limit and Hardware Stress

**Status:** VCC_R draws current only after Display ON, confirming commands are being received. Display still dark.

**Findings:**
- At 20mA current limit, VCC_R dropped from 9V to 4.88V when display ON. Pre-charge current calculation: ~130mA average, so 20mA limit was starving the display.
- Increased limit to 150mA. Current draw after Display ON: **124–129mA**—consistent with pre-charge load.
- **Key observation:** Current at maximum dot/peak settings (255µA dot, 1008µA peak) was **lower** than at minimum settings (25µA dot, 64µA peak). This is physically impossible if dot current writes are landing—higher dot current must increase VCC_R load. This confirmed the dot current writes were not taking effect.
- Floating VCC_R test (disconnect bench supply, run code): VCC_R settled at **3.45V and held steady** after Display ON. This independently confirms row scanning is active—the display is charging the VCC_R capacitor through OLED pixel paths. The display is dark because column drive current is zero, not because scanning is inactive.

**Root Cause Confirmed:** Multi-parameter commands (`0x0E` with 6 parameters, `0x0F` with 3 parameters) are failing. Only the first parameter of each lands; subsequent parameters are lost.

---

### Phase 5: Root Cause Analysis — CSB Protocol Violation

**Status:** Root cause identified (supposedly). Display remains dark due to 0 µA column current.

**Findings:**
- LD7138 serial timing diagram (datasheet §4.3, p.12) shows CSB remaining LOW continuously through command byte AND all parameter bytes. A0 transitions while CSB is asserted.
- Linux `spidev` driver deasserts CE0 (CSB goes HIGH) after every `ioctl()` call—including between command and parameter bytes, and between parameters within a multi-parameter command.
- Datasheet §11.1: "The high level of CSB signal clears the internal buffer of SDA and SCL Counter." When CSB goes high mid-command, the IC resets its internal counter, discarding the partially received command.
- Single-parameter commands appear to work because the IC tolerates one CSB pulse per command when there is only one parameter. Multi-parameter commands fail silently.

**Uncertainty:** This conclusion has not been verified with a logic analyzer. The datasheet timing diagram does not explicitly show CSB remaining low across multiple bytes—it shows a single byte transfer. The interpretation that CSB must stay low across a multi-byte command is inferred from the text that CSB high clears the internal buffer. However, it is possible that the IC expects CSB to pulse between bytes, as long as the high time meets the 30 ns minimum. Without scope/LA captures, the exact cause remains unconfirmed.

**Why `spidev` may not be fixable:**
- The driver does support `cs_change=0` to keep CS asserted across multiple transfers *within a single `ioctl()` call* using the multi-transfer array form.
- Constructing a single `ioctl()` that spans a variable-length command header plus up to 16,384 pixel bytes, with A0 toggling between them, is not practical with `spidev`'s fixed transfer structure.
- A cleaner approach for future work would be to **reassign CSB to a GPIO pin** (e.g., BCM 7, rewiring the breadboard) and control it manually via `libgpiod`, while still using `spidev` with the `SPI_NO_CS` flag for data lines. This would give full control over CSB timing without bitbanging SPI.

**Alternative:** Bitbang SPI entirely using GPIO (discussed earlier but rejected as not production-ready).

---

## Summary of Errors Found and Corrected

| # | Error | Detection Method | Correction | Status |
|---|-------|------------------|------------|--------|
| 1 | libgpiod v1 API used | Compile error | Rewrote to v2 | ✅ Fixed |
| 2 | SPI not enabled | `/dev/spidev0.0` missing | `dtparam=spi=on` | ✅ Fixed |
| 3 | VCC_C missing supply | Display dark, no high voltage | Added MT3608 | ✅ Fixed |
| 4 | VDD unpowered | Wiring audit | 3.3V jumper to pin 4 | ✅ Fixed |
| 5 | PRE pins improperly configured | Wiring audit | Zener clamp installed (cathode to pin 12, anode to GND) | ✅ Fixed |
| 6 | Power sequencing ignored | IC hot before code | Enforced GND→3.3V→VCC_C→VCC_R | ✅ Fixed |
| 7 | GPIO_RSTB/A0 swapped | VCC_R behavior changed with swap | Swapped defines | ✅ Fixed |
| 8 | Oscillator not started after reset | No scanning | Added `0x03, 0x00` | ✅ Fixed |
| 9 | Bus width default 6-bit | Pixel data misaligned | Added `0x08, 0x01` | ✅ Fixed |
| 10 | Row regulator disabled (EN=0) | VCC_R < 1.5V | Changed `0x30` to `0x14` | ✅ Fixed |
| 11 | Dot/Peak current at 0 µA | No light | Added `0x0E` and `0x0F` writes | ⚠️ Writes never land |
| 12 | **CSB protocol violation (suspected)** | Multi-param commands fail | **Unresolved — requires GPIO-controlled CSB or bitbang** | ❌ Not fixed |

---

## Hardware Incident Clarification

During debugging, a theory was proposed that an electrolytic capacitor had been installed with reversed polarity, potentially causing the first IC to fail. **This theory was never confirmed.** Subsequent inspection and testing confirmed that all capacitors were correctly oriented (positive lead to rail, negative lead to GND) throughout the entire bring-up process. The IC failure remains attributed to repeated operation with EN=0 (row regulator disabled) and no external VCC_R supply, which violates the datasheet requirement and likely stressed the row driver transistors. When EN=1 was finally applied, the pre-damaged transistor failed shorted, causing VCC_R to rise to VCC_C (16V) and the IC to burn out.

The second module (replacement) has been handled with proper power sequencing and EN=1 from the start; no further damage has occurred, but IC heating has been observed repeatedly and may have caused latent damage that contributes to the current issues in part.

---

## Known, Suspected, and Unknown

### Known (Confirmed by Testing)
- Hardware wiring is correct (per `docs/wiring.md` in the repository)
- No shorts on VCC_C or VCC_R rails (resistance checks clean)
- VDD (3.3V) is correctly supplied
- VCC_C (10V) is stable under load (MT3608)
- SPI hardware functional (loopback test passed)
- GPIO control functional (RSTB, A0 toggle measured)
- Single-parameter LD7138 commands are received (e.g., `0x01`, `0x03`, `0x02`, `0x08`, `0x04`, `0x30`)
- Row scanning is active (VCC_R self-charges to 3.45V with supply disconnected after Display ON)
- Current draw after Display ON (124–129mA) matches pre-charge load calculation
- Display is dark because column drive current is zero

### Suspected (Root Cause)
- **CSB protocol violation:** Multi-parameter commands (`0x0E`, `0x0F`, `0x07` fully, `0x0A` fully) are not being received because `spidev` deasserts CSB between bytes, resetting the IC's internal SCL counter.
- DotCurrent and PeakCurrent remain at **0 µA** default for all three channels, making light emission impossible.
- The display could produce visible output if multi-parameter writes were correctly transmitted.

### Unknown (Requires Future Work)
- Whether the physical panel is 64 rows bonded to rows 0–63 or 64–127 of the IC
- Whether the internal row regulator (EN=1) can produce stable VCC_R ≥ 8V (currently using external supply)
- The exact SPI transaction structure that would satisfy the LD7138's CSB requirement while using `spidev` (likely SPI_NO_CS + GPIO CSB management)
- Whether the current module remains electrically healthy after the corrected init sequence is run
- **Whether the CSB timing interpretation is correct**—this needs logic analyzer verification

---

## Remaining Technical Blockers

| Blocker | Description | Solution Path |
|---------|-------------|---------------|
| CSB protocol violation (suspected) | `spidev` deasserts CSB after every `ioctl()`, potentially resetting IC's SCL counter mid-command | **Preferred:** Rewire CSB to a GPIO pin (e.g., BCM 7) and control it manually via `libgpiod` while using `spidev` with `SPI_NO_CS`. This avoids bitbanging and gives full control over CSB timing. |
| Dot/Peak current at 0 µA | Multi-parameter writes fail, so current never leaves default | Resolve CSB control; verify with VCC_R current increase from ~126mA baseline to higher value when dot current increased |
| 64-row panel subset unknown | Panel is 64 rows bonded to either 0–63 or 64–127 of IC's 128-row driver | Once display responds, test both ranges by adjusting DispSize Yend |
| VCC_R internal regulator untested | Currently using external bench supply; internal regulator not validated | After basic output confirmed, set `0x30` = `0x14`, VCC_C ≥ 12.3V, and verify VCC_R = 8.0–8.5V |

---

## AI Tool Usage

This project made extensive use of AI tools for debugging assistance, code generation, and documentation. All AI interactions were conducted with human oversight, and all code was reviewed, tested, and integrated manually.

**All AI chat logs are archived in [`docs/debug_log.md`](docs/debug_log.md) for reference.**

| Tool | Usage |
|------|-------|
| Claude (Anthropic) | Debugging sessions, root cause analysis, register interpretation, wiring validation, documentation generation |
| GitHub Copilot (GPT-5.3-Codex) | Code review, build process validation, VCC_R behavior analysis, resistance measurement interpretation |
| Deepseek AI | Final diagnostic report synthesis, technical summary, root cause documentation |

Chat session for writing this report: [Deepseek Chat](https://chat.deepseek.com/share/d5ehelfx8nptaslsdn).

**Disclosure:** AI tools were used to suggest debugging steps, interpret datasheet sections, and draft documentation. All code modifications, hardware changes, and final conclusions were made by the author.

---

## Recommendations for Future Work

If this project is resumed, the following steps are recommended in order:

0. **Perform Regression Testing with Internal Regulator:** Before making any changes, test whether VCC_R can be supplied from the internal regulator (EN=1) with the current code or with some modifications. This will confirm whether the external supply is necessary or if the internal regulator can be used with proper configuration. When originally tested, the VDD pin was left floating rather than connected to 3.3V, which may have caused the internal regulator to fail or behave unpredictably. With VDD correctly supplied, it is worth verifying whether the internal regulator can function properly before proceeding with external supply.

1. **Verify CSB timing with logic analyzer:** Before making any changes, capture CSB, SCLK, MOSI, and A0 during a multi-parameter command to confirm whether the IC resets its SCL counter when CSB goes high between bytes.

2. **Implement GPIO-controlled CSB:**
   - Rewire the CSB line (ribbon pin 7) to a free GPIO (e.g., BCM 7) on the Raspberry Pi.
   - Open `/dev/spidev0.0` with `SPI_NO_CS` flag.
   - Use `libgpiod` to manually control CSB, holding it low across entire command+parameter transactions.
   - Retain `spidev` for MOSI/SCLK.

3. **Verify multi-parameter writes land:**
   - After CSB fix, run with dot current set to 100 µA (0x64) and peak to 256 µA (0x10)
   - Monitor VCC_R current: should increase from ~126mA baseline to higher value when dot current raised
   - If no increase, probe SCLK/MOSI with logic analyzer to verify bytes are sent with CSB low throughout

4. **Confirm display output:**
   - With dot current at moderate level (100 µA), fill screen with solid color
   - If still dark, verify MBoxSize window matches framebuffer size (currently 128×128)
   - If partial output, adjust Yend to 0x3F (63) and retest both 0–63 and 64–127 row ranges

5. **Transition to internal row regulator:**
   - Once basic output confirmed, set VCC_C ≥ 12.3V
   - Change `0x30` to `0x14` (EN=1, ratio 0.65)
   - Verify VCC_R stabilizes at 8.0–8.5V (or 0.65×VCC_C)
   - Remove external bench supply

6. **Narrow to 64 rows:**
   - Determine correct row subset (0–63 or 64–127) by testing both
   - Update DispSize Yend and MBoxSize Yend accordingly

---

## Conclusion

The LD7138 display bring-up effort successfully identified and corrected eight independent hardware and software errors, established a correct power supply and sequencing regime, and developed a complete userspace driver infrastructure. However, the fundamental incompatibility between the Linux `spidev` driver's CSB behavior and the LD7138's requirement for CSB to remain LOW across entire multi-byte transactions (suspected) prevented the critical column current registers from being set. As a result, the display remained dark throughout the investigation.

This work represents a thorough diagnostic of a poorly documented IC, and the lessons learned—particularly regarding power sequencing, multi-parameter command handling, and the pitfalls of using `spidev` for non-standard SPI peripherals—are valuable for future embedded projects. The project will now pivot to a Touch LCD display with a different controller IC (still SPI interface) to ensure a working deliverable for the final project demonstration.

**All hardware is currently operational and can be reused when the CSB issue is resolved.**

---

## Appendix: Key Datasheet References

| Section | Topic | Key Information |
|---------|-------|-----------------|
| §3.1 | Pin Description | VDDL, VDD, PSEL, VCC_C, VCC_R, PRE |
| §4.3 | Serial Interface Timing | CSB must stay low across command+parameters; A0 transitions while CSB asserted |
| §5.2.14 | DotCurrent (0x0Eh) | 8-bit current split across two nibble-wide bytes per channel; default 0x00 |
| §5.2.15 | PeakCurrent (0x0Fh) | 6-bit current, single byte per channel; default 0x00 |
| §5.2.20 | VCC_R_SEL (0x30h) | Bit 4 = EN; D[2:0] = ratio; default 0x04 = EN=0, ratio 0.65 |
| §8 | Hardware Reset | RSTB active low; min pulse width 1000 ns |
| §10.4 | Power Sequence | VDD stable before VCC_C/VCC_R; min delay 2ms |
| §11.1 | Serial I/F Initialization | CSB high clears internal buffer; used to reset SCL counter between commands |

---

**Document Version:** 1.2 (added reference to debug_log.md, clarified CSB uncertainty, recommended GPIO CSB approach)  
**Last Updated:** March 27, 2026  
**Next Steps:** Pivot to fallback display; resume LD7138 work if/when time permits with GPIO-controlled CSB approach.