/**
 * @file libresd_fat.c
 * @brief LibreSD FAT Filesystem Implementation
 * 
 * Full FAT12/FAT16/FAT32 support with LFN handling.
 * Optimized for speed and low memory footprint.
 */

#include "libresd_fat.h"
#include <string.h>
#include <ctype.h>

/*============================================================================
 * FAT CONSTANTS
 *============================================================================*/

#define FAT_BOOT_SIGNATURE      0xAA55
#define FAT_LFN_ENTRY_CHARS     13

/* End of chain markers */
#define FAT12_EOC               0x0FF8
#define FAT16_EOC               0xFFF8
#define FAT32_EOC               0x0FFFFFF8

/* Bad cluster markers */
#define FAT12_BAD               0x0FF7
#define FAT16_BAD               0xFFF7
#define FAT32_BAD               0x0FFFFFF7

/* Free cluster marker */
#define FAT_FREE                0x00000000

/* Directory entry special characters */
#define DIRENT_KANJI            0x05

/*============================================================================
 * HELPER MACROS
 *============================================================================*/

#define READ16(buf, off)    ((uint16_t)(buf)[off] | ((uint16_t)(buf)[(off)+1] << 8))
#define READ32(buf, off)    ((uint32_t)(buf)[off] | ((uint32_t)(buf)[(off)+1] << 8) | \
                             ((uint32_t)(buf)[(off)+2] << 16) | ((uint32_t)(buf)[(off)+3] << 24))

#define WRITE16(buf, off, v) do { \
    (buf)[off] = (v) & 0xFF; \
    (buf)[(off)+1] = ((v) >> 8) & 0xFF; \
} while(0)

#define WRITE32(buf, off, v) do { \
    (buf)[off] = (v) & 0xFF; \
    (buf)[(off)+1] = ((v) >> 8) & 0xFF; \
    (buf)[(off)+2] = ((v) >> 16) & 0xFF; \
    (buf)[(off)+3] = ((v) >> 24) & 0xFF; \
} while(0)

/*============================================================================
 * INTERNAL HELPERS
 *============================================================================*/

/**
 * @brief Convert 8.3 name to string
 */
static void fat_name_to_str(const uint8_t *name, char *str) {
    int i, j = 0;
    
    /* Copy name part (8 chars) */
    for (i = 0; i < 8 && name[i] != ' '; i++) {
        str[j++] = (name[i] == DIRENT_KANJI) ? 0xE5 : name[i];
    }
    
    /* Add extension if present */
    if (name[8] != ' ') {
        str[j++] = '.';
        for (i = 8; i < 11 && name[i] != ' '; i++) {
            str[j++] = name[i];
        }
    }
    
    str[j] = '\0';
    
    /* Convert to lowercase for display */
    for (i = 0; str[i]; i++) {
        if (str[i] >= 'A' && str[i] <= 'Z') {
            str[i] += 32;
        }
    }
}

/**
 * @brief Convert string to 8.3 name
 */
