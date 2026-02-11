/**
 * @file libresd_sd.c
 * @brief LibreSD SD Card Protocol Implementation
 * 
 * Fast, efficient SD card driver supporting:
 * - SD v1.x (up to 2GB)
 * - SD v2.0 (standard capacity)
 * - SDHC (2-32GB)
 * - SDXC (32GB-2TB)
 */

#include "libresd_sd.h"
#include <string.h>

/*============================================================================
 * INTERNAL HELPERS
 *============================================================================*/

/**
 * @brief Calculate CRC7 for SD commands
 */
static uint8_t sd_crc7(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t d = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc <<= 1;
            if ((d & 0x80) ^ (crc & 0x80)) {
                crc ^= 0x09;
            }
            d <<= 1;
        }
    }
    return (crc << 1) | 1;
}

/**
 * @brief Send dummy clocks with CS high (required for init)
 */
static void sd_send_clocks(uint8_t count) {
    libresd_hal_cs_high();
    for (uint8_t i = 0; i < count; i++) {
        libresd_hal_spi_transfer(0xFF);
    }
}

/**
 * @brief Wait for card response (not 0xFF)
 */
static uint8_t sd_wait_response(uint32_t timeout_ms) {
    uint32_t start = libresd_hal_get_ms();
    uint8_t r;
    do {
        r = libresd_hal_spi_transfer(0xFF);
        if (r != 0xFF) return r;
    } while ((libresd_hal_get_ms() - start) < timeout_ms);
    return 0xFF;
}

/**
 * @brief Wait for data token
 */
static uint8_t sd_wait_token(uint32_t timeout_ms) {
    uint32_t start = libresd_hal_get_ms();
    uint8_t token;
    do {
        token = libresd_hal_spi_transfer(0xFF);
        if (token != 0xFF) return token;
    } while ((libresd_hal_get_ms() - start) < timeout_ms);
    return 0xFF;
}

/*============================================================================
 * COMMAND INTERFACE
 *============================================================================*/

uint8_t libresd_sd_cmd(uint8_t cmd, uint32_t arg) {
    uint8_t frame[6];
    uint8_t response;
    
    /* Build command frame */
    frame[0] = 0x40 | cmd;
    frame[1] = (arg >> 24) & 0xFF;
    frame[2] = (arg >> 16) & 0xFF;
    frame[3] = (arg >> 8) & 0xFF;
    frame[4] = arg & 0xFF;
    frame[5] = sd_crc7(frame, 5);
    
    libresd_hal_cs_low();
    
    /* Send command */
    for (int i = 0; i < 6; i++) {
        libresd_hal_spi_transfer(frame[i]);
    }
    
    /* Wait for response (up to 8 bytes) */
    for (int i = 0; i < 8; i++) {
        response = libresd_hal_spi_transfer(0xFF);
        if (!(response & 0x80)) break;
    }
    
    return response;
}

uint8_t libresd_sd_acmd(uint8_t cmd, uint32_t arg) {
    /* Send CMD55 first */
    uint8_t r = libresd_sd_cmd(SD_CMD55, 0);
    if (r > 1) {
        libresd_hal_cs_high();
        return r;
    }
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    /* Send the actual ACMD */
    return libresd_sd_cmd(cmd, arg);
}

