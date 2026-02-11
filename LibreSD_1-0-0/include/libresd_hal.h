/**
 * @file libresd_hal.h
 * @brief LibreSD Hardware Abstraction Layer
 * 
 * Users MUST implement these functions for their MCU.
 * This is what makes LibreSD truly portable.
 */

#ifndef LIBRESD_HAL_H
#define LIBRESD_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "libresd_config.h"
#include "libresd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * REQUIRED HAL FUNCTIONS - User MUST implement these
 *============================================================================*/

/**
 * @brief Initialize SPI peripheral at specified speed
 * @param speed_hz Desired SPI clock frequency in Hz
 */
extern void libresd_hal_spi_init(uint32_t speed_hz);

/**
 * @brief Transfer a single byte over SPI (full duplex)
 * @param tx_byte Byte to transmit (use 0xFF for read-only)
 * @return Byte received
 */
extern uint8_t libresd_hal_spi_transfer(uint8_t tx_byte);

/**
 * @brief Transfer multiple bytes over SPI
 * @param tx Transmit buffer (NULL to send 0xFF)
 * @param rx Receive buffer (NULL to discard)
 * @param len Number of bytes to transfer
 */
extern void libresd_hal_spi_transfer_bulk(const uint8_t *tx, uint8_t *rx, uint32_t len);

/**
 * @brief Assert (pull low) the SD card chip select
 */
extern void libresd_hal_cs_low(void);

/**
 * @brief Deassert (pull high) the SD card chip select
 */
extern void libresd_hal_cs_high(void);

/**
 * @brief Delay for specified milliseconds
 * @param ms Milliseconds to delay
 */
extern void libresd_hal_delay_ms(uint32_t ms);

/**
 * @brief Get current time in milliseconds
 * @return Current time in milliseconds
 */
extern uint32_t libresd_hal_get_ms(void);

/*============================================================================
 * OPTIONAL HAL FUNCTIONS - Weak defaults provided in libresd_hal.c
 *============================================================================*/

/**
 * @brief Check if card is physically present
 * @return true if card is inserted
 */
extern bool libresd_hal_card_detect(void);

/**
 * @brief Check if card is write protected
 * @return true if write protected
 */
extern bool libresd_hal_write_protect(void);

/**
 * @brief Get current date/time for file timestamps
 * @param dt Pointer to datetime structure to fill
 */
extern void libresd_hal_get_datetime(libresd_datetime_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_HAL_H */