bool str_to_fat_name(const char *str, uint8_t *name) {
    int i, j;
    const char *dot;
    
    /* Initialize with spaces */
    memset(name, ' ', 11);
    
    /* Skip leading dots and spaces */
    while (*str == ' ' || *str == '.') str++;
    if (!*str) return false;
    
    /* Find extension */
    dot = strrchr(str, '.');
    
    /* Copy name part */
    j = 0;
    for (i = 0; str[i] && str + i != dot && j < 8; i++) {
        char c = str[i];
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        name[j++] = c;
    }
    
    /* Copy extension */
    if (dot && dot[1]) {
        j = 8;
        for (i = 1; dot[i] && j < 11; i++) {
            char c = dot[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            name[j++] = c;
        }
    }
    
    /* First byte 0xE5 is special */
    if (name[0] == 0xE5) {
        name[0] = DIRENT_KANJI;
    }
    
    return true;
}

/**
 * @brief Check if name matches 8.3 pattern
 */
static bool fat_name_match(const uint8_t *entry_name, const uint8_t *search_name) {
    for (int i = 0; i < 11; i++) {
        uint8_t e = entry_name[i];
        uint8_t s = search_name[i];
        
        /* Convert to uppercase for comparison */
        if (e >= 'a' && e <= 'z') e -= 32;
        if (s >= 'a' && s <= 'z') s -= 32;
        
        if (e != s) return false;
    }
    return true;
}

/**
 * @brief Parse path and resolve to cluster
 */
libresd_err_t fat_resolve_path(libresd_fat_t *fat, const char *path,
                                       uint32_t *cluster, uint32_t *dir_sector,
                                       uint16_t *dir_offset, libresd_fileinfo_t *info);

/*============================================================================
 * CLUSTER OPERATIONS
 *============================================================================*/

uint32_t libresd_fat_cluster_to_sector(libresd_fat_t *fat, uint32_t cluster) {
    if (cluster < 2) return 0;
    return fat->data_start_sector + (cluster - 2) * fat->sectors_per_cluster;
}

bool libresd_fat_is_eoc(libresd_fat_t *fat, uint32_t cluster) {
    switch (fat->fs_type) {
        case LIBRESD_FS_FAT12: return cluster >= FAT12_EOC;
        case LIBRESD_FS_FAT16: return cluster >= FAT16_EOC;
        case LIBRESD_FS_FAT32: return cluster >= FAT32_EOC;
        default: return true;
    }
}

uint32_t libresd_fat_read_entry(libresd_fat_t *fat, uint32_t cluster) {
    uint32_t fat_offset, fat_sector, offset;
    uint32_t value;
    
    switch (fat->fs_type) {
        case LIBRESD_FS_FAT12:
            fat_offset = cluster + (cluster / 2);
            fat_sector = fat->fat_start_sector + (fat_offset / 512);
            offset = fat_offset % 512;
            
            /* May span two sectors */
            if (fat->fat_buffer_sector != fat_sector) {
                if (libresd_sd_read_sector(fat->sd, fat_sector, fat->fat_buffer) != LIBRESD_OK) {
                    return 0;
                }
                fat->fat_buffer_sector = fat_sector;
            }
            
            value = fat->fat_buffer[offset];
            if (offset == 511) {
                uint8_t tmp[512];
                if (libresd_sd_read_sector(fat->sd, fat_sector + 1, tmp) != LIBRESD_OK) {
                    return 0;
                }
                value |= ((uint32_t)tmp[0] << 8);
            } else {
                value |= ((uint32_t)fat->fat_buffer[offset + 1] << 8);
            }
            
            if (cluster & 1) {
                value >>= 4;
            } else {
                value &= 0x0FFF;
            }
            return value;
            
        case LIBRESD_FS_FAT16:
            fat_offset = cluster * 2;
            fat_sector = fat->fat_start_sector + (fat_offset / 512);
            offset = fat_offset % 512;
            
            if (fat->fat_buffer_sector != fat_sector) {
                if (libresd_sd_read_sector(fat->sd, fat_sector, fat->fat_buffer) != LIBRESD_OK) {
                    return 0;
                }
                fat->fat_buffer_sector = fat_sector;
            }
            
            return READ16(fat->fat_buffer, offset);
            
        case LIBRESD_FS_FAT32:
            fat_offset = cluster * 4;
            fat_sector = fat->fat_start_sector + (fat_offset / 512);
            offset = fat_offset % 512;
            
            if (fat->fat_buffer_sector != fat_sector) {
                if (libresd_sd_read_sector(fat->sd, fat_sector, fat->fat_buffer) != LIBRESD_OK) {
                    return 0;
                }
                fat->fat_buffer_sector = fat_sector;
            }
            
            return READ32(fat->fat_buffer, offset) & 0x0FFFFFFF;
            
        default:
            return 0;
    }
}

uint32_t libresd_fat_next_cluster(libresd_fat_t *fat, uint32_t cluster) {
    uint32_t next = libresd_fat_read_entry(fat, cluster);
    
    if (libresd_fat_is_eoc(fat, next)) {
        return 0;
    }
    
    return next;
}

#if LIBRESD_ENABLE_WRITE

libresd_err_t libresd_fat_write_entry(libresd_fat_t *fat, uint32_t cluster, 
                                       uint32_t value) {
    uint32_t fat_offset, fat_sector, offset;
    libresd_err_t err;
    
    switch (fat->fs_type) {
        case LIBRESD_FS_FAT12:
            fat_offset = cluster + (cluster / 2);
            fat_sector = fat->fat_start_sector + (fat_offset / 512);
            offset = fat_offset % 512;
            
            if (fat->fat_buffer_sector != fat_sector) {
                if (fat->fat_buffer_dirty) {
                    err = libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector, 
                                                   fat->fat_buffer);
                    if (err != LIBRESD_OK) return err;
                }
                if (libresd_sd_read_sector(fat->sd, fat_sector, fat->fat_buffer) != LIBRESD_OK) {
                    return LIBRESD_ERR_SPI;
                }
                fat->fat_buffer_sector = fat_sector;
            }
            
            if (cluster & 1) {
                fat->fat_buffer[offset] = (fat->fat_buffer[offset] & 0x0F) | ((value << 4) & 0xF0);
                if (offset < 511) {
                    fat->fat_buffer[offset + 1] = (value >> 4) & 0xFF;
                }
            } else {
                fat->fat_buffer[offset] = value & 0xFF;
                if (offset < 511) {
                    fat->fat_buffer[offset + 1] = (fat->fat_buffer[offset + 1] & 0xF0) | 
                                                  ((value >> 8) & 0x0F);
                }
            }
            fat->fat_buffer_dirty = true;
            break;
            
        case LIBRESD_FS_FAT16:
            fat_offset = cluster * 2;
            fat_sector = fat->fat_start_sector + (fat_offset / 512);
            offset = fat_offset % 512;
            
            if (fat->fat_buffer_sector != fat_sector) {
                if (fat->fat_buffer_dirty) {
                    err = libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector, 
                                                   fat->fat_buffer);
                    if (err != LIBRESD_OK) return err;
                }
                if (libresd_sd_read_sector(fat->sd, fat_sector, fat->fat_buffer) != LIBRESD_OK) {
                    return LIBRESD_ERR_SPI;
                }
                fat->fat_buffer_sector = fat_sector;
            }
            
            WRITE16(fat->fat_buffer, offset, value);
            fat->fat_buffer_dirty = true;
            break;
            
        case LIBRESD_FS_FAT32:
            fat_offset = cluster * 4;
            fat_sector = fat->fat_start_sector + (fat_offset / 512);
            offset = fat_offset % 512;
            
            if (fat->fat_buffer_sector != fat_sector) {
                if (fat->fat_buffer_dirty) {
                    err = libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector, 
                                                   fat->fat_buffer);
                    if (err != LIBRESD_OK) return err;
                }
                if (libresd_sd_read_sector(fat->sd, fat_sector, fat->fat_buffer) != LIBRESD_OK) {
                    return LIBRESD_ERR_SPI;
                }
                fat->fat_buffer_sector = fat_sector;
            }
            
            /* Preserve high 4 bits */
            value = (READ32(fat->fat_buffer, offset) & 0xF0000000) | (value & 0x0FFFFFFF);
            WRITE32(fat->fat_buffer, offset, value);
            fat->fat_buffer_dirty = true;
            break;
            
        default:
            return LIBRESD_ERR_NOT_SUPPORTED;
    }
    
    return LIBRESD_OK;
}