bool libresd_sd_wait_ready(uint32_t timeout_ms) {
    uint32_t start = libresd_hal_get_ms();
    uint8_t r;
    do {
        r = libresd_hal_spi_transfer(0xFF);
        if (r == 0xFF) return true;
    } while ((libresd_hal_get_ms() - start) < timeout_ms);
    return false;
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

libresd_err_t libresd_sd_init(libresd_sd_t *sd, uint32_t fast_speed_hz) {
    uint8_t r1, r7[4];
    uint32_t ocr;
    uint32_t start;
    
    if (!sd) return LIBRESD_ERR_INVALID_PARAM;
    
    /* Clear state */
    memset(sd, 0, sizeof(libresd_sd_t));
    sd->block_size = 512;
    
    /* Check card presence */
    if (!libresd_hal_card_detect()) {
        LIBRESD_DEBUG_PRINTF("No card detected");
        return LIBRESD_ERR_NO_CARD;
    }
    
    /* Initialize SPI at slow speed (400kHz) */
    sd->spi_speed = libresd_hal_spi_init(LIBRESD_SPI_INIT_HZ);
    LIBRESD_DEBUG_PRINTF("SPI init at %lu Hz", sd->spi_speed);
    
    /* Send 80+ clock pulses with CS high to wake up card */
    libresd_hal_delay_ms(10);
    sd_send_clocks(10);
    
    /* CMD0 - Go to idle state */
    r1 = libresd_sd_cmd(SD_CMD0, 0);
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    if (r1 != SD_R1_IDLE) {
        LIBRESD_DEBUG_PRINTF("CMD0 failed: 0x%02X", r1);
        return LIBRESD_ERR_INIT;
    }
    LIBRESD_DEBUG_PRINTF("CMD0 OK - Card in idle state");
    
    /* CMD8 - Check voltage range (v2+ cards) */
    r1 = libresd_sd_cmd(SD_CMD8, 0x000001AA);
    if (r1 == SD_R1_IDLE) {
        /* SD v2.0 or later */
        for (int i = 0; i < 4; i++) {
            r7[i] = libresd_hal_spi_transfer(0xFF);
        }
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        
        if ((r7[2] != 0x01) || (r7[3] != 0xAA)) {
            LIBRESD_DEBUG_PRINTF("CMD8 voltage check failed");
            return LIBRESD_ERR_VOLTAGE;
        }
        LIBRESD_DEBUG_PRINTF("CMD8 OK - SD v2.0+");
        sd->type = LIBRESD_CARD_SD_V2;
    } else if (r1 & SD_R1_ILLEGAL_CMD) {
        /* SD v1.x or MMC */
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        LIBRESD_DEBUG_PRINTF("SD v1.x detected");
        sd->type = LIBRESD_CARD_SD_V1;
    } else {
        libresd_hal_cs_high();
        return LIBRESD_ERR_INIT;
    }
    
    /* ACMD41 - Initialize card */
    start = libresd_hal_get_ms();
    uint32_t acmd41_arg = (sd->type >= LIBRESD_CARD_SD_V2) ? 0x40000000 : 0;
    
    do {
        r1 = libresd_sd_acmd(SD_ACMD41, acmd41_arg);
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        
        if (r1 == 0x00) break;
        if (r1 & SD_R1_ILLEGAL_CMD) {
            /* Not SD card, try MMC with CMD1 */
            r1 = libresd_sd_cmd(SD_CMD1, 0);
            libresd_hal_cs_high();
            libresd_hal_spi_transfer(0xFF);
            if (r1 == 0x00) {
                sd->type = LIBRESD_CARD_MMC;
                break;
            }
        }
        libresd_hal_delay_ms(10);
    } while ((libresd_hal_get_ms() - start) < LIBRESD_INIT_TIMEOUT_MS);
    
    if (r1 != 0x00) {
        LIBRESD_DEBUG_PRINTF("ACMD41 timeout");
        return LIBRESD_ERR_TIMEOUT;
    }
    LIBRESD_DEBUG_PRINTF("ACMD41 OK - Card initialized");
    
    /* CMD58 - Read OCR (check for SDHC) */
    if (sd->type >= LIBRESD_CARD_SD_V2) {
        r1 = libresd_sd_cmd(SD_CMD58, 0);
        if (r1 == 0x00) {
            ocr = 0;
            for (int i = 0; i < 4; i++) {
                ocr = (ocr << 8) | libresd_hal_spi_transfer(0xFF);
            }
            libresd_hal_cs_high();
            libresd_hal_spi_transfer(0xFF);
            
            if (ocr & SD_OCR_CCS) {
                sd->type = LIBRESD_CARD_SDHC;
                sd->block_addr = true;
                LIBRESD_DEBUG_PRINTF("SDHC/SDXC detected (block addressing)");
            } else {
                LIBRESD_DEBUG_PRINTF("SD v2 standard capacity");
            }
        } else {
            libresd_hal_cs_high();
        }
    }
    
    /* CMD16 - Set block size to 512 (required for SD v1.x) */
    if (!sd->block_addr) {
        r1 = libresd_sd_cmd(SD_CMD16, 512);
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        if (r1 != 0x00) {
            LIBRESD_DEBUG_PRINTF("CMD16 failed");
        }
    }
    
    /* Read CSD to get card capacity */
    r1 = libresd_sd_cmd(SD_CMD9, 0);
    if (r1 == 0x00) {
        if (sd_wait_token(LIBRESD_READ_TIMEOUT_MS) == SD_TOKEN_SINGLE) {
            libresd_hal_spi_transfer_bulk(NULL, sd->csd, 16);
            /* Skip CRC */
            libresd_hal_spi_transfer(0xFF);
            libresd_hal_spi_transfer(0xFF);
            
            /* Parse CSD to get capacity */
            uint8_t csd_ver = (sd->csd[0] >> 6) & 0x03;
            
            if (csd_ver == 0) {
                /* CSD v1.0 */
                uint32_t c_size = ((sd->csd[6] & 0x03) << 10) |
                                  (sd->csd[7] << 2) |
                                  ((sd->csd[8] >> 6) & 0x03);
                uint8_t c_mult = ((sd->csd[9] & 0x03) << 1) |
                                 ((sd->csd[10] >> 7) & 0x01);
                uint8_t read_bl = sd->csd[5] & 0x0F;
                
                sd->sector_count = (c_size + 1) << (c_mult + 2 + read_bl - 9);
                sd->capacity = (uint64_t)sd->sector_count * 512;
            } else {
                /* CSD v2.0 (SDHC/SDXC) */
                uint32_t c_size = ((sd->csd[7] & 0x3F) << 16) |
                                  (sd->csd[8] << 8) |
                                  sd->csd[9];
                sd->sector_count = (c_size + 1) * 1024;
                sd->capacity = (uint64_t)sd->sector_count * 512;
                
                if (sd->capacity > 32ULL * 1024 * 1024 * 1024) {
                    sd->type = LIBRESD_CARD_SDXC;
                }
            }
        }
    }
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    /* Read CID */
    r1 = libresd_sd_cmd(SD_CMD10, 0);
    if (r1 == 0x00) {
        if (sd_wait_token(LIBRESD_READ_TIMEOUT_MS) == SD_TOKEN_SINGLE) {
            libresd_hal_spi_transfer_bulk(NULL, sd->cid, 16);
            libresd_hal_spi_transfer(0xFF);
            libresd_hal_spi_transfer(0xFF);
        }
    }
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    LIBRESD_DEBUG_PRINTF("Card capacity: %llu bytes (%lu sectors)",
                         sd->capacity, sd->sector_count);
    
    /* Ramp up to fast speed */
    uint32_t target_speed = fast_speed_hz ? fast_speed_hz : LIBRESD_SPI_FAST_HZ;
    if (target_speed > LIBRESD_SPI_MAX_HZ) target_speed = LIBRESD_SPI_MAX_HZ;
    
    sd->spi_speed = libresd_hal_spi_init(target_speed);
    LIBRESD_DEBUG_PRINTF("SPI speed: %lu Hz", sd->spi_speed);
    
    sd->initialized = true;
    return LIBRESD_OK;
}

void libresd_sd_deinit(libresd_sd_t *sd) {
    if (sd) {
        sd->initialized = false;
    }
}

bool libresd_sd_ready(libresd_sd_t *sd) {
    return sd && sd->initialized && libresd_hal_card_detect();
}

/*============================================================================
 * READ OPERATIONS
 *============================================================================*/

libresd_err_t libresd_sd_read_sector(libresd_sd_t *sd, uint32_t sector, uint8_t *buffer) {
    uint8_t r1, token;
    
    if (!sd || !buffer) return LIBRESD_ERR_INVALID_PARAM;
    if (!sd->initialized) return LIBRESD_ERR_NOT_MOUNTED;
    
    /* Convert to byte address for non-SDHC cards */
    uint32_t addr = sd->block_addr ? sector : (sector * 512);
    
    r1 = libresd_sd_cmd(SD_CMD17, addr);
    if (r1 != 0x00) {
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        sd->error_count++;
        LIBRESD_DEBUG_PRINTF("CMD17 failed: 0x%02X", r1);
        return LIBRESD_ERR_CMD;
    }
    
    /* Wait for data token */
    token = sd_wait_token(LIBRESD_READ_TIMEOUT_MS);
    if (token != SD_TOKEN_SINGLE) {
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        sd->error_count++;
        LIBRESD_DEBUG_PRINTF("No data token: 0x%02X", token);
        return (token == 0xFF) ? LIBRESD_ERR_TIMEOUT : LIBRESD_ERR_SPI;
    }
    
    /* Read data */
    libresd_hal_spi_transfer_bulk(NULL, buffer, 512);
    
    /* Skip CRC (2 bytes) */
    libresd_hal_spi_transfer(0xFF);
    libresd_hal_spi_transfer(0xFF);
    
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    sd->read_count++;
    return LIBRESD_OK;
}

libresd_err_t libresd_sd_read_sectors(libresd_sd_t *sd, uint32_t sector,
                                       uint8_t *buffer, uint32_t count) {
    uint8_t r1, token;
    libresd_err_t err = LIBRESD_OK;
    
    if (!sd || !buffer || count == 0) return LIBRESD_ERR_INVALID_PARAM;
    if (!sd->initialized) return LIBRESD_ERR_NOT_MOUNTED;
    
    /* Single sector - use simple read */
    if (count == 1) {
        return libresd_sd_read_sector(sd, sector, buffer);
    }
    
    /* Multi-sector read with CMD18 */
    uint32_t addr = sd->block_addr ? sector : (sector * 512);
    
    r1 = libresd_sd_cmd(SD_CMD18, addr);
    if (r1 != 0x00) {
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        return LIBRESD_ERR_CMD;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        token = sd_wait_token(LIBRESD_READ_TIMEOUT_MS);
        if (token != SD_TOKEN_SINGLE) {
            err = (token == 0xFF) ? LIBRESD_ERR_TIMEOUT : LIBRESD_ERR_SPI;
            break;
        }
        
        libresd_hal_spi_transfer_bulk(NULL, buffer + (i * 512), 512);
        
        /* Skip CRC */
        libresd_hal_spi_transfer(0xFF);
        libresd_hal_spi_transfer(0xFF);
        
        sd->read_count++;
    }
    
    /* CMD12 - Stop transmission */
    libresd_sd_cmd(SD_CMD12, 0);
    libresd_hal_cs_high();
    
    /* Wait for card to be ready */
    libresd_sd_wait_ready(LIBRESD_READ_TIMEOUT_MS);
    
    return err;
}

/*============================================================================
 * WRITE OPERATIONS
 *============================================================================*/

#if LIBRESD_ENABLE_WRITE

libresd_err_t libresd_sd_write_sector(libresd_sd_t *sd, uint32_t sector,
                                       const uint8_t *buffer) {
    uint8_t r1, response;
    
    if (!sd || !buffer) return LIBRESD_ERR_INVALID_PARAM;
    if (!sd->initialized) return LIBRESD_ERR_NOT_MOUNTED;
    if (libresd_hal_write_protect()) return LIBRESD_ERR_WRITE_PROTECT;
    
    uint32_t addr = sd->block_addr ? sector : (sector * 512);
    
    r1 = libresd_sd_cmd(SD_CMD24, addr);
    if (r1 != 0x00) {
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        sd->error_count++;
        return LIBRESD_ERR_CMD;
    }
    
    /* Send data token */
    libresd_hal_spi_transfer(0xFF);
    libresd_hal_spi_transfer(SD_TOKEN_SINGLE);
    
    /* Send data */
    libresd_hal_spi_transfer_bulk(buffer, NULL, 512);
    
    /* Send dummy CRC */
    libresd_hal_spi_transfer(0xFF);
    libresd_hal_spi_transfer(0xFF);
    
    /* Check response */
    response = libresd_hal_spi_transfer(0xFF);
    if ((response & 0x1F) != 0x05) {
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        sd->error_count++;
        LIBRESD_DEBUG_PRINTF("Write rejected: 0x%02X", response);
        return LIBRESD_ERR_SPI;
    }
    
    /* Wait for write to complete */
    if (!libresd_sd_wait_ready(LIBRESD_WRITE_TIMEOUT_MS)) {
        libresd_hal_cs_high();
        sd->error_count++;
        return LIBRESD_ERR_TIMEOUT;
    }
    
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    sd->write_count++;
    return LIBRESD_OK;
}

libresd_err_t libresd_sd_write_sectors(libresd_sd_t *sd, uint32_t sector,
                                        const uint8_t *buffer, uint32_t count) {
    uint8_t r1, response;
    libresd_err_t err = LIBRESD_OK;
    
    if (!sd || !buffer || count == 0) return LIBRESD_ERR_INVALID_PARAM;
    if (!sd->initialized) return LIBRESD_ERR_NOT_MOUNTED;
    if (libresd_hal_write_protect()) return LIBRESD_ERR_WRITE_PROTECT;
    
    /* Single sector */
    if (count == 1) {
        return libresd_sd_write_sector(sd, sector, buffer);
    }
    
    /* Pre-erase for better performance */
    libresd_sd_acmd(SD_ACMD23, count);
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    /* Multi-sector write with CMD25 */
    uint32_t addr = sd->block_addr ? sector : (sector * 512);
    
    r1 = libresd_sd_cmd(SD_CMD25, addr);
    if (r1 != 0x00) {
        libresd_hal_cs_high();
        libresd_hal_spi_transfer(0xFF);
        return LIBRESD_ERR_CMD;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        /* Send token */
        libresd_hal_spi_transfer(0xFF);
        libresd_hal_spi_transfer(SD_TOKEN_MULTI_W);
        
        /* Send data */
        libresd_hal_spi_transfer_bulk(buffer + (i * 512), NULL, 512);
        
        /* Dummy CRC */
        libresd_hal_spi_transfer(0xFF);
        libresd_hal_spi_transfer(0xFF);
        
        /* Check response */
        response = libresd_hal_spi_transfer(0xFF);
        if ((response & 0x1F) != 0x05) {
            err = LIBRESD_ERR_SPI;
            break;
        }
        
        /* Wait for write */
        if (!libresd_sd_wait_ready(LIBRESD_WRITE_TIMEOUT_MS)) {
            err = LIBRESD_ERR_TIMEOUT;
            break;
        }
        
        sd->write_count++;
    }
    
    /* Stop token */
    libresd_hal_spi_transfer(SD_TOKEN_STOP);
    libresd_hal_spi_transfer(0xFF);
    
    /* Wait for card to finish */
    libresd_sd_wait_ready(LIBRESD_WRITE_TIMEOUT_MS);
    
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    return err;
}

libresd_err_t libresd_sd_erase(libresd_sd_t *sd, uint32_t start_sector,
                                uint32_t end_sector) {
    uint8_t r1;
    
    if (!sd) return LIBRESD_ERR_INVALID_PARAM;
    if (!sd->initialized) return LIBRESD_ERR_NOT_MOUNTED;
    if (libresd_hal_write_protect()) return LIBRESD_ERR_WRITE_PROTECT;
    
    uint32_t start_addr = sd->block_addr ? start_sector : (start_sector * 512);
    uint32_t end_addr = sd->block_addr ? end_sector : (end_sector * 512);
    
    /* CMD32 - Erase start */
    r1 = libresd_sd_cmd(SD_CMD32, start_addr);
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    if (r1 != 0x00) return LIBRESD_ERR_CMD;
    
    /* CMD33 - Erase end */
    r1 = libresd_sd_cmd(SD_CMD33, end_addr);
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    if (r1 != 0x00) return LIBRESD_ERR_CMD;
    
    /* CMD38 - Erase */
    r1 = libresd_sd_cmd(SD_CMD38, 0);
    if (r1 != 0x00) {
        libresd_hal_cs_high();
        return LIBRESD_ERR_CMD;
    }
    
    /* Wait for erase (can be slow) */
    if (!libresd_sd_wait_ready(30000)) {
        libresd_hal_cs_high();
        return LIBRESD_ERR_TIMEOUT;
    }
    
    libresd_hal_cs_high();
    libresd_hal_spi_transfer(0xFF);
    
    return LIBRESD_OK;
}

#endif /* LIBRESD_ENABLE_WRITE */

/*============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

libresd_err_t libresd_sd_get_info(libresd_sd_t *sd, libresd_info_t *info) {
    if (!sd || !info) return LIBRESD_ERR_INVALID_PARAM;
    
    memset(info, 0, sizeof(libresd_info_t));
    
    info->card_type = sd->type;
    info->card_size = sd->capacity;
    info->sector_count = sd->sector_count;
    
    return LIBRESD_OK;
}

uint32_t libresd_sd_set_speed(libresd_sd_t *sd, uint32_t speed_hz) {
    if (!sd) return 0;
    
    if (speed_hz > LIBRESD_SPI_MAX_HZ) {
        speed_hz = LIBRESD_SPI_MAX_HZ;
    }
    
    sd->spi_speed = libresd_hal_spi_init(speed_hz);
    return sd->spi_speed;
}

const char* libresd_sd_type_str(libresd_card_type_t type) {
    switch (type) {
        case LIBRESD_CARD_NONE:  return "None";
        case LIBRESD_CARD_MMC:   return "MMC";
        case LIBRESD_CARD_SD_V1: return "SD v1.x";
        case LIBRESD_CARD_SD_V2: return "SD v2.0";
        case LIBRESD_CARD_SDHC:  return "SDHC";
        case LIBRESD_CARD_SDXC:  return "SDXC";
        default:                  return "Unknown";
    }
}
