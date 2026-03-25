/* test_ld7138.c
 *
 * LD7138 128x64 OLED display bring-up test.
 * Tests: hardware reset, SPI init sequence, 128x64 solid color fill.
 *
 * Compile: gcc -O2 -o test_ld7138 test_ld7138.c -lgpiod
 * Run:     sudo ./test_ld7138
 *
 * Prerequisites:
 *   - VCC_C must be at 8-16V from an external boost converter.
 *     The LD7138 has NO internal boost for VCC_C.
 *     Without it the screen will stay dark regardless of SPI correctness.
 *   - libgpiod v2 installed: sudo apt install libgpiod-dev
 *   - SPI enabled: dtparam=spi=on in /boot/firmware/config.txt
 *
 * Expected results (if VCC_C is correct):
 *   1. Screen fills solid RED   (RGB565 0xF800) for 2 seconds
 *   2. Screen fills solid GREEN (RGB565 0x07E0) for 2 seconds
 *   3. Screen fills solid BLUE  (RGB565 0x001F) for 2 seconds
 *
 * If screen stays fully dark: VCC_C supply issue (most likely).
 * If screen shows noise/partial image: A0 or DispSize mismatch.
 * If program crashes: GPIO or SPI open failure -- check sudo and /dev/spidev0.0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>
#include <time.h>

/* ── Hardware config ──────────────────────────────────────────────── */
#define SPI_DEVICE    "/dev/spidev0.0"
#define SPI_SPEED_HZ  5000000      /* 5 MHz, well within 10 MHz max */
#define SPI_MODE      SPI_MODE_0   /* CPOL=0, CPHA=0 */
#define SPI_BPW       8

#define GPIO_CHIP_PATH  "/dev/gpiochip0"
#define GPIO_RSTB       24   /* BCM 24 = physical pin 18 */
#define GPIO_A0         25   /* BCM 25 = physical pin 22 */

/* Display geometry -- 128(RGB) columns x 128 rows (full IC range for diagnostic).
 * The physical panel is 64 rows bonded to an unknown subset of the IC's 128 rows.
 * Scanning all 128 rows guarantees the panel rows receive data regardless of which
 * half (0-63 or 64-127) the manufacturer bonded the glass to.  Once the panel
 * responds, narrow DISPLAY_H back to 64 and adjust DispSize/MBoxSize to match. */
#define DISPLAY_W   128
#define DISPLAY_H   128

/* ── Globals ──────────────────────────────────────────────────────── */
static int spi_fd = -1;
static struct gpiod_chip         *chip    = NULL;
static struct gpiod_line_request *gpio_req = NULL;

/* ── Timing helpers ───────────────────────────────────────────────── */
static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ── SPI ──────────────────────────────────────────────────────────── */
static void spi_transfer(const uint8_t *buf, size_t len)
{
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)buf,
        .rx_buf        = 0,
        .len           = (uint32_t)len,
        .speed_hz      = SPI_SPEED_HZ,
        .bits_per_word = SPI_BPW,
        .delay_usecs   = 0,
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("spi_transfer: ioctl failed");
        exit(1);
    }
}

/* ── GPIO (libgpiod v2 API) ───────────────────────────────────────── */
/*
 * libgpiod v2 replaced the line-handle API with a request-based API.
 * Key differences from v1:
 *   - gpiod_chip_get_line()          removed -> use gpiod_chip_request_lines()
 *   - gpiod_line_request_output()    removed -> use gpiod_line_settings_set_direction()
 *   - gpiod_line_set_value()         removed -> use gpiod_line_request_set_value()
 *   - gpiod_LINE_VALUE_ACTIVE        = logical HIGH (respects polarity)
 *   - gpiod_LINE_VALUE_INACTIVE      = logical LOW
 */
