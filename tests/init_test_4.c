/* test_ld7138.c
 * Tests: hardware reset, SPI init sequence, full-screen solid color fill
 * Compile: gcc -O2 -o test_ld7138 test_ld7138.c -lgpiod
 * Run:     sudo ./test_ld7138
 *
 * Expected results:
 *   Pass 1 — screen fills solid RED   (RGB565: 0xF800)
 *   Pass 2 — screen fills solid GREEN (RGB565: 0x07E0)
 *   Pass 3 — screen fills solid BLUE  (RGB565: 0x001F)
 *   Each color held for 2 seconds.
 *   If screen stays dark: check VCC_C voltage (needs 8-20V).
 *   If screen shows noise/garbage: init sequence issue, check A0 toggling.
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
#include <errno.h>

/* ── Hardware config ───────────────────────────────────────────────── */
#define SPI_DEVICE   "/dev/spidev0.0"
#define SPI_SPEED_HZ  5000000   /* 5 MHz — well within 10 MHz max */
#define SPI_MODE      SPI_MODE_0
#define SPI_BPW       8

#define GPIO_CHIP     "/dev/gpiochip0"
#define GPIO_RSTB     24        /* physical pin 18 */
#define GPIO_A0       25        /* physical pin 22 */

#define DISPLAY_W     128
#define DISPLAY_H     128

/* ── Globals ──────────────────────────────────────────────────────── */
static int spi_fd;
static struct gpiod_chip *chip;
static struct gpiod_line_request *line_req;

static void gpio_set(unsigned int offset, enum gpiod_line_value value) {
    if (gpiod_line_request_set_value(line_req, offset, value) < 0) {
        perror("gpiod_line_request_set_value");
        exit(1);
    }
}

/* ── Helpers ──────────────────────────────────────────────────────── */
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void sleep_us(int us) {
    struct timespec ts = { 0, (long)us * 1000L };
    nanosleep(&ts, NULL);
}

static void spi_transfer(const uint8_t *buf, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)buf,
        .rx_buf        = 0,
        .len           = len,
        .speed_hz      = SPI_SPEED_HZ,
        .bits_per_word = SPI_BPW,
        .delay_usecs   = 0,
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI transfer failed");
        exit(1);
    }
}

/* Send a single command byte: A0 low = command */
static void write_cmd(uint8_t cmd) {
    gpio_set(GPIO_A0, GPIOD_LINE_VALUE_INACTIVE);   /* A0 low → command */
    spi_transfer(&cmd, 1);
}

/* Send data bytes: A0 high = data/parameter */
static void write_data(const uint8_t *buf, size_t len) {
    gpio_set(GPIO_A0, GPIOD_LINE_VALUE_ACTIVE);   /* A0 high → data */
    spi_transfer(buf, len);
}

static void write_data_byte(uint8_t b) {
    write_data(&b, 1);
}

/* ── Hardware reset ──────────────────────────────────────────────── */
static void hw_reset(void) {
    /* Datasheet §8: RSTB active low; hold ≥30ms after VDD stable */
    gpio_set(GPIO_RSTB, GPIOD_LINE_VALUE_ACTIVE);
    sleep_ms(10);
    gpio_set(GPIO_RSTB, GPIOD_LINE_VALUE_INACTIVE);  /* assert reset */
    sleep_ms(50);
    gpio_set(GPIO_RSTB, GPIOD_LINE_VALUE_ACTIVE);  /* deassert */
    sleep_ms(50);                         /* wait for IC to come up */
}

/* ── LD7138 initialization sequence ─────────────────────────────── */
static void ld7138_init(void) {
    hw_reset();

    /* 0x01 — Software Reset (auto-clears in 200ns; wait 1ms to be safe) */
    write_cmd(0x01);
    sleep_ms(1);

    /* 0x02 — Display OFF while configuring */
    write_cmd(0x02);
    write_data_byte(0x00);

    /* 0x04 — OSC Control: internal RC, 90Hz default frame rate */
    write_cmd(0x04);
    write_data_byte(0x02);  /* F2:F0 = 010 → 90Hz, M0=0 internal RC */

    /* 0x05 — Write Direction: RGB order (D3=0), normal direction */
    write_cmd(0x05);
    write_data_byte(0x00);

    /* 0x06 — Set Column Address window: 0 to 127 */
    write_cmd(0x06);
    write_data_byte(0x00);  /* start high nibble */
    write_data_byte(0x00);  /* start low nibble  */
    write_data_byte(0x00);  /* end high nibble   */
    write_data_byte(0x7F);  /* end = 127         */

    /* 0x07 — Set Row Address window: 0 to 127 */
    write_cmd(0x07);
    write_data_byte(0x00);
    write_data_byte(0x00);
    write_data_byte(0x00);
    write_data_byte(0x7F);

    /* 0x09 — Write direction / memory access: left-to-right, top-to-bottom */
    write_cmd(0x09);
    write_data_byte(0x00);

    /* 0x02 — Display ON */
    write_cmd(0x02);
    write_data_byte(0x01);

    sleep_ms(10);
    printf("  LD7138 init sequence complete\n");
}

