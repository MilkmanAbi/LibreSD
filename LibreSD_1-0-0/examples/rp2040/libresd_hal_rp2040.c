/**
 * @file libresd_hal_rp2040.c
 * @brief LibreSD HAL Implementation for RP2040 (Pico SDK)
 * 
 * Hardware Abstraction Layer for Raspberry Pi Pico / RP2040.
 * Uses the Pico SDK SPI peripheral.
 * 
 * Wiring (default pins - user configurable):
 *   - GPIO 16: MISO (RX)
 *   - GPIO 17: CS (active low)
 *   - GPIO 18: SCK
 *   - GPIO 19: MOSI (TX)
 * 
 * Usage:
 *   1. Call libresd_hal_rp2040_init() before any LibreSD functions
 *   2. Optionally call libresd_hal_rp2040_set_pins() to change pins
 *   3. Use LibreSD normally
 * 
 * @note This file implements the HAL interface from libresd_hal.h
 */

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "libresd_hal.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Default SPI instance */
#ifndef LIBRESD_SPI_INSTANCE
#define LIBRESD_SPI_INSTANCE    spi0
#endif

/* Default pins (user can change with libresd_hal_rp2040_set_pins) */
static uint pin_miso = 16;
static uint pin_cs   = 17;
static uint pin_sck  = 18;
static uint pin_mosi = 19;

/* SPI instance */
static spi_inst_t *spi = LIBRESD_SPI_INSTANCE;

/* Current SPI speed */
static uint32_t current_speed = 400000;

/* Initialization flag */
static bool hal_initialized = false;

/*============================================================================
 * RP2040-SPECIFIC FUNCTIONS
 *============================================================================*/

/**
 * @brief Set custom pin configuration
 * 
 * Call this BEFORE libresd_hal_rp2040_init() to use non-default pins.
 * 
 * @param miso_pin GPIO for MISO (RX)
 * @param cs_pin GPIO for CS
 * @param sck_pin GPIO for SCK
 * @param mosi_pin GPIO for MOSI (TX)
 */
void libresd_hal_rp2040_set_pins(uint miso_pin, uint cs_pin, uint sck_pin, uint mosi_pin) {
    pin_miso = miso_pin;
    pin_cs = cs_pin;
    pin_sck = sck_pin;
    pin_mosi = mosi_pin;
}

/**
 * @brief Set SPI instance
 * 
 * Call this BEFORE libresd_hal_rp2040_init() to use spi1 instead of spi0.
 * 
 * @param spi_instance Either spi0 or spi1
 */
void libresd_hal_rp2040_set_spi(spi_inst_t *spi_instance) {
    spi = spi_instance;
}

/**
 * @brief Initialize the HAL for RP2040
 * 
 * Sets up SPI peripheral and GPIO pins.
 * Must be called before any LibreSD functions.
 */
