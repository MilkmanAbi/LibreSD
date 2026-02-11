/**
 * @file libresd_file.c
 * @brief LibreSD File Operations Implementation
 * 
 * File open/close/read/write/seek operations.
 */

#include "libresd_fat.h"
#include "libresd_hal.h"
#include <string.h>

/*============================================================================
 * FILE OPERATIONS
 *============================================================================*/

libresd_err_t libresd_fat_open(libresd_fat_t *fat, libresd_file_t *file,
                                const char *path, uint8_t mode) {
    libresd_fileinfo_t info;
    libresd_err_t err;
    uint32_t dir_sector;
    uint16_t dir_offset;
    
    if (!fat || !file || !path) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    memset(file, 0, sizeof(libresd_file_t));
    file->buffer_sector = 0xFFFFFFFF;
    
#if !LIBRESD_ENABLE_WRITE
    if (mode & (LIBRESD_WRITE | LIBRESD_CREATE | LIBRESD_TRUNCATE)) {
        return LIBRESD_ERR_NOT_SUPPORTED;
    }
#endif
    
    /* Try to find existing file */
    err = fat_resolve_path(fat, path, NULL, &dir_sector, &dir_offset, &info);
    
    if (err == LIBRESD_OK) {
        /* File exists */
        if (info.attr & LIBRESD_ATTR_DIRECTORY) {
            return LIBRESD_ERR_NOT_FILE;
        }
        
        if ((mode & LIBRESD_EXCL) && (mode & LIBRESD_CREATE)) {
            return LIBRESD_ERR_EXISTS;
        }
        
        file->first_cluster = info.first_cluster;
        file->current_cluster = info.first_cluster;
        file->file_size = info.size;
        file->dir_sector = dir_sector;
        file->dir_offset = dir_offset;
        
#if LIBRESD_ENABLE_WRITE
        if (mode & LIBRESD_TRUNCATE) {
            /* Truncate existing file */
            if (file->first_cluster >= 2) {
                libresd_fat_free_chain(fat, file->first_cluster);
            }
            file->first_cluster = 0;
            file->current_cluster = 0;
            file->file_size = 0;
            
            /* Update directory entry */
            uint8_t buffer[512];
            if (libresd_sd_read_sector(fat->sd, dir_sector, buffer) == LIBRESD_OK) {
                fat_dirent_t *entry = (fat_dirent_t *)(buffer + dir_offset);
                entry->cluster_hi = 0;
                entry->cluster_lo = 0;
                entry->file_size = 0;
                libresd_sd_write_sector(fat->sd, dir_sector, buffer);
            }
        }
#endif
    } else if (err == LIBRESD_ERR_NOT_FOUND && (mode & LIBRESD_CREATE)) {
#if LIBRESD_ENABLE_WRITE
        /* Create new file */
        err = libresd_fat_create_file(fat, path, 0, &dir_sector, &dir_offset);
        if (err != LIBRESD_OK) return err;
        
        file->first_cluster = 0;
        file->current_cluster = 0;
        file->file_size = 0;
        file->dir_sector = dir_sector;
        file->dir_offset = dir_offset;
#else
        return LIBRESD_ERR_NOT_SUPPORTED;
#endif
    } else {
        return err;
    }
    
    file->mode = mode;
    file->position = 0;
    file->cluster_offset = 0;
    file->is_open = true;
    
    /* For append mode, seek to end */
    if (mode & LIBRESD_APPEND) {
        file->position = file->file_size;
        
        /* Find last cluster */
        if (file->first_cluster >= 2) {
            uint32_t cluster = file->first_cluster;
            uint32_t pos = 0;
            while (pos + fat->cluster_size <= file->file_size) {
                uint32_t next = libresd_fat_next_cluster(fat, cluster);
                if (next == 0) break;
                cluster = next;
                pos += fat->cluster_size;
            }
            file->current_cluster = cluster;
            file->cluster_offset = file->file_size - pos;
        }
    }
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_close(libresd_fat_t *fat, libresd_file_t *file) {
    if (!fat || !file) return LIBRESD_ERR_INVALID_PARAM;
    if (!file->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    
#if LIBRESD_ENABLE_WRITE
    /* Flush buffer if dirty */
    if (file->buffer_dirty && file->buffer_sector != 0xFFFFFFFF) {
        libresd_sd_write_sector(fat->sd, file->buffer_sector, file->buffer);
    }
    
    /* Update directory entry if file was modified */
    if (file->mode & LIBRESD_WRITE) {
        uint8_t buffer[512];
        if (libresd_sd_read_sector(fat->sd, file->dir_sector, buffer) == LIBRESD_OK) {
            fat_dirent_t *entry = (fat_dirent_t *)(buffer + file->dir_offset);
            
            entry->cluster_hi = (file->first_cluster >> 16) & 0xFFFF;
            entry->cluster_lo = file->first_cluster & 0xFFFF;
            entry->file_size = file->file_size;
            
            /* Update modification time */
            libresd_datetime_t dt;
            libresd_hal_get_datetime(&dt);
            entry->modify_date = LIBRESD_FAT_DATE(dt.year, dt.month, dt.day);
            entry->modify_time = LIBRESD_FAT_TIME(dt.hour, dt.minute, dt.second);
            
            libresd_sd_write_sector(fat->sd, file->dir_sector, buffer);
        }
    }
#endif
    
    file->is_open = false;
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_read(libresd_fat_t *fat, libresd_file_t *file,
                                void *buffer, uint32_t size, uint32_t *bytes_read) {
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t total_read = 0;
    uint32_t to_read;
    uint32_t sector, offset_in_sector;
    libresd_err_t err;
    
    if (!fat || !file || !buffer) return LIBRESD_ERR_INVALID_PARAM;
    if (!file->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    if (!(file->mode & LIBRESD_READ)) return LIBRESD_ERR_READ_ONLY;
    
    /* Limit to remaining file size */
    if (file->position >= file->file_size) {
        if (bytes_read) *bytes_read = 0;
        return LIBRESD_ERR_EOF;
    }
    
    if (file->position + size > file->file_size) {
        size = file->file_size - file->position;
    }
    
    while (size > 0) {
        /* Calculate sector within current cluster */
        uint32_t offset_in_cluster = file->cluster_offset;
        uint32_t sector_in_cluster = offset_in_cluster / 512;
        offset_in_sector = offset_in_cluster % 512;
        
        if (file->current_cluster < 2) {
            /* No more clusters */
            break;
        }
        
        sector = libresd_fat_cluster_to_sector(fat, file->current_cluster) + sector_in_cluster;
        
        /* Read sector if not in buffer */
        if (file->buffer_sector != sector) {
#if LIBRESD_ENABLE_WRITE
            /* Flush dirty buffer */
            if (file->buffer_dirty) {
                err = libresd_sd_write_sector(fat->sd, file->buffer_sector, file->buffer);
                if (err != LIBRESD_OK) return err;
                file->buffer_dirty = false;
            }
#endif
            err = libresd_sd_read_sector(fat->sd, sector, file->buffer);
            if (err != LIBRESD_OK) return err;
            file->buffer_sector = sector;
        }
        
        /* Calculate how much to read from this sector */
        to_read = 512 - offset_in_sector;
        if (to_read > size) to_read = size;
        
        /* Copy data */
        memcpy(dst, file->buffer + offset_in_sector, to_read);
        
        dst += to_read;
        size -= to_read;
        total_read += to_read;
        file->position += to_read;
        file->cluster_offset += to_read;
        
        /* Check if we need to move to next cluster */
        if (file->cluster_offset >= fat->cluster_size) {
            uint32_t next = libresd_fat_next_cluster(fat, file->current_cluster);
            if (next == 0) {
                /* End of file chain */
                break;
            }
            file->current_cluster = next;
            file->cluster_offset = 0;
        }
    }
    
    if (bytes_read) *bytes_read = total_read;
    
    return (total_read > 0) ? LIBRESD_OK : LIBRESD_ERR_EOF;
}

#if LIBRESD_ENABLE_WRITE

/**
 * @brief Create a new file entry in directory
 */
libresd_err_t libresd_fat_create_file(libresd_fat_t *fat, const char *path,
                                       uint8_t attr, uint32_t *out_dir_sector,
                                       uint16_t *out_dir_offset) {
    libresd_dir_t dir;
    uint8_t buffer[512];
    fat_dirent_t *entry;
    char parent_path[LIBRESD_MAX_PATH];
    char filename[LIBRESD_MAX_FILENAME];
    uint8_t fat_name[11];
    const char *last_slash;
    libresd_err_t err;
    uint32_t parent_cluster;
    libresd_fileinfo_t parent_info;
    
    /* Split path into parent and filename */
    last_slash = strrchr(path, '/');
    if (last_slash) {
        size_t parent_len = last_slash - path;
        if (parent_len == 0) {
            strcpy(parent_path, "/");
        } else {
            strncpy(parent_path, path, parent_len);
            parent_path[parent_len] = '\0';
        }
        strncpy(filename, last_slash + 1, LIBRESD_MAX_FILENAME - 1);
    } else {
        strcpy(parent_path, "");
        strncpy(filename, path, LIBRESD_MAX_FILENAME - 1);
    }
    filename[LIBRESD_MAX_FILENAME - 1] = '\0';
    
    /* Convert filename to 8.3 */
    if (!str_to_fat_name(filename, fat_name)) {
        return LIBRESD_ERR_INVALID_NAME;
    }
    
    /* Resolve parent directory */
    if (parent_path[0]) {
        err = fat_resolve_path(fat, parent_path, &parent_cluster, NULL, NULL, &parent_info);
        if (err != LIBRESD_OK) return err;
        if (!(parent_info.attr & LIBRESD_ATTR_DIRECTORY)) {
            return LIBRESD_ERR_NOT_DIR;
        }
    } else {
        parent_cluster = fat->cwd_cluster;
    }
    
    /* Open parent directory to find free entry */
    memset(&dir, 0, sizeof(dir));
    dir.first_cluster = parent_cluster;
    dir.current_cluster = parent_cluster;
    dir.current_sector = (parent_cluster == 0) ?
                         fat->root_start_sector :
                         libresd_fat_cluster_to_sector(fat, parent_cluster);
    dir.is_open = true;
    
    if (libresd_sd_read_sector(fat->sd, dir.current_sector, dir.buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    /* Search for free entry */
    uint32_t max_entries = (parent_cluster == 0) ? fat->root_entry_count : 0xFFFFFFFF;
    uint32_t entry_count = 0;
    bool found = false;
    
    while (!found) {
        if (dir.entry_offset >= 512) {
            /* Need next sector */
            dir.entry_offset = 0;
            
            if (parent_cluster == 0) {
                /* Fixed root */
                dir.current_sector++;
                if (entry_count >= max_entries) {
                    return LIBRESD_ERR_ROOT_FULL;
                }
            } else {
                /* Cluster-based */
                uint32_t sector_in_cluster = dir.current_sector -
                    libresd_fat_cluster_to_sector(fat, dir.current_cluster);
                sector_in_cluster++;
                
                if (sector_in_cluster >= fat->sectors_per_cluster) {
                    uint32_t next = libresd_fat_next_cluster(fat, dir.current_cluster);
                    if (next == 0) {
                        /* Allocate new cluster for directory */
                        next = libresd_fat_alloc_cluster(fat, dir.current_cluster);
                        if (next == 0) return LIBRESD_ERR_FULL;
                        
                        /* Zero the new cluster */
                        memset(buffer, 0, 512);
                        uint32_t new_sector = libresd_fat_cluster_to_sector(fat, next);
                        for (uint32_t i = 0; i < fat->sectors_per_cluster; i++) {
                            libresd_sd_write_sector(fat->sd, new_sector + i, buffer);
                        }
                    }
                    dir.current_cluster = next;
                    dir.current_sector = libresd_fat_cluster_to_sector(fat, next);
                } else {
                    dir.current_sector++;
                }
            }
            
            if (libresd_sd_read_sector(fat->sd, dir.current_sector, dir.buffer) != LIBRESD_OK) {
                return LIBRESD_ERR_SPI;
            }
        }
        
        entry = (fat_dirent_t *)(dir.buffer + dir.entry_offset);
        
        if (entry->name[0] == DIRENT_FREE || entry->name[0] == DIRENT_END) {
            found = true;
            break;
        }
        
        dir.entry_offset += FAT_DIRENT_SIZE;
        entry_count++;
    }
    
    /* Fill in the entry */
    memset(entry, 0, FAT_DIRENT_SIZE);
    memcpy(entry->name, fat_name, 11);
    entry->attr = attr | LIBRESD_ATTR_ARCHIVE;
    entry->cluster_hi = 0;
    entry->cluster_lo = 0;
    entry->file_size = 0;
    
    /* Set timestamps */
    libresd_datetime_t dt;
    libresd_hal_get_datetime(&dt);
    entry->create_date = LIBRESD_FAT_DATE(dt.year, dt.month, dt.day);
    entry->create_time = LIBRESD_FAT_TIME(dt.hour, dt.minute, dt.second);
    entry->modify_date = entry->create_date;
    entry->modify_time = entry->create_time;
    entry->access_date = entry->create_date;
    
    /* Write the sector */
    err = libresd_sd_write_sector(fat->sd, dir.current_sector, dir.buffer);
    if (err != LIBRESD_OK) return err;
    
    if (out_dir_sector) *out_dir_sector = dir.current_sector;
    if (out_dir_offset) *out_dir_offset = dir.entry_offset;
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_write(libresd_fat_t *fat, libresd_file_t *file,
                                 const void *buffer, uint32_t size,
                                 uint32_t *bytes_written) {
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t total_written = 0;
    uint32_t to_write;
    uint32_t sector, offset_in_sector;
    libresd_err_t err;
    
    if (!fat || !file || !buffer) return LIBRESD_ERR_INVALID_PARAM;
    if (!file->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    if (!(file->mode & LIBRESD_WRITE) && !(file->mode & LIBRESD_APPEND)) {
        return LIBRESD_ERR_READ_ONLY;
    }
    
    while (size > 0) {
        /* Allocate cluster if needed */
        if (file->current_cluster < 2) {
            uint32_t new_cluster = libresd_fat_alloc_cluster(fat, 0);
            if (new_cluster == 0) {
                err = LIBRESD_ERR_FULL;
                break;
            }
            
            if (file->first_cluster < 2) {
                file->first_cluster = new_cluster;
            }
            file->current_cluster = new_cluster;
            file->cluster_offset = 0;
            
            /* Zero the new cluster */
            memset(file->buffer, 0, 512);
            sector = libresd_fat_cluster_to_sector(fat, new_cluster);
            for (uint32_t i = 0; i < fat->sectors_per_cluster; i++) {
                libresd_sd_write_sector(fat->sd, sector + i, file->buffer);
            }
        }
        
        /* Check if we need to allocate next cluster */
        if (file->cluster_offset >= fat->cluster_size) {
            uint32_t next = libresd_fat_next_cluster(fat, file->current_cluster);
            if (next == 0) {
                next = libresd_fat_alloc_cluster(fat, file->current_cluster);
                if (next == 0) {
                    err = LIBRESD_ERR_FULL;
                    break;
                }
            }
            file->current_cluster = next;
            file->cluster_offset = 0;
        }
        
        /* Calculate sector */
        uint32_t offset_in_cluster = file->cluster_offset;
        uint32_t sector_in_cluster = offset_in_cluster / 512;
        offset_in_sector = offset_in_cluster % 512;
        
        sector = libresd_fat_cluster_to_sector(fat, file->current_cluster) + sector_in_cluster;
        
        /* Load sector if partial write or different sector */
        if (file->buffer_sector != sector) {
            /* Flush dirty buffer */
            if (file->buffer_dirty) {
                err = libresd_sd_write_sector(fat->sd, file->buffer_sector, file->buffer);
                if (err != LIBRESD_OK) return err;
                file->buffer_dirty = false;
            }
            
            /* Read sector if partial write */
            if (offset_in_sector != 0 || size < 512) {
                err = libresd_sd_read_sector(fat->sd, sector, file->buffer);
                if (err != LIBRESD_OK) return err;
            }
            file->buffer_sector = sector;
        }
        
        /* Calculate how much to write to this sector */
        to_write = 512 - offset_in_sector;
        if (to_write > size) to_write = size;
        
        /* Copy data */
        memcpy(file->buffer + offset_in_sector, src, to_write);
        file->buffer_dirty = true;
        
        src += to_write;
        size -= to_write;
        total_written += to_write;
        file->position += to_write;
        file->cluster_offset += to_write;
        
        /* Update file size */
        if (file->position > file->file_size) {
            file->file_size = file->position;
        }
    }
    
    if (bytes_written) *bytes_written = total_written;
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_flush(libresd_fat_t *fat, libresd_file_t *file) {
    libresd_err_t err;
    
    if (!fat || !file) return LIBRESD_ERR_INVALID_PARAM;
    if (!file->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    
    /* Flush file buffer */
    if (file->buffer_dirty && file->buffer_sector != 0xFFFFFFFF) {
        err = libresd_sd_write_sector(fat->sd, file->buffer_sector, file->buffer);
        if (err != LIBRESD_OK) return err;
        file->buffer_dirty = false;
    }
    
    /* Flush FAT */
    return libresd_fat_sync(fat);
}

libresd_err_t libresd_fat_truncate(libresd_fat_t *fat, libresd_file_t *file) {
    if (!fat || !file) return LIBRESD_ERR_INVALID_PARAM;
    if (!file->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    if (!(file->mode & LIBRESD_WRITE)) return LIBRESD_ERR_READ_ONLY;
    
    /* Free clusters after current position */
    if (file->current_cluster >= 2 && file->position < file->file_size) {
        uint32_t eoc;
        switch (fat->fs_type) {
            case LIBRESD_FS_FAT12: eoc = 0x0FFF; break;
            case LIBRESD_FS_FAT16: eoc = 0xFFFF; break;
            default: eoc = 0x0FFFFFFF; break;
        }
        
        if (file->cluster_offset == 0 && file->position > 0) {
            /* Free from current cluster onwards */
            libresd_fat_free_chain(fat, file->current_cluster);
            
            /* Find previous cluster */
            if (file->first_cluster != file->current_cluster) {
                uint32_t prev = file->first_cluster;
                while (libresd_fat_next_cluster(fat, prev) != file->current_cluster) {
                    prev = libresd_fat_next_cluster(fat, prev);
                }
                libresd_fat_write_entry(fat, prev, eoc);
                file->current_cluster = prev;
            } else {
                file->first_cluster = 0;
                file->current_cluster = 0;
            }
        } else {
            /* Free from next cluster onwards */
            uint32_t next = libresd_fat_next_cluster(fat, file->current_cluster);
            if (next >= 2) {
                libresd_fat_free_chain(fat, next);
                libresd_fat_write_entry(fat, file->current_cluster, eoc);
            }
        }
    }
    
    file->file_size = file->position;
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_unlink(libresd_fat_t *fat, const char *path) {
    libresd_fileinfo_t info;
    libresd_err_t err;
    uint32_t dir_sector;
    uint16_t dir_offset;
    uint8_t buffer[512];
    
    if (!fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    /* Find the file */
    err = fat_resolve_path(fat, path, NULL, &dir_sector, &dir_offset, &info);
    if (err != LIBRESD_OK) return err;
    
    if (info.attr & LIBRESD_ATTR_DIRECTORY) {
        return LIBRESD_ERR_NOT_FILE;
    }
    
    /* Free cluster chain */
    if (info.first_cluster >= 2) {
        libresd_fat_free_chain(fat, info.first_cluster);
    }
    
    /* Mark directory entry as deleted */
    if (libresd_sd_read_sector(fat->sd, dir_sector, buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    buffer[dir_offset] = DIRENT_FREE;
    
    /* TODO: Also mark LFN entries as deleted */
    
    return libresd_sd_write_sector(fat->sd, dir_sector, buffer);
}

libresd_err_t libresd_fat_rename(libresd_fat_t *fat, const char *old_path,
                                  const char *new_path) {
    /* This is a simplified implementation - doesn't handle cross-directory moves */
    libresd_fileinfo_t info;
    libresd_err_t err;
    uint32_t dir_sector;
    uint16_t dir_offset;
    uint8_t buffer[512];
    uint8_t new_name[11];
    const char *new_filename;
    
    if (!fat || !old_path || !new_path) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    /* Check if new name already exists */
    if (libresd_fat_exists(fat, new_path)) {
        return LIBRESD_ERR_EXISTS;
    }
    
    /* Find the old file */
    err = fat_resolve_path(fat, old_path, NULL, &dir_sector, &dir_offset, &info);
    if (err != LIBRESD_OK) return err;
    
    /* Get just the filename part of new path */
    new_filename = strrchr(new_path, '/');
    new_filename = new_filename ? new_filename + 1 : new_path;
    
    /* Convert to 8.3 name */
    if (!str_to_fat_name(new_filename, new_name)) {
        return LIBRESD_ERR_INVALID_NAME;
    }
    
    /* Update directory entry */
    if (libresd_sd_read_sector(fat->sd, dir_sector, buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    memcpy(buffer + dir_offset, new_name, 11);
    
    return libresd_sd_write_sector(fat->sd, dir_sector, buffer);
}

#if LIBRESD_ENABLE_DIRS

libresd_err_t libresd_fat_mkdir(libresd_fat_t *fat, const char *path) {
    libresd_err_t err;
    uint32_t dir_sector, cluster;
    uint16_t dir_offset;
    uint8_t buffer[512];
    fat_dirent_t *entry;
    
    if (!fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    /* Check if already exists */
    if (libresd_fat_exists(fat, path)) {
        return LIBRESD_ERR_EXISTS;
    }
    
    /* Create directory entry */
    err = libresd_fat_create_file(fat, path, LIBRESD_ATTR_DIRECTORY, 
                                   &dir_sector, &dir_offset);
    if (err != LIBRESD_OK) return err;
    
    /* Allocate cluster for directory contents */
    cluster = libresd_fat_alloc_cluster(fat, 0);
    if (cluster == 0) {
        /* Undo - delete the entry */
        libresd_sd_read_sector(fat->sd, dir_sector, buffer);
        buffer[dir_offset] = DIRENT_FREE;
        libresd_sd_write_sector(fat->sd, dir_sector, buffer);
        return LIBRESD_ERR_FULL;
    }
    
    /* Update directory entry with cluster */
    if (libresd_sd_read_sector(fat->sd, dir_sector, buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    entry = (fat_dirent_t *)(buffer + dir_offset);
    entry->cluster_hi = (cluster >> 16) & 0xFFFF;
    entry->cluster_lo = cluster & 0xFFFF;
    
    if (libresd_sd_write_sector(fat->sd, dir_sector, buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    /* Initialize directory contents with . and .. entries */
    memset(buffer, 0, 512);
    
    /* . entry (self) */
    entry = (fat_dirent_t *)buffer;
    memset(entry->name, ' ', 11);
    entry->name[0] = '.';
    entry->attr = LIBRESD_ATTR_DIRECTORY;
    entry->cluster_hi = (cluster >> 16) & 0xFFFF;
    entry->cluster_lo = cluster & 0xFFFF;
    
    libresd_datetime_t dt;
    libresd_hal_get_datetime(&dt);
    entry->create_date = LIBRESD_FAT_DATE(dt.year, dt.month, dt.day);
    entry->create_time = LIBRESD_FAT_TIME(dt.hour, dt.minute, dt.second);
    entry->modify_date = entry->create_date;
    entry->modify_time = entry->create_time;
    
    /* .. entry (parent) */
    entry = (fat_dirent_t *)(buffer + FAT_DIRENT_SIZE);
    memset(entry->name, ' ', 11);
    entry->name[0] = '.';
    entry->name[1] = '.';
    entry->attr = LIBRESD_ATTR_DIRECTORY;
    /* Parent cluster would need to be resolved - simplified here */
    entry->cluster_hi = 0;
    entry->cluster_lo = 0;
    entry->create_date = LIBRESD_FAT_DATE(dt.year, dt.month, dt.day);
    entry->create_time = LIBRESD_FAT_TIME(dt.hour, dt.minute, dt.second);
    entry->modify_date = entry->create_date;
    entry->modify_time = entry->create_time;
    
    /* Write all sectors in the cluster */
    uint32_t sector = libresd_fat_cluster_to_sector(fat, cluster);
    for (uint32_t i = 0; i < fat->sectors_per_cluster; i++) {
        if (libresd_sd_write_sector(fat->sd, sector + i, 
                                    (i == 0) ? buffer : (uint8_t[512]){0}) != LIBRESD_OK) {
            return LIBRESD_ERR_SPI;
        }
    }
    
    return LIBRESD_OK;
}

libresd_err_t libresd_fat_rmdir(libresd_fat_t *fat, const char *path) {
    libresd_fileinfo_t info;
    libresd_dir_t dir;
    libresd_err_t err;
    uint32_t dir_sector;
    uint16_t dir_offset;
    uint8_t buffer[512];
    
    if (!fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    if (!fat->mounted) return LIBRESD_ERR_NOT_MOUNTED;
    
    /* Find the directory */
    err = fat_resolve_path(fat, path, NULL, &dir_sector, &dir_offset, &info);
    if (err != LIBRESD_OK) return err;
    
    if (!(info.attr & LIBRESD_ATTR_DIRECTORY)) {
        return LIBRESD_ERR_NOT_DIR;
    }
    
    /* Check if directory is empty (only . and ..) */
    err = libresd_fat_opendir(fat, &dir, path);
    if (err != LIBRESD_OK) return err;
    
    libresd_fileinfo_t child;
    int count = 0;
    while (libresd_fat_readdir(fat, &dir, &child) == LIBRESD_OK) {
        if (child.name[0] != '.') {
            libresd_fat_closedir(&dir);
            return LIBRESD_ERR_DIR_NOT_EMPTY;
        }
        count++;
    }
    libresd_fat_closedir(&dir);
    
    /* Free cluster chain */
    if (info.first_cluster >= 2) {
        libresd_fat_free_chain(fat, info.first_cluster);
    }
    
    /* Mark directory entry as deleted */
    if (libresd_sd_read_sector(fat->sd, dir_sector, buffer) != LIBRESD_OK) {
        return LIBRESD_ERR_SPI;
    }
    
    buffer[dir_offset] = DIRENT_FREE;
    
    return libresd_sd_write_sector(fat->sd, dir_sector, buffer);
}

#endif /* LIBRESD_ENABLE_DIRS */

#endif /* LIBRESD_ENABLE_WRITE */

/*============================================================================
 * SEEK / TELL / EOF
 *============================================================================*/

libresd_err_t libresd_fat_seek(libresd_fat_t *fat, libresd_file_t *file,
                                int32_t offset, libresd_seek_t whence) {
    uint32_t new_pos;
    
    if (!fat || !file) return LIBRESD_ERR_INVALID_PARAM;
    if (!file->is_open) return LIBRESD_ERR_INVALID_HANDLE;
    
    /* Calculate new position */
    switch (whence) {
        case LIBRESD_SEEK_SET:
            if (offset < 0) return LIBRESD_ERR_SEEK;
            new_pos = offset;
            break;
        case LIBRESD_SEEK_CUR:
            if (offset < 0 && (uint32_t)(-offset) > file->position) {
                return LIBRESD_ERR_SEEK;
            }
            new_pos = file->position + offset;
            break;
        case LIBRESD_SEEK_END:
            if (offset < 0 && (uint32_t)(-offset) > file->file_size) {
                return LIBRESD_ERR_SEEK;
            }
            new_pos = file->file_size + offset;
            break;
        default:
            return LIBRESD_ERR_INVALID_PARAM;
    }
    
    /* Limit to file size for read mode */
    if (!(file->mode & LIBRESD_WRITE) && new_pos > file->file_size) {
        new_pos = file->file_size;
    }
    
    /* If seeking backwards or to start, reset to beginning */
    if (new_pos < file->position || new_pos == 0) {
        file->current_cluster = file->first_cluster;
        file->cluster_offset = 0;
        file->position = 0;
    }
    
    /* Walk cluster chain to find position */
    while (file->position < new_pos && file->current_cluster >= 2) {
        uint32_t remaining_in_cluster = fat->cluster_size - file->cluster_offset;
        uint32_t to_advance = new_pos - file->position;
        
        if (to_advance >= remaining_in_cluster) {
            /* Move to next cluster */
            uint32_t next = libresd_fat_next_cluster(fat, file->current_cluster);
            if (next == 0) {
                /* Past end of file */
                file->position += remaining_in_cluster;
                file->cluster_offset = 0;
                break;
            }
            file->current_cluster = next;
            file->position += remaining_in_cluster;
            file->cluster_offset = 0;
        } else {
            /* Stay in current cluster */
            file->position = new_pos;
            file->cluster_offset += to_advance;
        }
    }
    
    return LIBRESD_OK;
}

uint32_t libresd_fat_tell(libresd_file_t *file) {
    return file ? file->position : 0;
}

bool libresd_fat_eof(libresd_file_t *file) {
    return !file || file->position >= file->file_size;
}

uint32_t libresd_fat_size(libresd_file_t *file) {
    return file ? file->file_size : 0;
}
