/**
 * @file libresd_types.h
 * @brief LibreSD Common Types and Error Codes
 * 
 * LibreSD - High-performance, cross-platform SD card library
 * Pure C, minimal footprint, maximum speed
 */

#ifndef LIBRESD_TYPES_H
#define LIBRESD_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "libresd_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * ERROR CODES
 *============================================================================*/

typedef enum {
    LIBRESD_OK                  = 0,    /**< Success */
    
    /* Hardware errors (1-19) */
    LIBRESD_ERR_NO_CARD         = 1,    /**< No card detected */
    LIBRESD_ERR_SPI             = 2,    /**< SPI communication error */
    LIBRESD_ERR_TIMEOUT         = 3,    /**< Operation timed out */
    LIBRESD_ERR_CRC             = 4,    /**< CRC check failed */
    LIBRESD_ERR_VOLTAGE         = 5,    /**< Voltage range not supported */
    LIBRESD_ERR_INIT            = 6,    /**< Card initialization failed */
    LIBRESD_ERR_CMD             = 7,    /**< Command failed */
    LIBRESD_ERR_WRITE_PROTECT   = 8,    /**< Card is write protected */
    LIBRESD_ERR_BUSY            = 9,    /**< Card is busy */
    LIBRESD_ERR_READ            = 10,   /**< Read error */
    LIBRESD_ERR_WRITE           = 11,   /**< Write error */
    LIBRESD_ERR_ERASE           = 12,   /**< Erase error */
    
    /* Filesystem errors (20-39) */
    LIBRESD_ERR_NO_FS           = 20,   /**< No filesystem found */
    LIBRESD_ERR_INVALID_FS      = 21,   /**< Invalid/corrupt filesystem */
    LIBRESD_ERR_NOT_FAT         = 22,   /**< Not a FAT filesystem */
    LIBRESD_ERR_FAT_CORRUPT     = 23,   /**< FAT table corrupted */
    LIBRESD_ERR_FULL            = 24,   /**< Filesystem full */
    LIBRESD_ERR_ROOT_FULL       = 25,   /**< Root directory full (FAT12/16) */
    
    /* File errors (40-59) */
    LIBRESD_ERR_NOT_FOUND       = 40,   /**< File/directory not found */
    LIBRESD_ERR_EXISTS          = 41,   /**< File/directory already exists */
    LIBRESD_ERR_NOT_FILE        = 42,   /**< Not a file (is directory) */
    LIBRESD_ERR_NOT_DIR         = 43,   /**< Not a directory (is file) */
    LIBRESD_ERR_DIR_NOT_EMPTY   = 44,   /**< Directory not empty */
    LIBRESD_ERR_INVALID_NAME    = 45,   /**< Invalid filename */
    LIBRESD_ERR_PATH_TOO_LONG   = 46,   /**< Path exceeds max length */
    LIBRESD_ERR_TOO_LONG        = 46,   /**< Alias for PATH_TOO_LONG */
    LIBRESD_ERR_TOO_MANY_OPEN   = 47,   /**< Too many open files */
    LIBRESD_ERR_INVALID_HANDLE  = 48,   /**< Invalid file handle */
    LIBRESD_ERR_EOF             = 49,   /**< End of file reached */
    LIBRESD_ERR_READ_ONLY       = 50,   /**< File opened read-only */
    LIBRESD_ERR_SEEK            = 51,   /**< Seek error */
    LIBRESD_ERR_LOCKED          = 52,   /**< File is locked */
    
    /* General errors (60+) */
    LIBRESD_ERR_INVALID_PARAM   = 60,   /**< Invalid parameter */
    LIBRESD_ERR_NO_MEM          = 61,   /**< Out of memory */
    LIBRESD_ERR_NOT_MOUNTED     = 62,   /**< Filesystem not mounted */
    LIBRESD_ERR_ALREADY_MOUNTED = 63,   /**< Already mounted */
    LIBRESD_ERR_NOT_SUPPORTED   = 64,   /**< Feature not supported/enabled */
    LIBRESD_ERR_GENERAL         = 98,   /**< General error */
    LIBRESD_ERR_INTERNAL        = 99,   /**< Internal error (bug) */
} libresd_err_t;

