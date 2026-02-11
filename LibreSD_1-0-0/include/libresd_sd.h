/**
 * @file libresd_sd.h
 * @brief LibreSD SD Card Protocol Layer
 * 
 * Handles SD card initialization, commands, and raw sector access.
 * Supports SD v1, SD v2, SDHC, SDXC.
 */

#ifndef LIBRESD_SD_H
#define LIBRESD_SD_H

#include "libresd_types.h"
#include "libresd_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * SD COMMANDS
 *============================================================================*/

/* Basic commands */
#define SD_CMD0     0       /* GO_IDLE_STATE - Reset card */
#define SD_CMD1     1       /* SEND_OP_COND - MMC init */
#define SD_CMD8     8       /* SEND_IF_COND - Voltage check (v2+) */
#define SD_CMD9     9       /* SEND_CSD - Card specific data */
#define SD_CMD10    10      /* SEND_CID - Card identification */
#define SD_CMD12    12      /* STOP_TRANSMISSION - Abort multi-block */
#define SD_CMD13    13      /* SEND_STATUS - Card status */
#define SD_CMD16    16      /* SET_BLOCKLEN - Set block size */
#define SD_CMD17    17      /* READ_SINGLE_BLOCK */
#define SD_CMD18    18      /* READ_MULTIPLE_BLOCK */
#define SD_CMD24    24      /* WRITE_BLOCK */
#define SD_CMD25    25      /* WRITE_MULTIPLE_BLOCK */
#define SD_CMD32    32      /* ERASE_WR_BLK_START */
#define SD_CMD33    33      /* ERASE_WR_BLK_END */
#define SD_CMD38    38      /* ERASE */
#define SD_CMD55    55      /* APP_CMD - Next is ACMD */
#define SD_CMD58    58      /* READ_OCR - Read OCR register */
#define SD_CMD59    59      /* CRC_ON_OFF */

/* Application commands (preceded by CMD55) */
#define SD_ACMD13   13      /* SD_STATUS */
#define SD_ACMD23   23      /* SET_WR_BLK_ERASE_COUNT */
#define SD_ACMD41   41      /* SD_SEND_OP_COND */

/* Response types */
#define SD_R1       1
#define SD_R1B      2       /* R1 + busy */
#define SD_R2       3       /* 2 bytes */
#define SD_R3       4       /* R1 + 32-bit OCR */
#define SD_R7       5       /* R1 + 32-bit (CMD8) */

/* R1 response bits */
#define SD_R1_IDLE          0x01
#define SD_R1_ERASE_RESET   0x02
#define SD_R1_ILLEGAL_CMD   0x04
#define SD_R1_CRC_ERROR     0x08
#define SD_R1_ERASE_SEQ     0x10
#define SD_R1_ADDRESS_ERROR 0x20
#define SD_R1_PARAM_ERROR   0x40

/* Data tokens */
#define SD_TOKEN_SINGLE     0xFE    /* Single block read/write */
#define SD_TOKEN_MULTI_W    0xFC    /* Multi-block write start */
#define SD_TOKEN_STOP       0xFD    /* Multi-block write stop */

/* Data error tokens */
#define SD_TOKEN_ERROR      0x00
#define SD_TOKEN_CC_ERROR   0x02
#define SD_TOKEN_ECC_FAIL   0x04
#define SD_TOKEN_OUT_RANGE  0x08

/* OCR register bits */
#define SD_OCR_CCS          0x40000000  /* Card Capacity Status (SDHC) */
#define SD_OCR_BUSY         0x80000000  /* Card power up status */

/*============================================================================
 * SD CARD STATE
 *============================================================================*/

typedef struct {
    bool                initialized;    /**< Card is initialized */
    libresd_card_type_t type;          /**< Card type */
    uint32_t            spi_speed;     /**< Current SPI speed */
    
    /* Card info (from CSD/CID) */
    uint64_t            capacity;       /**< Card capacity in bytes */
    uint32_t            sector_count;   /**< Total sectors */
    uint16_t            block_size;     /**< Block size (usually 512) */
    
    /* Addressing mode */
    bool                block_addr;     /**< true = block addressing (SDHC) */
    
    /* CID info */
    uint8_t             cid[16];        /**< Raw CID register */
    uint8_t             csd[16];        /**< Raw CSD register */
    
    /* Statistics */
    uint32_t            read_count;     /**< Sectors read */
    uint32_t            write_count;    /**< Sectors written */
    uint32_t            error_count;    /**< Error count */
} libresd_sd_t;

