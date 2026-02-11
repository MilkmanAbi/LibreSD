/**
 * @file libresd_config.h
 * @brief LibreSD Configuration - Tune for your MCU
 * 
 * LibreSD - High-performance, cross-platform SD card library
 * Pure C, minimal footprint, maximum speed
 * 
 * MIT License - Free as fuck
 */

#ifndef LIBRESD_CONFIG_H
#define LIBRESD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * USER CONFIGURATION - Modify these for your needs
 *============================================================================*/

/**
 * @brief Sector buffer size (must be 512 for SD cards)
 * Don't change unless you know what you're doing
 */
#ifndef LIBRESD_SECTOR_SIZE
#define LIBRESD_SECTOR_SIZE         512
#endif

/**
 * @brief Maximum path length for file operations
 * Reduce to save RAM on tiny MCUs
 */
#ifndef LIBRESD_MAX_PATH
#define LIBRESD_MAX_PATH            256
#endif

/**
 * @brief Maximum filename length (8.3 = 12, LFN up to 255)
 */
#ifndef LIBRESD_MAX_FILENAME
#define LIBRESD_MAX_FILENAME        128
#endif

/**
 * @brief Maximum open files simultaneously
 * Each open file uses ~32 bytes + sector buffer if buffered
 */
#ifndef LIBRESD_MAX_OPEN_FILES
#define LIBRESD_MAX_OPEN_FILES      4
#endif

/**
 * @brief Enable Long File Name (LFN) support
 * Set to 0 to save ~2KB flash (8.3 names only)
 */
#ifndef LIBRESD_ENABLE_LFN
#define LIBRESD_ENABLE_LFN          1
#endif

/**
 * @brief Enable write support
 * Set to 0 for read-only (saves ~4KB flash)
 */
#ifndef LIBRESD_ENABLE_WRITE
#define LIBRESD_ENABLE_WRITE        1
#endif

/**
 * @brief Enable format support (mkfs)
 * Set to 0 to save ~2KB flash
 */
#ifndef LIBRESD_ENABLE_FORMAT
#define LIBRESD_ENABLE_FORMAT       1
#endif

/**
 * @brief Enable directory operations (mkdir, rmdir)
 */
#ifndef LIBRESD_ENABLE_DIRS
#define LIBRESD_ENABLE_DIRS         1
#endif

/**
 * @brief Enable shell command interface
 */
#ifndef LIBRESD_ENABLE_SHELL
#define LIBRESD_ENABLE_SHELL        1
#endif

/**
 * @brief Number of directory entries to cache
 * More = faster directory traversal, more RAM
 */
#ifndef LIBRESD_DIR_CACHE_SIZE
#define LIBRESD_DIR_CACHE_SIZE      16
#endif

/**
 * @brief Enable FAT32 support (in addition to FAT16)
 */
#ifndef LIBRESD_ENABLE_FAT32
#define LIBRESD_ENABLE_FAT32        1
#endif

/**
 * @brief Enable exFAT support (for >32GB cards)
 * Adds ~3KB flash
 */
#ifndef LIBRESD_ENABLE_EXFAT
#define LIBRESD_ENABLE_EXFAT        0
#endif

/*============================================================================
 * SPI SPEED CONFIGURATION
 *============================================================================*/

/**
 * @brief Initial SPI clock for card detection (max 400kHz per spec)
 */
#ifndef LIBRESD_SPI_INIT_HZ
#define LIBRESD_SPI_INIT_HZ         400000
#endif

/**
 * @brief Default high-speed SPI clock after init
 * Can be overridden at runtime
 */
#ifndef LIBRESD_SPI_FAST_HZ
#define LIBRESD_SPI_FAST_HZ         4000000
#endif

/**
 * @brief Maximum SPI clock (hardware limit)
 * SD cards support up to 25MHz, but your wiring may not
 */
#ifndef LIBRESD_SPI_MAX_HZ
#define LIBRESD_SPI_MAX_HZ          25000000
#endif

/*============================================================================
 * TIMEOUT CONFIGURATION (in milliseconds)
 *============================================================================*/

#ifndef LIBRESD_INIT_TIMEOUT_MS
#define LIBRESD_INIT_TIMEOUT_MS     1000
#endif

#ifndef LIBRESD_READ_TIMEOUT_MS
#define LIBRESD_READ_TIMEOUT_MS     200
#endif

#ifndef LIBRESD_WRITE_TIMEOUT_MS
#define LIBRESD_WRITE_TIMEOUT_MS    500
#endif

/*============================================================================
 * DEBUG CONFIGURATION
 *============================================================================*/

/**
 * @brief Enable debug output
 * Define LIBRESD_DEBUG_PRINTF(fmt, ...) to your printf function
 */
#ifndef LIBRESD_DEBUG
#define LIBRESD_DEBUG               0
#endif

#if LIBRESD_DEBUG
    #ifndef LIBRESD_DEBUG_PRINTF
        #include <stdio.h>
        #define LIBRESD_DEBUG_PRINTF(fmt, ...) printf("[LibreSD] " fmt "\n", ##__VA_ARGS__)
    #endif
#else
    #define LIBRESD_DEBUG_PRINTF(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_CONFIG_H */