static void gpio_set(unsigned int bcm_pin, int high)
{
    enum gpiod_line_value val = high ? GPIOD_LINE_VALUE_ACTIVE
                                     : GPIOD_LINE_VALUE_INACTIVE;
    if (gpiod_line_request_set_value(gpio_req, bcm_pin, val) < 0) {
        fprintf(stderr, "gpio_set BCM%u failed\n", bcm_pin);
        exit(1);
    }
}

/* ── LD7138 command/data helpers ──────────────────────────────────── */

/* Send one command byte: A0 low = command register */
static void write_cmd(uint8_t cmd)
{
    gpio_set(GPIO_A0, 0);   /* A0 low -> command */
    spi_transfer(&cmd, 1);
}

/* Send one data/parameter byte: A0 high = parameter */
static void write_data_byte(uint8_t b)
{
    gpio_set(GPIO_A0, 1);   /* A0 high -> parameter/data */
    spi_transfer(&b, 1);
}

/* Send a buffer of pixel data: caller must have set A0 high already */
static void write_pixel_buf(const uint8_t *buf, size_t len)
{
    /* A0 should already be high; this is called in a tight loop */
    spi_transfer(buf, len);
}

/* ── Hardware reset ───────────────────────────────────────────────── */
static void hw_reset(void)
{
    /*
     * Datasheet §8: RSTB active low.
     * Datasheet §10.4: wait >= 30ms after VDD stable before releasing RSTB.
     * We start with RSTB high, pulse low for 50ms, then release and wait.
     */
    gpio_set(GPIO_RSTB, 1);
    sleep_ms(10);
    gpio_set(GPIO_RSTB, 0);   /* assert reset */
    sleep_ms(50);
    gpio_set(GPIO_RSTB, 1);   /* release reset */
    sleep_ms(50);             /* allow IC to come out of reset */
    printf("  hw_reset: done\n");
}