/*============================================================================
 * SD CARD TYPES
 *============================================================================*/

typedef enum {
    LIBRESD_CARD_NONE           = 0,    /**< No card */
    LIBRESD_CARD_MMC            = 1,    /**< MMC card */
    LIBRESD_CARD_SD_V1          = 2,    /**< SD v1.x (up to 2GB) */
    LIBRESD_CARD_SD_V2          = 3,    /**< SD v2.0 standard capacity */
    LIBRESD_CARD_SDHC           = 4,    /**< SDHC (2-32GB) */
    LIBRESD_CARD_SDXC           = 5,    /**< SDXC (32GB-2TB) */
} libresd_card_type_t;

/*============================================================================
 * FILESYSTEM TYPES
 *============================================================================*/

typedef enum {
    LIBRESD_FS_NONE             = 0,
    LIBRESD_FS_FAT12            = 1,
    LIBRESD_FS_FAT16            = 2,
    LIBRESD_FS_FAT32            = 3,
    LIBRESD_FS_EXFAT            = 4,
} libresd_fs_type_t;

/*============================================================================
 * FILE ATTRIBUTES
 *============================================================================*/

#define LIBRESD_ATTR_READ_ONLY  0x01
#define LIBRESD_ATTR_HIDDEN     0x02
#define LIBRESD_ATTR_SYSTEM     0x04
#define LIBRESD_ATTR_VOLUME_ID  0x08
#define LIBRESD_ATTR_DIRECTORY  0x10
#define LIBRESD_ATTR_ARCHIVE    0x20
#define LIBRESD_ATTR_LFN        0x0F    /* Long filename entry */

/*============================================================================
 * FILE OPEN MODES
 *============================================================================*/

typedef enum {
    LIBRESD_READ        = 0x01,         /**< Open for reading */
    LIBRESD_WRITE       = 0x02,         /**< Open for writing */
    LIBRESD_APPEND      = 0x04,         /**< Append to end */
    LIBRESD_CREATE      = 0x08,         /**< Create if not exists */
    LIBRESD_TRUNCATE    = 0x10,         /**< Truncate existing file */
    LIBRESD_EXCL        = 0x20,         /**< Fail if exists (with CREATE) */
} libresd_open_mode_t;

/*============================================================================
 * SEEK MODES
 *============================================================================*/

typedef enum {
    LIBRESD_SEEK_SET    = 0,            /**< From beginning */
    LIBRESD_SEEK_CUR    = 1,            /**< From current position */
    LIBRESD_SEEK_END    = 2,            /**< From end */
} libresd_seek_t;

/*============================================================================
 * DATE/TIME STRUCTURE
 *============================================================================*/

typedef struct {
    uint16_t year;      /**< 1980-2107 */
    uint8_t  month;     /**< 1-12 */
    uint8_t  day;       /**< 1-31 */
    uint8_t  hour;      /**< 0-23 */
    uint8_t  minute;    /**< 0-59 */
    uint8_t  second;    /**< 0-59 (2-second resolution in FAT) */
} libresd_datetime_t;

/*============================================================================
 * FILE/DIRECTORY INFO
 *============================================================================*/

typedef struct {
    char        name[LIBRESD_MAX_FILENAME];     /**< File/dir name */
    uint32_t    size;                           /**< File size in bytes */
    uint8_t     attr;                           /**< Attributes */
    libresd_datetime_t created;                 /**< Creation time */
    libresd_datetime_t modified;                /**< Last modified time */
    libresd_datetime_t accessed;                /**< Last accessed date */
    
    /* Internal use */
    uint32_t    first_cluster;                  /**< First cluster */
    uint32_t    dir_sector;                     /**< Directory entry sector */
    uint16_t    dir_offset;                     /**< Offset in directory sector */
} libresd_fileinfo_t;

