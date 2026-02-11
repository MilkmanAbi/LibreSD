/**
 * @file libresd_fat.h
 * @brief LibreSD FAT Filesystem Layer
 * 
 * Supports FAT12, FAT16, and FAT32 filesystems.
 * Full read/write support with long filename handling.
 */

#ifndef LIBRESD_FAT_H
#define LIBRESD_FAT_H

#include "libresd_types.h"
#include "libresd_sd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * FAT FILESYSTEM STRUCTURES
 *============================================================================*/

/**
 * @brief FAT volume state
 */
typedef struct {
    libresd_sd_t    *sd;                /**< SD card reference */
    bool            mounted;            /**< Volume is mounted */
    libresd_fs_type_t fs_type;          /**< Filesystem type */
    
    /* BPB (BIOS Parameter Block) info */
    uint16_t        bytes_per_sector;   /**< Bytes per sector (512) */
    uint8_t         sectors_per_cluster;/**< Sectors per cluster */
    uint16_t        reserved_sectors;   /**< Reserved sector count */
    uint8_t         num_fats;           /**< Number of FAT copies */
    uint16_t        root_entry_count;   /**< Root dir entries (FAT12/16) */
    uint32_t        total_sectors;      /**< Total sectors on volume */
    uint32_t        sectors_per_fat;    /**< Sectors per FAT */
    uint32_t        root_cluster;       /**< Root dir cluster (FAT32) */
    
    /* Calculated values */
    uint32_t        fat_start_sector;   /**< First FAT sector */
    uint32_t        root_start_sector;  /**< Root dir start (FAT12/16) */
    uint32_t        data_start_sector;  /**< First data sector */
    uint32_t        cluster_count;      /**< Total data clusters */
    uint32_t        cluster_size;       /**< Bytes per cluster */
    
    /* Volume info */
    char            volume_label[12];   /**< Volume label */
    uint32_t        volume_serial;      /**< Volume serial */
    
    /* Current directory */
    uint32_t        cwd_cluster;        /**< Current working directory cluster */
    char            cwd_path[LIBRESD_MAX_PATH]; /**< Current path */
    
    /* Free space tracking */
    uint32_t        free_clusters;      /**< Free cluster count (-1 = unknown) */
    uint32_t        last_alloc_cluster; /**< Last allocated cluster (hint) */
    
    /* Sector buffer for FAT operations */
    uint8_t         fat_buffer[LIBRESD_SECTOR_SIZE];
    uint32_t        fat_buffer_sector;  /**< Sector currently in buffer */
    bool            fat_buffer_dirty;   /**< Buffer modified? */
} libresd_fat_t;

/*============================================================================
 * FAT DIRECTORY ENTRY STRUCTURE (32 bytes)
 *============================================================================*/

typedef struct __attribute__((packed)) {
    uint8_t     name[11];           /**< 8.3 filename (space padded) */
    uint8_t     attr;               /**< Attributes */
    uint8_t     nt_reserved;        /**< Reserved for NT */
    uint8_t     create_time_tenth;  /**< Creation time (10ms) */
    uint16_t    create_time;        /**< Creation time */
    uint16_t    create_date;        /**< Creation date */
    uint16_t    access_date;        /**< Last access date */
    uint16_t    cluster_hi;         /**< High 16 bits of cluster (FAT32) */
    uint16_t    modify_time;        /**< Last modify time */
    uint16_t    modify_date;        /**< Last modify date */
    uint16_t    cluster_lo;         /**< Low 16 bits of cluster */
    uint32_t    file_size;          /**< File size in bytes */
} fat_dirent_t;

/*============================================================================
 * MOUNT/UNMOUNT
 *============================================================================*/

/**
 * @brief Mount FAT filesystem
 * 
 * @param fat FAT volume state structure
 * @param sd Initialized SD card
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_mount(libresd_fat_t *fat, libresd_sd_t *sd);

/**
 * @brief Unmount FAT filesystem
 * 
 * Flushes any dirty buffers.
 * 
 * @param fat FAT volume state
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_unmount(libresd_fat_t *fat);

/**
 * @brief Check if filesystem is mounted
 */
bool libresd_fat_is_mounted(libresd_fat_t *fat);

/**
 * @brief Sync all pending writes to disk
 */
libresd_err_t libresd_fat_sync(libresd_fat_t *fat);