/* ── LD7138 initialization sequence ──────────────────────────────── */
static void ld7138_init(void)
{
    hw_reset();

    /*
     * (1) Software Reset 0x01
     * Reinitializes all registers to defaults (does not clear GRAM).
     * Auto-clears within 200ns; datasheet §11.3 says wait min 200ns.
     * We wait 1ms for safety.
     */
    write_cmd(0x01);
    sleep_ms(1);

    /*
     * (2) Exit Software Standby: 0x03h, param 0x00
     * After reset, DSTBYON/OFF defaults to 0x01 (P0=1 = oscillator stopped).
     * P0=0 starts the oscillator.
     */
    write_cmd(0x03);
    write_data_byte(0x00);   /* P0=0: start oscillator, exit standby */
    sleep_ms(10);            /* allow oscillator to stabilize */

    /*
     * (3) Internal Row Regulator: 0x30h  *** MUST FOLLOW DSTBY EXIT IMMEDIATELY ***
     * Register layout (page 19): bit4=EN, bit3=-, bit2=D2, bit1=D1, bit0=D0
     * Default is 0x04 = EN=0 (regulator DISABLED). The datasheet requires that if
     * the internal regulator is disabled, VCC_R must be driven by an external source.
     * Running with DSTBY=0 and EN=0 without an external VCC_R supply violates this
     * requirement and will damage the row driver over time. Enable immediately.
     * D[2:0]=100: VCC_R = VCC_C x 0.65 (e.g. 16V x 0.65 = 10.4V)
     */
    write_cmd(0x30);
    write_data_byte(0x14);   /* EN=1 (bit4), D[2:0]=100 (bit2): VCC_R = VCC_C x 0.65 */
    sleep_ms(50);            /* allow VCC_R to ramp to operating voltage before proceeding */

    /*
     * (4) Set Interface Bus Width: 0x08h, param 0x01  *** CRITICAL ***
     * Default is 0x00 = 6-bit I/F bus. Pixel data in this driver is sent as
     * 2 bytes per pixel (RGB565 = 8-bit bus format). Without this command the
     * IC interprets the pixel stream as 6-bit mode (3 bytes/pixel), corrupting
     * every pixel and misaligning the entire data stream.
     * P1=0, P0=1: select 8-bit I/F bus.
     */
    write_cmd(0x08);
    write_data_byte(0x01);   /* 8-bit I/F bus */

    /*
     * (5) Display OFF while configuring: 0x02h, param 0x00
     * Default is already OFF, but explicit is safer.
     */
    write_cmd(0x02);
    write_data_byte(0x00);   /* P0=0: display off */

    /*
     * (6) OSC Control: 0x04h
     * M0=0: internal RC oscillator
     * F[2:0]=010: 90Hz frame rate (default; conditions: Pcw=8, Ppw=5, Pdw=5, Scan_N=128)
     */
    write_cmd(0x04);
    write_data_byte(0x02);   /* 0b00000010: M0=0, F2=0, F1=1, F0=0 -> 90Hz */

    /*
     * (7) Set Display Size: 0x07h — 128 columns x 128 rows (DIAGNOSTIC: full IC range)
     * Scanning all 128 rows ensures the physical panel pixels receive data regardless
     * of which 64-row subset the manufacturer bonded the panel glass to (0-63 or 64-127).
     * Once the display responds, narrow Yend back to 63 and confirm row orientation.
     * Each 7-bit coordinate is split: high nibble in bits D2:D0, low nibble in bits D3:D0.
     * Setting value = pixel count - 1. Max = 127 = 0x7F.
     */
    write_cmd(0x07);
    /* Xstart = 0 */
    write_data_byte(0x00);   /* FX[6:4] */
    write_data_byte(0x00);   /* FX[3:0] */
    /* Xend = 127 = 0x7F */
    write_data_byte(0x07);   /* TX[6:4] = 0x7 */
    write_data_byte(0x0F);   /* TX[3:0] = 0xF -> 0x7F = 127 */
    /* Ystart = 0 */
    write_data_byte(0x00);   /* FY[6:4] */
    write_data_byte(0x00);   /* FY[3:0] */
    /* Yend = 127 = 0x7F (full 128 rows) */
    write_data_byte(0x07);   /* TY[6:4] = 0x7 */
    write_data_byte(0x0F);   /* TY[3:0] = 0xF -> 0x7F = 127 */

    /*
     * (8) GRAM Writing Direction: 0x05h
     * D3=0: RGB order (not BGR)
     * D[2:0]=000: left-to-right, top-to-bottom
     */
    write_cmd(0x05);
    write_data_byte(0x00);

    /*
     * (9) Row Scan Direction: 0x06h
     * P0=0: row address increments min to max (top to bottom)
     */
    write_cmd(0x06);
    write_data_byte(0x00);

    /*
     * (10) Set Dot Matrix Current Level: 0x0Eh  *** CRITICAL ***
     * Default for ALL six parameters is 0x00 = 0.0 uA.
     * The LD7138 is a constant-current driver. If this register is not set
     * to a nonzero value, every OLED element is driven with zero current and
     * the display emits no light regardless of all other settings.
     *
     * ENCODING: each 8-bit current value I[7:0] is split across TWO bytes:
     *   Param N+0: bits D3:D0 carry I[7:4]  (high nibble)
     *   Param N+1: bits D3:D0 carry I[3:0]  (low  nibble)
     *
     * Target: 100 uA per channel = 0x64 = 0b0110_0100
     *   High nibble: 0x6 -> send 0x06
     *   Low  nibble: 0x4 -> send 0x04
     *
     * Range: 0x00 (0 uA) to 0xFF (255 uA), 1 uA per step.
     */
    write_cmd(0x0E);
    write_data_byte(0x06);   /* IR[7:4] high nibble: Red   */
    write_data_byte(0x04);   /* IR[3:0] low  nibble: Red   */
    write_data_byte(0x06);   /* IG[7:4] high nibble: Green */
    write_data_byte(0x04);   /* IG[3:0] low  nibble: Green */
    write_data_byte(0x06);   /* IB[7:4] high nibble: Blue  */
    write_data_byte(0x04);   /* IB[3:0] low  nibble: Blue  */

    /*
     * (11) Set Dot Matrix Peak Current Level: 0x0Fh  *** CRITICAL ***
     * Default for all three parameters is 0x00 = 0.0 uA.
     * Peak current is the boosted column drive during the "Peak Boot" period
     * of each scan cycle (see datasheet §4.4 waveform). Also zero by default.
     * Each channel value is a single 6-bit byte (no nibble split for this cmd).
     * Setting R/G/B to 0x10 = 16 steps x 16 uA = 256 uA — moderate for bring-up.
     * Range: 0x00 (0 uA) to 0x3F (1008 uA), 16 uA per step.
     */
    write_cmd(0x0F);
    write_data_byte(0x10);   /* PR[5:0] = 256 uA peak for Red   */
    write_data_byte(0x10);   /* PG[5:0] = 256 uA peak for Green */
    write_data_byte(0x10);   /* PB[5:0] = 256 uA peak for Blue  */

    /*
     * (12) Display ON: 0x02h, param 0x01
     * P0=1: turns on dot matrix display.
     * Must come after current levels are set.
     */
    write_cmd(0x02);
    write_data_byte(0x01);   /* P0=1: display on */

    sleep_ms(10);
    printf("  ld7138_init: complete\n");
}