uint32_t libresd_fat_alloc_cluster(libresd_fat_t *fat, uint32_t prev_cluster) {
    uint32_t cluster;
    uint32_t start = fat->last_alloc_cluster;
    uint32_t eoc;
    
    switch (fat->fs_type) {
        case LIBRESD_FS_FAT12: eoc = 0x0FFF; break;
        case LIBRESD_FS_FAT16: eoc = 0xFFFF; break;
        case LIBRESD_FS_FAT32: eoc = 0x0FFFFFFF; break;
        default: return 0;
    }
    
    /* Search for free cluster */
    cluster = start;
    do {
        cluster++;
        if (cluster >= fat->cluster_count + 2) {
            cluster = 2;
        }
        if (cluster == start) {
            return 0;  /* Disk full */
        }
    } while (libresd_fat_read_entry(fat, cluster) != FAT_FREE);
    
    /* Mark as end of chain */
    if (libresd_fat_write_entry(fat, cluster, eoc) != LIBRESD_OK) {
        return 0;
    }
    
    /* Link to previous cluster */
    if (prev_cluster >= 2) {
        if (libresd_fat_write_entry(fat, prev_cluster, cluster) != LIBRESD_OK) {
            return 0;
        }
    }
    
    fat->last_alloc_cluster = cluster;
    if (fat->free_clusters != 0xFFFFFFFF) {
        fat->free_clusters--;
    }
    
    return cluster;
}

