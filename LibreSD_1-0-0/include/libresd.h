/**
 * @file libresd.h
 * @brief LibreSD - High-Performance Cross-Platform SD Card Library
 * 
 * LibreSD is a blazingly fast, cross-platform SD card driver and filesystem
 * library written in pure C. Designed for microcontrollers of all types.
 * 
 * Features:
 *   - Pure C for maximum portability (any MCU, any compiler)
 *   - Minimal memory footprint
 *   - High performance (DMA-friendly, optimized algorithms)
 *   - Support for SD v1, v2, SDHC, SDXC
 *   - FAT12, FAT16, FAT32 filesystems
 *   - Long filename (LFN) support
 *   - Shell-like commands (ls, cd, cat, mkdir, rm, etc.)
 *   - Hardware Abstraction Layer (HAL) for easy porting
 *   - Configurable SPI speeds (400kHz init → user-defined fast)
 * 
 * Quick Start:
 * 
 *   1. Implement HAL functions for your MCU (see libresd_hal.h)
 *   2. Include this header
 *   3. Initialize SD card and mount filesystem
 * 
 * Example:
 * 
 *   #include "libresd.h"
 *   
 *   int main(void) {
 *       libresd_sd_t sd;
 *       libresd_fat_t fat;
 *       
 *       // Initialize SD card
 *       if (libresd_sd_init(&sd) != LIBRESD_OK) {
 *           printf("SD init failed!\n");
 *           return 1;
 *       }
 *       
 *       // Mount filesystem
 *       if (libresd_fat_mount(&fat, &sd) != LIBRESD_OK) {
 *           printf("Mount failed!\n");
 *           return 1;
 *       }
 *       
 *       // Read a file
 *       libresd_file_t file;
 *       char buf[256];
 *       uint32_t bytes_read;
 *       
 *       if (libresd_fat_open(&fat, &file, "/hello.txt", LIBRESD_READ) == LIBRESD_OK) {
 *           libresd_fat_read(&fat, &file, buf, sizeof(buf)-1, &bytes_read);
 *           buf[bytes_read] = '\0';
 *           printf("File contents: %s\n", buf);
 *           libresd_fat_close(&fat, &file);
 *       }
 *       
 *       // Or use shell commands
 *       libresd_shell_t shell;
 *       libresd_shell_init(&shell, &sd, &fat);
 *       libresd_shell_exec(&shell, "ls -la");
 *       libresd_shell_exec(&shell, "cat /readme.txt");
 *       
 *       return 0;
 *   }
 * 
 * @author LibreSD Contributors
 * @version 1.0.0
 * @license MIT
 * 
 * @see https://github.com/your-repo/libresd
 */

#ifndef LIBRESD_H
#define LIBRESD_H

/* Configuration (user can override before including) */
#include "libresd_config.h"

/* Common types and error codes */
#include "libresd_types.h"

/* Hardware Abstraction Layer */
#include "libresd_hal.h"

/* SD card protocol layer */
#include "libresd_sd.h"

/* FAT filesystem */
#include "libresd_fat.h"

/* Shell commands */
#if LIBRESD_ENABLE_SHELL
#include "libresd_shell.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * VERSION INFO
 *============================================================================*/

#define LIBRESD_VERSION_MAJOR   1
#define LIBRESD_VERSION_MINOR   0
#define LIBRESD_VERSION_PATCH   0
#define LIBRESD_VERSION_STRING  "1.0.0"

/**
 * @brief Get version string
 */
static inline const char* libresd_version(void) {
    return LIBRESD_VERSION_STRING;
}

/*============================================================================
 * CONVENIENCE MACROS
 *============================================================================*/

/**
 * @brief Quick file read to buffer
 * 
 * Reads entire file (up to buf_size) into buffer.
 * Returns bytes read, or -1 on error.
 */
static inline int32_t libresd_read_file(libresd_fat_t *fat, const char *path,
                                         void *buf, uint32_t buf_size) {
    libresd_file_t file;
    uint32_t bytes_read = 0;
    
    if (libresd_fat_open(fat, &file, path, LIBRESD_READ) != LIBRESD_OK) {
        return -1;
    }
    
    libresd_fat_read(fat, &file, buf, buf_size, &bytes_read);
    libresd_fat_close(fat, &file);
    
    return (int32_t)bytes_read;
}

#if LIBRESD_ENABLE_WRITE

/**
 * @brief Quick file write from buffer
 * 
 * Writes buffer to file (creates/truncates).
 * Returns bytes written, or -1 on error.
 */