/* ── Fill the entire 128x64 display with one solid RGB565 color ───── */
static void fill_screen(uint16_t color)
{
    /*
     * GRAM write requires two distinct commands:
     *
     * 1. 0x0A — MBOXSize: defines the X/Y read/write address window inside GRAM.
     *    This is separate from 0x07 (DispSize), which configures the active scan
     *    area on the panel. Both must be set; they serve different purposes.
     *    After MBOXSize executes, the GRAM write pointer is reset to (XS, YS).
     *
     * 2. 0x0C — DataWrite/Read: begins the pixel stream. The IC accepts pixel
     *    data (A0 high) and auto-increments the GRAM address according to the
     *    WriteDirection setting until the MBox boundary is reached.
     *    NOTE: "If you read/write again, re-enter DataWrite/Read command."
     *    i.e. 0x0A + 0x0C must be re-issued for every new frame write.
     *
     * 0x08 (IF_BUS_SEL) was previously used here in error — that command
     * selects the bus interface width and has nothing to do with GRAM writes.
     */

    /* Step 1: Set GRAM write window via MBOXSize (0x0A) — full 128x128 (diagnostic) */
    write_cmd(0x0A);
    write_data_byte(0x00); write_data_byte(0x00);  /* Xstart = 0 */
    write_data_byte(0x07); write_data_byte(0x0F);  /* Xend   = 127 (0x7F) */
    write_data_byte(0x00); write_data_byte(0x00);  /* Ystart = 0 */
    write_data_byte(0x07); write_data_byte(0x0F);  /* Yend   = 127 (0x7F) */

    /* Step 2: Issue DataWrite/Read command (0x0C) to begin pixel stream */
    write_cmd(0x0C);

    /*
     * Step 3: Stream 128 * 64 * 2 = 16384 bytes of RGB565 pixel data.
     * Each pixel = 2 bytes, MSB first: [RRRRRGGG] [GGGBBBBB]
     * A0 must remain HIGH for the entire transfer.
     * The IC stops accepting data once the MBox boundary is reached.
     */
    gpio_set(GPIO_A0, 1);   /* A0 high = pixel data */

    #define CHUNK_SIZE 256
    uint8_t buf[CHUNK_SIZE];
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    for (int i = 0; i < CHUNK_SIZE; i += 2) {
        buf[i]   = hi;
        buf[i+1] = lo;
    }
    int total_bytes = DISPLAY_W * DISPLAY_H * 2;   /* 16384 */
    for (int sent = 0; sent < total_bytes; sent += CHUNK_SIZE) {
        write_pixel_buf(buf, CHUNK_SIZE);
    }
}