libresd_err_t libresd_fat_free_chain(libresd_fat_t *fat, uint32_t cluster) {
    uint32_t next;
    libresd_err_t err;
    
    while (cluster >= 2 && !libresd_fat_is_eoc(fat, cluster)) {
        next = libresd_fat_read_entry(fat, cluster);
        
        err = libresd_fat_write_entry(fat, cluster, FAT_FREE);
        if (err != LIBRESD_OK) return err;
        
        if (fat->free_clusters != 0xFFFFFFFF) {
            fat->free_clusters++;
        }
        
        cluster = next;
    }
    
    return LIBRESD_OK;
}

#endif /* LIBRESD_ENABLE_WRITE */

/*============================================================================
 * MOUNT/UNMOUNT
 *============================================================================*/

libresd_err_t libresd_fat_mount(libresd_fat_t *fat, libresd_sd_t *sd) {
    uint8_t buffer[512];
    uint32_t root_sectors, data_sectors;
    
    if (!fat || !sd) return LIBRESD_ERR_INVALID_PARAM;
    if (!sd->initialized) return LIBRESD_ERR_NOT_MOUNTED;
    
    memset(fat, 0, sizeof(libresd_fat_t));
    fat->sd = sd;
    fat->fat_buffer_sector = 0xFFFFFFFF;
    fat->free_clusters = 0xFFFFFFFF;
    
    /* Read MBR/boot sector */
    if (libresd_sd_read_sector(sd, 0, buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    uint32_t partition_start = 0;
    
    /* Check for MBR */
    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        /* Check first partition entry */
        if (buffer[446 + 4] == 0x01 || buffer[446 + 4] == 0x04 ||
            buffer[446 + 4] == 0x06 || buffer[446 + 4] == 0x0B ||
            buffer[446 + 4] == 0x0C || buffer[446 + 4] == 0x0E) {
            /* FAT partition found */
            partition_start = READ32(buffer, 446 + 8);
            
            if (libresd_sd_read_sector(sd, partition_start, buffer) != LIBRESD_OK) {
                return LIBRESD_ERR_SPI;
            }
        }
    }
    
    /* Verify boot sector signature */
    if (buffer[510] != 0x55 || buffer[511] != 0xAA) {
        return LIBRESD_ERR_NO_FS;
    }
    
    /* Parse BPB */
    fat->bytes_per_sector = READ16(buffer, 11);
    fat->sectors_per_cluster = buffer[13];
    fat->reserved_sectors = READ16(buffer, 14);
    fat->num_fats = buffer[16];
    fat->root_entry_count = READ16(buffer, 17);
    fat->total_sectors = READ16(buffer, 19);
    if (fat->total_sectors == 0) {
        fat->total_sectors = READ32(buffer, 32);
    }
    fat->sectors_per_fat = READ16(buffer, 22);
    if (fat->sectors_per_fat == 0) {
        fat->sectors_per_fat = READ32(buffer, 36);
    }
    
    /* Validate BPB */
    if (fat->bytes_per_sector != 512 || fat->sectors_per_cluster == 0 ||
        fat->num_fats == 0 || fat->reserved_sectors == 0) {
        return LIBRESD_ERR_INVALID_FS;
    }
    
    /* Calculate layout */
    fat->fat_start_sector = partition_start + fat->reserved_sectors;
    root_sectors = ((fat->root_entry_count * 32) + 511) / 512;
    fat->root_start_sector = fat->fat_start_sector + 
                             (fat->num_fats * fat->sectors_per_fat);
    fat->data_start_sector = fat->root_start_sector + root_sectors;
    data_sectors = fat->total_sectors - fat->data_start_sector + partition_start;
    fat->cluster_count = data_sectors / fat->sectors_per_cluster;
    fat->cluster_size = fat->sectors_per_cluster * 512;
    
    /* Determine FAT type */
    if (fat->cluster_count < 4085) {
        fat->fs_type = LIBRESD_FS_FAT12;
    } else if (fat->cluster_count < 65525) {
        fat->fs_type = LIBRESD_FS_FAT16;
    } else {
        fat->fs_type = LIBRESD_FS_FAT32;
        fat->root_cluster = READ32(buffer, 44);
        fat->data_start_sector = fat->root_start_sector;  /* No fixed root */
    }
    
    /* Read volume label */
    uint32_t label_offset = (fat->fs_type == LIBRESD_FS_FAT32) ? 71 : 43;
    memcpy(fat->volume_label, buffer + label_offset, 11);
    fat->volume_label[11] = '\0';
    
    /* Trim trailing spaces */
    for (int i = 10; i >= 0 && fat->volume_label[i] == ' '; i--) {
        fat->volume_label[i] = '\0';
    }
    
    /* Volume serial */
    fat->volume_serial = READ32(buffer, (fat->fs_type == LIBRESD_FS_FAT32) ? 67 : 39);
    
    /* Set root directory as current */
    fat->cwd_cluster = (fat->fs_type == LIBRESD_FS_FAT32) ? fat->root_cluster : 0;
    strcpy(fat->cwd_path, "/");
    
    fat->mounted = true;
    
    LIBRESD_DEBUG_PRINTF("Mounted %s, %lu clusters, cluster size %lu",
                         fat->fs_type == LIBRESD_FS_FAT12 ? "FAT12" :
                         fat->fs_type == LIBRESD_FS_FAT16 ? "FAT16" : "FAT32",
                         fat->cluster_count, fat->cluster_size);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_unmount(libresd_fat_t *fat) {
    if (!fat) return LIBRESD_ERR_INVALID_PARAM;
    
#if LIBRESD_ENABLE_WRITE
    /* Flush FAT buffer */
    if (fat->fat_buffer_dirty && fat->fat_buffer_sector != 0xFFFFFFFF) {
        libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector, fat->fat_buffer);
        
        /* Write to backup FAT */
        if (fat->num_fats > 1) {
            libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector + fat->sectors_per_fat,
                                    fat->fat_buffer);
        }
    }
#endif
    
    fat->mounted = false;
    return LIBRESD_OK;
}