static inline int32_t libresd_write_file(libresd_fat_t *fat, const char *path,
                                          const void *buf, uint32_t size) {
    libresd_file_t file;
    uint32_t bytes_written = 0;
    
    if (libresd_fat_open(fat, &file, path, 
                         LIBRESD_WRITE | LIBRESD_CREATE | LIBRESD_TRUNCATE) != LIBRESD_OK) {
        return -1;
    }
    
    libresd_fat_write(fat, &file, buf, size, &bytes_written);
    libresd_fat_close(fat, &file);
    
    return (int32_t)bytes_written;
}

/**
 * @brief Append data to file
 * 
 * Appends buffer to file (creates if doesn't exist).
 * Returns bytes written, or -1 on error.
 */
static inline int32_t libresd_append_file(libresd_fat_t *fat, const char *path,
                                           const void *buf, uint32_t size) {
    libresd_file_t file;
    uint32_t bytes_written = 0;
    
    if (libresd_fat_open(fat, &file, path, 
                         LIBRESD_WRITE | LIBRESD_CREATE | LIBRESD_APPEND) != LIBRESD_OK) {
        return -1;
    }
    
    libresd_fat_write(fat, &file, buf, size, &bytes_written);
    libresd_fat_close(fat, &file);
    
    return (int32_t)bytes_written;
}

#endif /* LIBRESD_ENABLE_WRITE */

/**
 * @brief Get file size by path
 * 
 * Returns file size, or -1 on error.
 */
static inline int32_t libresd_file_size(libresd_fat_t *fat, const char *path) {
    libresd_fileinfo_t info;
    if (libresd_fat_stat(fat, path, &info) != LIBRESD_OK) {
        return -1;
    }
    return (int32_t)info.size;
}

/*============================================================================
 * ERROR HANDLING HELPERS
 *============================================================================*/

/**
 * @brief Convert error code to string
 */
static inline const char* libresd_error_str(libresd_err_t err) {
    switch (err) {
        case LIBRESD_OK:                return "OK";
        case LIBRESD_ERR_TIMEOUT:       return "Timeout";
        case LIBRESD_ERR_NO_CARD:       return "No card";
        case LIBRESD_ERR_SPI:           return "SPI error";
        case LIBRESD_ERR_CMD:           return "Command error";
        case LIBRESD_ERR_CRC:           return "CRC error";
        case LIBRESD_ERR_VOLTAGE:       return "Voltage error";
        case LIBRESD_ERR_INIT:          return "Init failed";
        case LIBRESD_ERR_READ:          return "Read error";
        case LIBRESD_ERR_WRITE:         return "Write error";
        case LIBRESD_ERR_ERASE:         return "Erase error";
        case LIBRESD_ERR_NO_FS:         return "No filesystem";
        case LIBRESD_ERR_INVALID_FS:    return "Invalid filesystem";
        case LIBRESD_ERR_NOT_MOUNTED:   return "Not mounted";
        case LIBRESD_ERR_ROOT_FULL:     return "Root directory full";
        case LIBRESD_ERR_FULL:          return "Disk full";
        case LIBRESD_ERR_NOT_FOUND:     return "Not found";
        case LIBRESD_ERR_EXISTS:        return "Already exists";
        case LIBRESD_ERR_NOT_FILE:      return "Not a file";
        case LIBRESD_ERR_NOT_DIR:       return "Not a directory";
        case LIBRESD_ERR_DIR_NOT_EMPTY: return "Directory not empty";
        case LIBRESD_ERR_INVALID_NAME:  return "Invalid name";
        case LIBRESD_ERR_TOO_LONG:      return "Path too long";
        case LIBRESD_ERR_EOF:           return "End of file";
        case LIBRESD_ERR_SEEK:          return "Seek error";
        case LIBRESD_ERR_INVALID_HANDLE:return "Invalid handle";
        case LIBRESD_ERR_READ_ONLY:     return "Read only";
        case LIBRESD_ERR_LOCKED:        return "Locked";
        case LIBRESD_ERR_INVALID_PARAM: return "Invalid parameter";
        case LIBRESD_ERR_NOT_SUPPORTED: return "Not supported";
        case LIBRESD_ERR_GENERAL:       return "General error";
        default:                        return "Unknown error";
    }
}

/**
 * @brief Check if error is OK
 */
#define LIBRESD_IS_OK(err)      ((err) == LIBRESD_OK)

/**
 * @brief Check if error indicates failure
 */
#define LIBRESD_IS_ERROR(err)   ((err) != LIBRESD_OK)

/**
 * @brief Return on error
 */
#define LIBRESD_RETURN_ON_ERROR(expr) do { \
    libresd_err_t _err = (expr); \
    if (_err != LIBRESD_OK) return _err; \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_H */