/*============================================================================
 * FILE HANDLE (opaque to user)
 *============================================================================*/

typedef struct {
    bool        is_open;
    uint8_t     mode;                           /**< Open mode flags */
    uint32_t    first_cluster;                  /**< First cluster of file */
    uint32_t    current_cluster;                /**< Current cluster */
    uint32_t    file_size;                      /**< Total file size */
    uint32_t    position;                       /**< Current position */
    uint32_t    cluster_offset;                 /**< Offset within cluster */
    
    /* For directory entry updates */
    uint32_t    dir_sector;
    uint16_t    dir_offset;
    
    /* Sector buffer for this file */
    uint8_t     buffer[LIBRESD_SECTOR_SIZE];
    uint32_t    buffer_sector;                  /**< Sector currently in buffer */
    bool        buffer_dirty;                   /**< Buffer modified? */
} libresd_file_t;

/*============================================================================
 * DIRECTORY HANDLE
 *============================================================================*/

typedef struct {
    bool        is_open;
    uint32_t    first_cluster;                  /**< First cluster of dir */
    uint32_t    current_cluster;                /**< Current cluster */
    uint32_t    current_sector;                 /**< Current sector in cluster */
    uint16_t    entry_offset;                   /**< Entry offset in sector */
    uint8_t     buffer[LIBRESD_SECTOR_SIZE];    /**< Sector buffer */
} libresd_dir_t;

/*============================================================================
 * FILESYSTEM INFO (CARD/VOLUME INFO)
 *============================================================================*/

typedef struct {
    /* Card info */
    libresd_card_type_t card_type;
    uint64_t    card_size;                      /**< Total card size in bytes */
    uint32_t    sector_count;                   /**< Total sectors */
    
    /* Filesystem info */
    libresd_fs_type_t fs_type;
    char        volume_label[12];               /**< Volume label */
    uint32_t    volume_serial;                  /**< Volume serial number */
    
    /* Capacity */
    uint64_t    total_bytes;                    /**< Total filesystem size */
    uint64_t    free_bytes;                     /**< Free space */
    uint64_t    used_bytes;                     /**< Used space */
    
    /* Cluster info */
    uint32_t    cluster_size;                   /**< Bytes per cluster */
    uint32_t    total_clusters;                 /**< Total data clusters */
    uint32_t    free_clusters;                  /**< Free clusters */
} libresd_info_t;

/*============================================================================
 * UTILITY MACROS
 *============================================================================*/

#define LIBRESD_IS_DIR(attr)    (((attr) & LIBRESD_ATTR_DIRECTORY) != 0)
#define LIBRESD_IS_FILE(attr)   (((attr) & LIBRESD_ATTR_DIRECTORY) == 0)
#define LIBRESD_IS_HIDDEN(attr) (((attr) & LIBRESD_ATTR_HIDDEN) != 0)
#define LIBRESD_IS_READONLY(attr) (((attr) & LIBRESD_ATTR_READ_ONLY) != 0)

/* Pack/unpack FAT date/time */
#define LIBRESD_FAT_DATE(y, m, d) (((((y) - 1980) & 0x7F) << 9) | (((m) & 0x0F) << 5) | ((d) & 0x1F))
#define LIBRESD_FAT_TIME(h, m, s) ((((h) & 0x1F) << 11) | (((m) & 0x3F) << 5) | (((s) / 2) & 0x1F))

#define LIBRESD_FAT_YEAR(d)   ((((d) >> 9) & 0x7F) + 1980)
#define LIBRESD_FAT_MONTH(d)  (((d) >> 5) & 0x0F)
#define LIBRESD_FAT_DAY(d)    ((d) & 0x1F)
#define LIBRESD_FAT_HOUR(t)   (((t) >> 11) & 0x1F)
#define LIBRESD_FAT_MIN(t)    (((t) >> 5) & 0x3F)
#define LIBRESD_FAT_SEC(t)    (((t) & 0x1F) * 2)

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_TYPES_H */