/*============================================================================
 * DIRECTORY OPERATIONS
 *============================================================================*/

/**
 * @brief Open a directory for reading
 * 
 * @param fat FAT volume
 * @param dir Directory handle to fill
 * @param path Directory path (NULL or "/" for root)
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_opendir(libresd_fat_t *fat, libresd_dir_t *dir, 
                                   const char *path);

/**
 * @brief Read next directory entry
 * 
 * @param fat FAT volume
 * @param dir Directory handle
 * @param info File info structure to fill
 * @return LIBRESD_OK, LIBRESD_ERR_EOF when done, or error
 */
libresd_err_t libresd_fat_readdir(libresd_fat_t *fat, libresd_dir_t *dir,
                                   libresd_fileinfo_t *info);

/**
 * @brief Close directory
 */
void libresd_fat_closedir(libresd_dir_t *dir);

/**
 * @brief Change current working directory
 * 
 * @param fat FAT volume
 * @param path New directory path
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_chdir(libresd_fat_t *fat, const char *path);

/**
 * @brief Get current working directory path
 * 
 * @param fat FAT volume
 * @param buffer Buffer to store path
 * @param size Buffer size
 * @return Pointer to buffer, or NULL on error
 */
char* libresd_fat_getcwd(libresd_fat_t *fat, char *buffer, size_t size);

#if LIBRESD_ENABLE_DIRS

/**
 * @brief Create a directory
 * 
 * @param fat FAT volume
 * @param path Directory path to create
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_mkdir(libresd_fat_t *fat, const char *path);

/**
 * @brief Remove an empty directory
 * 
 * @param fat FAT volume
 * @param path Directory path
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_rmdir(libresd_fat_t *fat, const char *path);

#endif /* LIBRESD_ENABLE_DIRS */

/*============================================================================
 * FILE OPERATIONS
 *============================================================================*/

/**
 * @brief Open a file
 * 
 * @param fat FAT volume
 * @param file File handle to fill
 * @param path File path
 * @param mode Open mode flags (LIBRESD_READ, LIBRESD_WRITE, etc.)
 * @return LIBRESD_OK or error code
 */
libresd_err_t libresd_fat_open(libresd_fat_t *fat, libresd_file_t *file,
                                const char *path, uint8_t mode);

/**
 * @brief Close a file
 * 
 * Flushes buffer and updates directory entry.
 */
libresd_err_t libresd_fat_close(libresd_fat_t *fat, libresd_file_t *file);

/**
 * @brief Read from a file
 * 
 * @param fat FAT volume
 * @param file File handle
 * @param buffer Destination buffer
 * @param size Bytes to read
 * @param bytes_read Actual bytes read (can be NULL)
 * @return LIBRESD_OK, LIBRESD_ERR_EOF, or error
 */
libresd_err_t libresd_fat_read(libresd_fat_t *fat, libresd_file_t *file,
                                void *buffer, uint32_t size, uint32_t *bytes_read);

#if LIBRESD_ENABLE_WRITE

/**
 * @brief Write to a file
 * 
 * @param fat FAT volume
 * @param file File handle
 * @param buffer Source buffer
 * @param size Bytes to write
 * @param bytes_written Actual bytes written (can be NULL)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_write(libresd_fat_t *fat, libresd_file_t *file,
                                 const void *buffer, uint32_t size, 
                                 uint32_t *bytes_written);

/**
 * @brief Flush file buffer to disk
 */
libresd_err_t libresd_fat_flush(libresd_fat_t *fat, libresd_file_t *file);

/**
 * @brief Truncate file at current position
 */
libresd_err_t libresd_fat_truncate(libresd_fat_t *fat, libresd_file_t *file);

/**
 * @brief Delete a file
 * 
 * @param fat FAT volume
 * @param path File path
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_unlink(libresd_fat_t *fat, const char *path);

/**
 * @brief Rename/move a file or directory
 * 
 * @param fat FAT volume
 * @param old_path Current path
 * @param new_path New path
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_rename(libresd_fat_t *fat, const char *old_path,
                                  const char *new_path);

#endif /* LIBRESD_ENABLE_WRITE */

/**
 * @brief Seek to position in file
 * 
 * @param fat FAT volume
 * @param file File handle
 * @param offset Offset to seek to
 * @param whence LIBRESD_SEEK_SET/CUR/END
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_seek(libresd_fat_t *fat, libresd_file_t *file,
                                int32_t offset, libresd_seek_t whence);

/**
 * @brief Get current file position
 */
