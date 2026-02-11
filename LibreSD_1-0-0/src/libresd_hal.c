/**
 * @file libresd_hal.c
 * @brief LibreSD HAL default implementations (weak symbols)
 * 
 * Override these in your platform-specific HAL implementation.
 */

#include "libresd_hal.h"

/*============================================================================
 * DEFAULT IMPLEMENTATIONS (weak symbols - override in your HAL)
 *============================================================================*/

/**
 * @brief Default bulk transfer using single-byte transfers
 * Override with DMA implementation for best performance!
 */
__attribute__((weak)) 
void libresd_hal_spi_transfer_bulk(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = libresd_hal_spi_transfer(tx ? tx[i] : 0xFF);
        if (rx) rx[i] = b;
    }
}

/**
 * @brief Default card detect - assume card present
 */
__attribute__((weak))
bool libresd_hal_card_detect(void) {
    return true;
}

/**
 * @brief Default write protect - assume not protected
 */
__attribute__((weak))
bool libresd_hal_write_protect(void) {
    return false;
}

/**
 * @brief Default datetime - return fixed date
 */
__attribute__((weak))
void libresd_hal_get_datetime(libresd_datetime_t *dt) {
    if (dt) {
        dt->year = 2025;
        dt->month = 1;
        dt->day = 1;
        dt->hour = 0;
        dt->minute = 0;
        dt->second = 0;
    }
}