void libresd_hal_rp2040_init(void) {
    if (hal_initialized) return;
    
    /* Initialize SPI at 400kHz for card init */
    spi_init(spi, 400000);
    current_speed = 400000;
    
    /* Set SPI format: 8 bits, CPOL=0, CPHA=0, MSB first */
    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    
    /* Configure GPIO pins for SPI */
    gpio_set_function(pin_miso, GPIO_FUNC_SPI);
    gpio_set_function(pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(pin_mosi, GPIO_FUNC_SPI);
    
    /* Configure CS as GPIO output (active low) */
    gpio_init(pin_cs);
    gpio_set_dir(pin_cs, GPIO_OUT);
    gpio_put(pin_cs, 1);  /* Deselect initially */
    
    hal_initialized = true;
}

/**
 * @brief Deinitialize the HAL
 */
void libresd_hal_rp2040_deinit(void) {
    if (!hal_initialized) return;
    
    spi_deinit(spi);
    gpio_deinit(pin_cs);
    
    hal_initialized = false;
}

/*============================================================================
 * HAL INTERFACE IMPLEMENTATION
 *============================================================================*/

/**
 * @brief Initialize SPI at specified speed
 */
void libresd_hal_spi_init(uint32_t speed_hz) {
    if (!hal_initialized) {
        libresd_hal_rp2040_init();
    }
    
    /* Set SPI speed */
    uint32_t actual = spi_set_baudrate(spi, speed_hz);
    current_speed = actual;
    
    LIBRESD_DEBUG_PRINTF("SPI speed: requested %lu, actual %lu Hz\n", 
                         (unsigned long)speed_hz, (unsigned long)actual);
}

/**
 * @brief Transfer single byte over SPI
 */
uint8_t libresd_hal_spi_transfer(uint8_t tx) {
    uint8_t rx;
    spi_write_read_blocking(spi, &tx, &rx, 1);
    return rx;
}

/**
 * @brief Bulk SPI transfer (DMA-friendly on RP2040)
 * 
 * This is the high-performance path. On RP2040, we use blocking
 * transfers which still benefit from the hardware FIFO.
 * For even higher performance, could add DMA support.
 */
void libresd_hal_spi_transfer_bulk(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    if (tx && rx) {
        /* Full duplex */
        spi_write_read_blocking(spi, tx, rx, len);
    } else if (tx) {
        /* TX only */
        spi_write_blocking(spi, tx, len);
    } else if (rx) {
        /* RX only (send 0xFF) */
        /* For SD cards, sending 0xFF keeps MOSI high */
        static const uint8_t dummy = 0xFF;
        for (uint32_t i = 0; i < len; i++) {
            spi_write_read_blocking(spi, &dummy, &rx[i], 1);
        }
    }
}

/**
 * @brief Assert chip select (active low)
 */
void libresd_hal_cs_low(void) {
    gpio_put(pin_cs, 0);
    
    /* Small delay for card to recognize CS */
    __asm volatile ("nop\nnop\nnop\nnop\n");
}

/**
 * @brief Deassert chip select
 */
void libresd_hal_cs_high(void) {
    gpio_put(pin_cs, 1);
    
    /* Extra clock for card to release MISO */
    libresd_hal_spi_transfer(0xFF);
}

/**
 * @brief Blocking delay in milliseconds
 */
void libresd_hal_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

/**
 * @brief Get monotonic millisecond counter
 */
uint32_t libresd_hal_get_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

/*============================================================================
 * OPTIONAL HAL FUNCTIONS (override weak defaults)
 *============================================================================*/

/**
 * @brief Check for card presence
 * 
 * If you have a card detect pin wired, implement this.
 * Returns true if card is inserted.
 */
bool libresd_hal_card_detect(void) {
    /* If you have a CD pin, uncomment and modify:
     * return gpio_get(PIN_CD) == 0;  // CD is usually active low
     */
    return true;  /* Assume card present */
}

/**
 * @brief Check write protect status
 * 
 * If you have a write protect detect pin, implement this.
 * Returns true if card is write protected.
 */
bool libresd_hal_write_protect(void) {
    /* If you have a WP pin, uncomment and modify:
     * return gpio_get(PIN_WP) == 1;  // WP is usually active high
     */
    return false;  /* Assume not protected */
}

/**
 * @brief Get current date/time for file timestamps
 * 
 * If you have RTC configured, this will use it.
 * Otherwise returns a default date.
 */
void libresd_hal_get_datetime(libresd_datetime_t *dt) {
    if (!dt) return;
    
#if PICO_RP2040_RTC_SUPPORTED
    datetime_t t;
    if (rtc_running() && rtc_get_datetime(&t)) {
        dt->year = t.year;
        dt->month = t.month;
        dt->day = t.day;
        dt->hour = t.hour;
        dt->minute = t.min;
        dt->second = t.sec;
        return;
    }
#endif
    
    /* Default date if no RTC */
    dt->year = 2024;
    dt->month = 1;
    dt->day = 1;
    dt->hour = 0;
    dt->minute = 0;
    dt->second = 0;
}

/*============================================================================
 * PERFORMANCE OPTIMIZATIONS (optional)
 *============================================================================*/

/**
 * @brief Fast sector read (512 bytes)
 * 
 * Optimized for reading entire sectors.
 * Can be further optimized with DMA if needed.
 */
void libresd_hal_read_sector_fast(uint8_t *buffer) {
    /* Use the bulk transfer which is already optimized */
    libresd_hal_spi_transfer_bulk(NULL, buffer, 512);
}

/**
 * @brief Fast sector write (512 bytes)
 */
void libresd_hal_write_sector_fast(const uint8_t *buffer) {
    libresd_hal_spi_transfer_bulk(buffer, NULL, 512);
}