/* ── Open and configure /dev/spidev0.0 ───────────────────────────── */
static void spi_open_dev(void)
{
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        perror("open " SPI_DEVICE);
        fprintf(stderr, "Hint: run with sudo, and confirm dtparam=spi=on in config.txt\n");
        exit(1);
    }
    uint8_t  mode  = SPI_MODE;
    uint8_t  bpw   = SPI_BPW;
    uint32_t speed = SPI_SPEED_HZ;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE,          &mode)  < 0) { perror("SPI_IOC_WR_MODE");  exit(1); }
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw)   < 0) { perror("SPI_IOC_WR_BPW");   exit(1); }
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed) < 0) { perror("SPI_IOC_WR_SPEED"); exit(1); }
    printf("  SPI: %s, mode %u, %u Hz\n", SPI_DEVICE, mode, speed);
}

/* ── Open GPIO chip and request RSTB + A0 lines (libgpiod v2) ─────── */
static void gpio_open_lines(void)
{
    /* Open the GPIO chip */
    chip = gpiod_chip_open(GPIO_CHIP_PATH);
    if (!chip) {
        perror("gpiod_chip_open " GPIO_CHIP_PATH);
        fprintf(stderr, "Hint: run with sudo\n");
        exit(1);
    }

    /*
     * libgpiod v2 API:
     *  1. Create line settings describing the desired configuration.
     *  2. Create a line config and assign those settings to specific offsets.
     *  3. Create a request config (optional consumer name).
     *  4. Request the lines from the chip -> returns a line_request handle.
     */

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) { perror("gpiod_line_settings_new"); exit(1); }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    /* Both lines start HIGH: RSTB deasserted, A0 in data mode */
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_ACTIVE);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    if (!line_cfg) { perror("gpiod_line_config_new"); exit(1); }
    unsigned int offsets[2] = { GPIO_RSTB, GPIO_A0 };
    if (gpiod_line_config_add_line_settings(line_cfg, offsets, 2, settings) < 0) {
        perror("gpiod_line_config_add_line_settings");
        exit(1);
    }

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    if (!req_cfg) { perror("gpiod_request_config_new"); exit(1); }
    gpiod_request_config_set_consumer(req_cfg, "ld7138_test");

    gpio_req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!gpio_req) {
        perror("gpiod_chip_request_lines");
        fprintf(stderr, "Hint: are pins BCM%d and BCM%d already in use?\n",
                GPIO_RSTB, GPIO_A0);
        exit(1);
    }

    /* Free config objects; the request handle is what we keep */
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);

    printf("  GPIO: RSTB=BCM%d (pin 18), A0=BCM%d (pin 22)\n", GPIO_RSTB, GPIO_A0);
}

/* ── Cleanup ──────────────────────────────────────────────────────── */
static void cleanup(void)
{
    if (gpio_req) { gpiod_line_request_release(gpio_req); gpio_req = NULL; }
    if (chip)     { gpiod_chip_close(chip);                chip    = NULL; }
    if (spi_fd >= 0) { close(spi_fd); spi_fd = -1; }
}

/* ── Main ─────────────────────────────────────────────────────────── */
int main(void)
{
    printf("=== LD7138 128x64 Display Test ===\n\n");
    printf("Opening hardware...\n");
    spi_open_dev();
    gpio_open_lines();

    printf("\nInitializing LD7138...\n");
    ld7138_init();

    static const struct {
        const char *name;
        uint16_t    color;
    } colors[] = {
        { "RED",   0xF800 },
        { "GREEN", 0x07E0 },
        { "BLUE",  0x001F },
    };

    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        printf("Filling: %s (RGB565 0x%04X)\n", colors[i].name, colors[i].color);
        fill_screen(colors[i].color);
        sleep_ms(2000);
    }

    printf("\nTest complete.\n");
    cleanup();
    return 0;
}