/* ── Fill screen with a solid RGB565 color ───────────────────────── */
static void fill_screen(uint16_t color) {
    /* Reset address window to full 128x128 */
    write_cmd(0x06);
    write_data_byte(0x00); write_data_byte(0x00);
    write_data_byte(0x00); write_data_byte(0x7F);

    write_cmd(0x07);
    write_data_byte(0x00); write_data_byte(0x00);
    write_data_byte(0x00); write_data_byte(0x7F);

    /* 0x08 — Begin GRAM write */
    write_cmd(0x08);

    /* Stream 128*128 pixels = 32768 bytes (RGB565, MSB first) */
    /* Send in chunks to avoid a 32KB stack buffer */
    #define CHUNK 256
    uint8_t buf[CHUNK];
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    for (int i = 0; i < CHUNK; i += 2) {
        buf[i]   = hi;
        buf[i+1] = lo;
    }
    gpio_set(GPIO_A0, GPIOD_LINE_VALUE_ACTIVE);  /* A0 high = pixel data */
    int total = DISPLAY_W * DISPLAY_H * 2;   /* 32768 bytes */
    for (int sent = 0; sent < total; sent += CHUNK) {
        spi_transfer(buf, CHUNK);
    }
}

/* ── Setup / teardown ────────────────────────────────────────────── */
static void spi_open_dev(void) {
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) { perror("open " SPI_DEVICE); exit(1); }

    uint8_t  mode  = SPI_MODE;
    uint8_t  bpw   = SPI_BPW;
    uint32_t speed = SPI_SPEED_HZ;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE,          &mode)  < 0) { perror("SPI mode");  exit(1); }
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw)   < 0) { perror("SPI bpw");   exit(1); }
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed) < 0) { perror("SPI speed"); exit(1); }
    printf("  SPI: %s @ %u Hz, mode %u\n", SPI_DEVICE, speed, mode);
}

static void gpio_open_lines(void) {
    chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) { perror("gpiod_chip_open"); exit(1); }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    unsigned int offsets[] = { GPIO_RSTB, GPIO_A0 };

    if (!settings || !line_cfg || !req_cfg) {
        fprintf(stderr, "Failed to allocate libgpiod configuration objects\n");
        exit(1);
    }

    if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0) {
        perror("gpiod_line_settings_set_direction");
        exit(1);
    }
    if (gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        perror("gpiod_line_settings_set_output_value");
        exit(1);
    }
    if (gpiod_line_config_add_line_settings(line_cfg, offsets, 2, settings) < 0) {
        perror("gpiod_line_config_add_line_settings");
        exit(1);
    }

    gpiod_request_config_set_consumer(req_cfg, "ld7138_test");

    line_req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!line_req) {
        perror("gpiod_chip_request_lines");
        exit(1);
    }

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);

    printf("  GPIO: RSTB=BCM%d  A0=BCM%d\n", GPIO_RSTB, GPIO_A0);
}

static void cleanup(void) {
    if (line_req)  gpiod_line_request_release(line_req);
    if (chip)      gpiod_chip_close(chip);
    if (spi_fd > 0) close(spi_fd);
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(void) {
    printf("=== LD7138 Display Test ===\n");
    spi_open_dev();
    gpio_open_lines();

    printf("\nInitializing LD7138...\n");
    ld7138_init();

    struct { const char *name; uint16_t color; } colors[] = {
        { "RED",   0xF800 },
        { "GREEN", 0x07E0 },
        { "BLUE",  0x001F },
    };

    for (int i = 0; i < 3; i++) {
        printf("Filling screen: %s (0x%04X)\n", colors[i].name, colors[i].color);
        fill_screen(colors[i].color);
        sleep_ms(2000);
    }

    printf("\nTest complete.\n");
    cleanup();
    return 0;
}