bool libresd_fat_is_mounted(libresd_fat_t *fat) {
    return fat && fat->mounted;
}

libresd_err_t libresd_fat_sync(libresd_fat_t *fat) {
#if LIBRESD_ENABLE_WRITE
    if (!fat || !fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    if (fat->fat_buffer_dirty && fat->fat_buffer_sector != 0xFFFFFFFF) {
        libresd_err_t err = libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector, 
                                                     fat->fat_buffer);
        if (err != LIBRESD_OK) return err;
        
        if (fat->num_fats > 1) {
            err = libresd_sd_write_sector(fat->sd, fat->fat_buffer_sector + fat->sectors_per_fat,
                                          fat->fat_buffer);
            if (err != LIBRESD_OK) return err;
        }
        
        fat->fat_buffer_dirty = false;
    }
#endif
    return LIBRESD_OK;
}

/*============================================================================
 * DIRECTORY OPERATIONS
 *============================================================================*/

libresd_err_t libresd_fat_opendir(libresd_fat_t *fat, libresd_dir_t *dir, 
                                   const char *path) {
    libresd_fileinfo_t info;
    libresd_err_t err;
    uint32_t cluster;
    
    if (!fat || !dir) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    memset(dir, 0, sizeof(libresd_dir_t));
    
    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        /* Root directory */
        cluster = (fat->fs_type == LIBRESD_FS_FAT32) ? fat->root_cluster : 0;
    } else {
        /* Resolve path */
        err = fat_resolve_path(fat, path, &cluster, NULL, NULL, &info);
        if (err != LIBRESD_OK) return err;
        
        if (!(info.attr & LIBRESD_ATTR_DIRECTORY)) {
            return LIBRESD_ERR_NOT_DIR;
        }
    }
    
    dir->first_cluster = cluster;
    dir->current_cluster = cluster;
    
    /* For FAT12/16 root, use fixed sectors */
    if (cluster == 0) {
        dir->current_sector = fat->root_start_sector;
    } else {
        dir->current_sector = libresd_fat_cluster_to_sector(fat, cluster);
    }
    
    dir->entry_offset = 0;
    dir->is_open = true;
    
    /* Read first sector */
    if (libresd_sd_read_sector(fat->sd, dir->current_sector, dir->buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_readdir(libresd_fat_t *fat, libresd_dir_t *dir,
                                   libresd_fileinfo_t *info) {
    fat_dirent_t *entry;
    uint32_t sector_in_cluster = 0;
    uint32_t max_sector;
    
    if (!fat || !dir || !info) return LIBRESD_ERR_INVALID_PARAM;
    if (!dir->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    
#if LIBRESD_ENABLE_LFN
    char lfn_buffer[LIBRESD_MAX_FILENAME];
    int lfn_index = 0;
    bool has_lfn = false;
    lfn_buffer[0] = '\0';
#endif
    
    /* Calculate max sectors for root dir */
    if (dir->first_cluster == 0) {
        max_sector = fat->root_start_sector + 
                     ((fat->root_entry_count * 32) + 511) / 512;
    } else {
        max_sector = 0xFFFFFFFF;  /* Use cluster chain */
    }
    
    while (1) {
        /* Check if we need next sector */
        if (dir->entry_offset >= 512) {
            dir->entry_offset = 0;
            
            if (dir->first_cluster == 0) {
                /* Fixed root directory */
                dir->current_sector++;
                if (dir->current_sector >= max_sector) {
                    return LIBRESD_ERR_EOF;
                }
            } else {
                /* Cluster-based directory */
                sector_in_cluster = dir->current_sector - 
                                    libresd_fat_cluster_to_sector(fat, dir->current_cluster);
                sector_in_cluster++;
                
                if (sector_in_cluster >= fat->sectors_per_cluster) {
                    /* Next cluster */
                    uint32_t next = libresd_fat_next_cluster(fat, dir->current_cluster);
                    if (next == 0) {
                        return LIBRESD_ERR_EOF;
                    }
                    dir->current_cluster = next;
                    dir->current_sector = libresd_fat_cluster_to_sector(fat, next);
                } else {
                    dir->current_sector++;
                }
            }
            
            /* Read new sector */
            if (libresd_sd_read_sector(fat->sd, dir->current_sector, dir->buffer) != LIBRESD_OK) {
                return LIBRESD_ERR_SPI;
            }
        }
        
        entry = (fat_dirent_t *)(dir->buffer + dir->entry_offset);
        dir->entry_offset += FAT_DIRENT_SIZE;
        
        /* End of directory */
        if (entry->name[0] == DIRENT_END) {
            return LIBRESD_ERR_EOF;
        }
        
        /* Deleted entry */
        if (entry->name[0] == DIRENT_FREE) {
#if LIBRESD_ENABLE_LFN
            has_lfn = false;
            lfn_index = 0;
#endif
            continue;
        }
        
#if LIBRESD_ENABLE_LFN
        /* Long filename entry */
        if ((entry->attr & LIBRESD_ATTR_LFN) == LIBRESD_ATTR_LFN) {
            uint8_t *lfn = (uint8_t *)entry;
            int seq = lfn[0] & 0x1F;
            int idx = (seq - 1) * FAT_LFN_ENTRY_CHARS;
            
            if (lfn[0] & 0x40) {
                /* Last LFN entry - start fresh */
                memset(lfn_buffer, 0, sizeof(lfn_buffer));
                has_lfn = true;
            }
            
            /* Extract Unicode characters (simplified - ASCII only) */
            if (idx < LIBRESD_MAX_FILENAME - 1) {
                for (int i = 0; i < 5 && idx < LIBRESD_MAX_FILENAME - 1; i++) {
                    uint16_t c = lfn[1 + i*2] | (lfn[2 + i*2] << 8);
                    if (c && c < 128) lfn_buffer[idx++] = c;
                }
                for (int i = 0; i < 6 && idx < LIBRESD_MAX_FILENAME - 1; i++) {
                    uint16_t c = lfn[14 + i*2] | (lfn[15 + i*2] << 8);
                    if (c && c < 128) lfn_buffer[idx++] = c;
                }
                for (int i = 0; i < 2 && idx < LIBRESD_MAX_FILENAME - 1; i++) {
                    uint16_t c = lfn[28 + i*2] | (lfn[29 + i*2] << 8);
                    if (c && c < 128) lfn_buffer[idx++] = c;
                }
                lfn_index = idx;
            }
            continue;
        }
#endif
        
        /* Volume label - skip */
        if (entry->attr & LIBRESD_ATTR_VOLUME_ID) {
#if LIBRESD_ENABLE_LFN
            has_lfn = false;
#endif
            continue;
        }
        
        /* Found a valid entry! */
        memset(info, 0, sizeof(libresd_fileinfo_t));
        
#if LIBRESD_ENABLE_LFN
        if (has_lfn && lfn_buffer[0]) {
            strncpy(info->name, lfn_buffer, LIBRESD_MAX_FILENAME - 1);
        } else
#endif
        {
            fat_name_to_str(entry->name, info->name);
        }
        
        info->attr = entry->attr;
        info->size = entry->file_size;
        info->first_cluster = ((uint32_t)entry->cluster_hi << 16) | entry->cluster_lo;
        info->dir_sector = dir->current_sector;
        info->dir_offset = dir->entry_offset - FAT_DIRENT_SIZE;
        
        /* Parse timestamps */
        info->created.year = LIBRESD_FAT_YEAR(entry->create_date);
        info->created.month = LIBRESD_FAT_MONTH(entry->create_date);
        info->created.day = LIBRESD_FAT_DAY(entry->create_date);
        info->created.hour = LIBRESD_FAT_HOUR(entry->create_time);
        info->created.minute = LIBRESD_FAT_MIN(entry->create_time);
        info->created.second = LIBRESD_FAT_SEC(entry->create_time);
        
        info->modified.year = LIBRESD_FAT_YEAR(entry->modify_date);
        info->modified.month = LIBRESD_FAT_MONTH(entry->modify_date);
        info->modified.day = LIBRESD_FAT_DAY(entry->modify_date);
        info->modified.hour = LIBRESD_FAT_HOUR(entry->modify_time);
        info->modified.minute = LIBRESD_FAT_MIN(entry->modify_time);
        info->modified.second = LIBRESD_FAT_SEC(entry->modify_time);
        
#if LIBRESD_ENABLE_LFN
        has_lfn = false;
        lfn_index = 0;
#endif
        
        return LIBRESD_OK;
    }
}

void libresd_fat_closedir(libresd_dir_t *dir) {
    if (dir) {
        dir->is_open = false;
    }
}

libresd_err_t libresd_fat_chdir(libresd_fat_t *fat, const char *path) {
    libresd_fileinfo_t info;
    libresd_err_t err;
    uint32_t cluster;
    
    if (!fat) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        /* Go to root */
        fat->cwd_cluster = (fat->fs_type == LIBRESD_FS_FAT32) ? fat->root_cluster : 0;
        strcpy(fat->cwd_path, "/");
        return LIBRESD_OK;
    }
    
    err = fat_resolve_path(fat, path, &cluster, NULL, NULL, &info);
    if (err != LIBRESD_OK) return err;
    
    if (!(info.attr & LIBRESD_ATTR_DIRECTORY)) {
        return LIBRESD_ERR_NOT_DIR;
    }
    
    fat->cwd_cluster = info.first_cluster;
    
    /* Update path string */
    if (path[0] == '/') {
        strncpy(fat->cwd_path, path, LIBRESD_MAX_PATH - 1);
    } else {
        size_t len = strlen(fat->cwd_path);
        if (len > 1) {
            strncat(fat->cwd_path, "/", LIBRESD_MAX_PATH - len - 1);
            len++;
        }
        strncat(fat->cwd_path, path, LIBRESD_MAX_PATH - len - 1);
    }
    
    return LIBRESD_OK;
}

char* libresd_fat_getcwd(libresd_fat_t *fat, char *buffer, size_t size) {
    if (!fat || !buffer || size == 0) return NULL;
    
    strncpy(buffer, fat->cwd_path, size - 1);
    buffer[size - 1] = '\0';
    
    return buffer;
}

/*============================================================================
 * PATH RESOLUTION
 *============================================================================*/

libresd_err_t fat_resolve_path(libresd_fat_t *fat, const char *path,
                                       uint32_t *cluster, uint32_t *dir_sector,
                                       uint16_t *dir_offset, libresd_fileinfo_t *info) {
    libresd_dir_t dir;
    libresd_fileinfo_t entry;
    libresd_err_t err;
    char component[LIBRESD_MAX_FILENAME];
    const char *p = path;
    uint32_t current_cluster;
    bool found;
    
    /* Start from root or cwd */
    if (*p == '/') {
        current_cluster = (fat->fs_type == LIBRESD_FS_FAT32) ? fat->root_cluster : 0;
        p++;
    } else {
        current_cluster = fat->cwd_cluster;
    }
    
    /* Handle empty path */
    if (*p == '\0') {
        if (cluster) *cluster = current_cluster;
        if (info) {
            memset(info, 0, sizeof(libresd_fileinfo_t));
            info->attr = LIBRESD_ATTR_DIRECTORY;
            info->first_cluster = current_cluster;
        }
        return LIBRESD_OK;
    }
    
    while (*p) {
        /* Extract path component */
        int i = 0;
        while (*p && *p != '/' && i < LIBRESD_MAX_FILENAME - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        /* Skip trailing slashes */
        while (*p == '/') p++;
        
        /* Handle . and .. */
        if (strcmp(component, ".") == 0) {
            continue;
        }
        
        if (strcmp(component, "..") == 0) {
            /* For simplicity, .. just goes to root if we don't track parent */
            /* A full implementation would need parent tracking */
            current_cluster = (fat->fs_type == LIBRESD_FS_FAT32) ? fat->root_cluster : 0;
            continue;
        }
        
        /* Search directory */
        memset(&dir, 0, sizeof(dir));
        dir.first_cluster = current_cluster;
        dir.current_cluster = current_cluster;
        dir.current_sector = (current_cluster == 0) ? 
                             fat->root_start_sector :
                             libresd_fat_cluster_to_sector(fat, current_cluster);
        dir.is_open = true;
        
        if (libresd_sd_read_sector(fat->sd, dir.current_sector, dir.buffer) != LIBRESD_OK) {
            return LIBRESD_ERR_SPI;
        }
        
        found = false;
        while ((err = libresd_fat_readdir(fat, &dir, &entry)) == LIBRESD_OK) {
            /* Case-insensitive compare */
            if (strcasecmp(entry.name, component) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            return LIBRESD_ERR_NOT_FOUND;
        }
        
        /* If more path components, this must be a directory */
        if (*p && !(entry.attr & LIBRESD_ATTR_DIRECTORY)) {
            return LIBRESD_ERR_NOT_DIR;
        }
        
        current_cluster = entry.first_cluster;
        
        /* Save info for last component */
        if (*p == '\0') {
            if (cluster) *cluster = entry.first_cluster;
            if (dir_sector) *dir_sector = entry.dir_sector;
            if (dir_offset) *dir_offset = entry.dir_offset;
            if (info) memcpy(info, &entry, sizeof(libresd_fileinfo_t));
        }
    }
    
    return LIBRESD_OK;
}

/*============================================================================
 * FILE OPERATIONS (continuing in next part...)
 *============================================================================*/

libresd_err_t libresd_fat_stat(libresd_fat_t *fat, const char *path,
                                libresd_fileinfo_t *info) {
    if (!fat || !path || !info) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    return fat_resolve_path(fat, path, NULL, NULL, NULL, info);
}

bool libresd_fat_exists(libresd_fat_t *fat, const char *path) {
    libresd_fileinfo_t info;
    return fat_resolve_path(fat, path, NULL, NULL, NULL, &info) == LIBRESD_OK;
}

libresd_err_t libresd_fat_get_info(libresd_fat_t *fat, libresd_info_t *info) {
    if (!fat || !info) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    memset(info, 0, sizeof(libresd_info_t));
    
    info->card_type = fat->sd->type;
    info->card_size = fat->sd->capacity;
    info->sector_count = fat->sd->sector_count;
    
    info->fs_type = fat->fs_type;
    strncpy(info->volume_label, fat->volume_label, sizeof(info->volume_label) - 1);
    info->volume_serial = fat->volume_serial;
    
    info->cluster_size = fat->cluster_size;
    info->total_clusters = fat->cluster_count;
    info->total_bytes = (uint64_t)fat->cluster_count * fat->cluster_size;
    
    /* Free space calculation would require scanning FAT */
    /* For now, return -1 for unknown */
    info->free_clusters = fat->free_clusters;
    if (fat->free_clusters != 0xFFFFFFFF) {
        info->free_bytes = (uint64_t)fat->free_clusters * fat->cluster_size;
        info->used_bytes = info->total_bytes - info->free_bytes;
    }
    
    return LIBRESD_OK;
}

uint64_t libresd_fat_get_free(libresd_fat_t *fat) {
    if (!fat || !fat->mounted) return 0;
    
    /* If we haven't calculated free space yet, do it now */
    if (fat->free_clusters == 0xFFFFFFFF) {
        uint32_t free = 0;
        for (uint32_t c = 2; c < fat->cluster_count + 2; c++) {
            if (libresd_fat_read_entry(fat, c) == FAT_FREE) {
                free++;
            }
        }
        fat->free_clusters = free;
    }
    
    return (uint64_t)fat->free_clusters * fat->cluster_size;
}

const char* libresd_fat_get_label(libresd_fat_t *fat) {
    return fat ? fat->volume_label : "";
}