/*============================================================================
 * SD CARD API
 *============================================================================*/

/**
 * @brief Initialize SD card
 * 
 * Performs full initialization sequence:
 * 1. Set SPI to 400kHz
 * 2. Send 80+ clock pulses with CS high
 * 3. Send CMD0 (reset)
 * 4. Send CMD8 (check v2 card)
 * 5. Send ACMD41 (init)
 * 6. Send CMD58 (check SDHC)
 * 7. Ramp SPI to fast speed
 * 
 * @param sd SD card state structure
 * @param fast_speed_hz Speed to use after init (0 = default)
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_init(libresd_sd_t *sd, uint32_t fast_speed_hz);

/**
 * @brief Deinitialize SD card
 * 
 * @param sd SD card state
 */
void libresd_sd_deinit(libresd_sd_t *sd);

/**
 * @brief Check if SD card is ready
 * 
 * @param sd SD card state
 * @return true if initialized and ready
 */
bool libresd_sd_ready(libresd_sd_t *sd);

/**
 * @brief Read single sector
 * 
 * @param sd SD card state
 * @param sector Sector number (LBA)
 * @param buffer Buffer for data (must be 512 bytes)
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_read_sector(libresd_sd_t *sd, uint32_t sector, uint8_t *buffer);

/**
 * @brief Read multiple sectors
 * 
 * More efficient than reading one at a time.
 * 
 * @param sd SD card state
 * @param sector Starting sector number
 * @param buffer Buffer for data
 * @param count Number of sectors to read
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_read_sectors(libresd_sd_t *sd, uint32_t sector, 
                                       uint8_t *buffer, uint32_t count);

#if LIBRESD_ENABLE_WRITE

/**
 * @brief Write single sector
 * 
 * @param sd SD card state
 * @param sector Sector number (LBA)
 * @param buffer Data to write (must be 512 bytes)
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_write_sector(libresd_sd_t *sd, uint32_t sector, 
                                       const uint8_t *buffer);

/**
 * @brief Write multiple sectors
 * 
 * @param sd SD card state
 * @param sector Starting sector number
 * @param buffer Data to write
 * @param count Number of sectors
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_write_sectors(libresd_sd_t *sd, uint32_t sector,
                                        const uint8_t *buffer, uint32_t count);

/**
 * @brief Erase sectors
 * 
 * @param sd SD card state
 * @param start_sector First sector to erase
 * @param end_sector Last sector to erase
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_erase(libresd_sd_t *sd, uint32_t start_sector, 
                                uint32_t end_sector);

#endif /* LIBRESD_ENABLE_WRITE */

/**
 * @brief Get card info
 * 
 * @param sd SD card state
 * @param info Info structure to fill
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_sd_get_info(libresd_sd_t *sd, libresd_info_t *info);

/**
 * @brief Set SPI speed
 * 
 * @param sd SD card state
 * @param speed_hz New speed in Hz
 * @return Actual speed achieved
 */
uint32_t libresd_sd_set_speed(libresd_sd_t *sd, uint32_t speed_hz);

/**
 * @brief Get card type as string
 * 
 * @param type Card type enum
 * @return String description
 */
const char* libresd_sd_type_str(libresd_card_type_t type);

/*============================================================================
 * LOW-LEVEL FUNCTIONS (for advanced use)
 *============================================================================*/

/**
 * @brief Send SD command
 * 
 * @param cmd Command number (0-63)
 * @param arg 32-bit argument
 * @return R1 response byte
 */
uint8_t libresd_sd_cmd(uint8_t cmd, uint32_t arg);

/**
 * @brief Send application command (ACMD)
 * 
 * Automatically sends CMD55 first.
 * 
 * @param cmd ACMD number
 * @param arg 32-bit argument
 * @return R1 response byte
 */
uint8_t libresd_sd_acmd(uint8_t cmd, uint32_t arg);

/**
 * @brief Wait for card to be ready
 * 
 * @param timeout_ms Timeout in milliseconds
 * @return true if ready, false if timeout
 */
bool libresd_sd_wait_ready(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_SD_H */