uint32_t libresd_fat_tell(libresd_file_t *file);

/**
 * @brief Check if at end of file
 */
bool libresd_fat_eof(libresd_file_t *file);

/**
 * @brief Get file size
 */
uint32_t libresd_fat_size(libresd_file_t *file);

/**
 * @brief Get file/directory info by path
 * 
 * @param fat FAT volume
 * @param path Path to file or directory
 * @param info Info structure to fill
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_stat(libresd_fat_t *fat, const char *path,
                                libresd_fileinfo_t *info);

/**
 * @brief Check if path exists
 */
bool libresd_fat_exists(libresd_fat_t *fat, const char *path);

/*============================================================================
 * VOLUME OPERATIONS
 *============================================================================*/

/**
 * @brief Get filesystem info
 */
libresd_err_t libresd_fat_get_info(libresd_fat_t *fat, libresd_info_t *info);

/**
 * @brief Get free space in bytes
 */
uint64_t libresd_fat_get_free(libresd_fat_t *fat);

/**
 * @brief Get volume label
 */
const char* libresd_fat_get_label(libresd_fat_t *fat);

#if LIBRESD_ENABLE_FORMAT

/**
 * @brief Format SD card with FAT filesystem
 * 
 * @param sd SD card (must be initialized)
 * @param label Volume label (NULL for default)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_format(libresd_sd_t *sd, const char *label);

#endif /* LIBRESD_ENABLE_FORMAT */

/*============================================================================
 * INTERNAL FUNCTIONS (exposed for advanced use)
 *============================================================================*/

/* FAT directory entry constants */
#define FAT_DIRENT_SIZE         32
#define DIRENT_FREE             0xE5
#define DIRENT_END              0x00

/**
 * @brief Read FAT entry
 */
uint32_t libresd_fat_read_entry(libresd_fat_t *fat, uint32_t cluster);

/**
 * @brief Get next cluster in chain
 */
uint32_t libresd_fat_next_cluster(libresd_fat_t *fat, uint32_t cluster);

/**
 * @brief Convert cluster number to sector
 */
uint32_t libresd_fat_cluster_to_sector(libresd_fat_t *fat, uint32_t cluster);

/**
 * @brief Check if cluster is end-of-chain
 */
bool libresd_fat_is_eoc(libresd_fat_t *fat, uint32_t cluster);

/**
 * @brief Convert string filename to 8.3 FAT format
 * @param str Input filename string
 * @param name Output 11-byte FAT name buffer
 * @return true on success, false if name is invalid
 */
bool str_to_fat_name(const char *str, uint8_t *name);

/**
 * @brief Resolve a path to its directory entry location
 * @param fat FAT volume
 * @param path Path to resolve
 * @param parent_cluster Output: parent directory cluster (can be NULL)
 * @param dir_sector Output: sector containing directory entry (can be NULL)
 * @param dir_offset Output: offset within sector (can be NULL)
 * @param info Output: file info (can be NULL)
 * @return LIBRESD_OK or error
 */
libresd_err_t fat_resolve_path(libresd_fat_t *fat, const char *path,
                               uint32_t *parent_cluster, uint32_t *dir_sector,
                               uint16_t *dir_offset, libresd_fileinfo_t *info);

#if LIBRESD_ENABLE_WRITE

/**
 * @brief Write FAT entry
 */
libresd_err_t libresd_fat_write_entry(libresd_fat_t *fat, uint32_t cluster, 
                                       uint32_t value);

/**
 * @brief Allocate a new cluster
 */
uint32_t libresd_fat_alloc_cluster(libresd_fat_t *fat, uint32_t prev_cluster);

/**
 * @brief Free cluster chain
 */
libresd_err_t libresd_fat_free_chain(libresd_fat_t *fat, uint32_t cluster);

/**
 * @brief Create a new file entry in a directory
 * @param fat FAT volume
 * @param path Full path for new file
 * @param attr File attributes
 * @param dir_sector Output: sector containing new entry
 * @param dir_offset Output: offset within sector
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_fat_create_file(libresd_fat_t *fat, const char *path,
                                       uint8_t attr, uint32_t *dir_sector,
                                       uint16_t *dir_offset);

#endif /* LIBRESD_ENABLE_WRITE */

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_FAT_H */
