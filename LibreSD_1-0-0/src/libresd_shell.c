/**
 * @file libresd_shell.c
 * @brief LibreSD Shell Commands Implementation
 * 
 * High-level shell-like commands for easy filesystem interaction.
 */

#include "libresd_shell.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

/*============================================================================
 * INTERNAL HELPERS
 *============================================================================*/

/* Default print function */
static void default_print(const char *str) {
    printf("%s", str);
}

/* Print wrapper */
static void shell_print(libresd_shell_t *shell, const char *str) {
    if (shell->print) {
        shell->print(str);
    } else {
        default_print(str);
    }
}

/* Printf-style print */
static void shell_printf(libresd_shell_t *shell, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    shell_print(shell, buf);
}

/* Error print */
static void shell_error(libresd_shell_t *shell, const char *str) {
    if (shell->error) {
        shell->error(str);
    } else {
        shell_print(shell, str);
    }
}

/* Format size to human readable */
static void format_size(uint64_t size, char *buf, size_t bufsize, bool human) {
    if (!human || size < 1024) {
        snprintf(buf, bufsize, "%lu", (unsigned long)size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, bufsize, "%.1fK", size / 1024.0);
    } else if (size < 1024ULL * 1024 * 1024) {
        snprintf(buf, bufsize, "%.1fM", size / (1024.0 * 1024));
    } else {
        snprintf(buf, bufsize, "%.1fG", size / (1024.0 * 1024 * 1024));
    }
}

/* Format date/time */
static void format_datetime(libresd_datetime_t *dt, char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "%04u-%02u-%02u %02u:%02u",
             dt->year, dt->month, dt->day,
             dt->hour, dt->minute);
}

/* Simple glob match */
static bool glob_match(const char *pattern, const char *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return true;
            while (*str) {
                if (glob_match(pattern, str)) return true;
                str++;
            }
            return false;
        }
        if (*pattern == '?' || tolower(*pattern) == tolower(*str)) {
            pattern++;
            str++;
        } else {
            return false;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0' && *str == '\0';
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

void libresd_shell_init(libresd_shell_t *shell, libresd_sd_t *sd, libresd_fat_t *fat) {
    if (!shell) return;
    
    memset(shell, 0, sizeof(libresd_shell_t));
    shell->sd = sd;
    shell->fat = fat;
    shell->print = NULL;
    shell->error = NULL;
    shell->show_hidden = false;
    shell->long_format = false;
    shell->human_readable = true;
}

void libresd_shell_set_output(libresd_shell_t *shell, void (*print)(const char *)) {
    if (shell) {
        shell->print = print;
    }
}

/*============================================================================
 * DIRECTORY COMMANDS
 *============================================================================*/

libresd_err_t libresd_shell_ls(libresd_shell_t *shell, const char *path) {
    libresd_dir_t dir;
    libresd_fileinfo_t info;
    libresd_err_t err;
    char size_buf[16];
    char date_buf[32];
    int count = 0;
    uint64_t total_size = 0;
    
    if (!shell || !shell->fat) return LIBRESD_ERR_INVALID_PARAM;
    
    err = libresd_fat_opendir(shell->fat, &dir, path);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot open directory\n");
        return err;
    }
    
    while (libresd_fat_readdir(shell->fat, &dir, &info) == LIBRESD_OK) {
        /* Skip . and .. */
        if (info.name[0] == '.' && (info.name[1] == '\0' || 
            (info.name[1] == '.' && info.name[2] == '\0'))) {
            continue;
        }
        
        /* Skip hidden files if not showing */
        if (!shell->show_hidden && (info.attr & LIBRESD_ATTR_HIDDEN)) {
            continue;
        }
        
        if (shell->long_format) {
            /* Long format: drwxr-xr-x  size  date  name */
            char attr_str[11] = "----------";
            
            if (info.attr & LIBRESD_ATTR_DIRECTORY) attr_str[0] = 'd';
            if (!(info.attr & LIBRESD_ATTR_READ_ONLY)) {
                attr_str[1] = 'r';
                attr_str[2] = 'w';
            } else {
                attr_str[1] = 'r';
            }
            if (info.attr & LIBRESD_ATTR_HIDDEN) attr_str[3] = 'h';
            if (info.attr & LIBRESD_ATTR_SYSTEM) attr_str[4] = 's';
            if (info.attr & LIBRESD_ATTR_ARCHIVE) attr_str[5] = 'a';
            
            format_size(info.size, size_buf, sizeof(size_buf), shell->human_readable);
            format_datetime(&info.modified, date_buf, sizeof(date_buf));
            
            shell_printf(shell, "%s %8s %s %s\n", attr_str, size_buf, date_buf, info.name);
        } else {
            /* Short format */
            if (info.attr & LIBRESD_ATTR_DIRECTORY) {
                shell_printf(shell, "%s/\n", info.name);
            } else {
                shell_printf(shell, "%s\n", info.name);
            }
        }
        
        count++;
        total_size += info.size;
    }
    
    libresd_fat_closedir(&dir);
    
    if (shell->long_format) {
        format_size(total_size, size_buf, sizeof(size_buf), shell->human_readable);
        shell_printf(shell, "Total: %d items, %s\n", count, size_buf);
    }
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_cd(libresd_shell_t *shell, const char *path) {
    if (!shell || !shell->fat) return LIBRESD_ERR_INVALID_PARAM;
    
    libresd_err_t err = libresd_fat_chdir(shell->fat, path);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot change directory\n");
    }
    return err;
}

libresd_err_t libresd_shell_pwd(libresd_shell_t *shell) {
    char path[LIBRESD_MAX_PATH];
    
    if (!shell || !shell->fat) return LIBRESD_ERR_INVALID_PARAM;
    
    if (libresd_fat_getcwd(shell->fat, path, sizeof(path))) {
        shell_printf(shell, "%s\n", path);
        return LIBRESD_OK;
    }
    
    return LIBRESD_ERR_GENERAL;
}

#if LIBRESD_ENABLE_WRITE && LIBRESD_ENABLE_DIRS

libresd_err_t libresd_shell_mkdir(libresd_shell_t *shell, const char *path) {
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    
    libresd_err_t err = libresd_fat_mkdir(shell->fat, path);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot create directory\n");
    }
    return err;
}

libresd_err_t libresd_shell_rmdir(libresd_shell_t *shell, const char *path) {
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    
    libresd_err_t err = libresd_fat_rmdir(shell->fat, path);
    if (err != LIBRESD_OK) {
        if (err == LIBRESD_ERR_DIR_NOT_EMPTY) {
            shell_error(shell, "Error: Directory not empty\n");
        } else {
            shell_error(shell, "Error: Cannot remove directory\n");
        }
    }
    return err;
}

#endif /* LIBRESD_ENABLE_WRITE && LIBRESD_ENABLE_DIRS */

/*============================================================================
 * FILE COMMANDS
 *============================================================================*/

libresd_err_t libresd_shell_cat(libresd_shell_t *shell, const char *path) {
    libresd_file_t file;
    libresd_err_t err;
    uint8_t buf[128];
    uint32_t bytes_read;
    
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    
    err = libresd_fat_open(shell->fat, &file, path, LIBRESD_READ);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot open file\n");
        return err;
    }
    
    while (libresd_fat_read(shell->fat, &file, buf, sizeof(buf) - 1, &bytes_read) == LIBRESD_OK 
           && bytes_read > 0) {
        buf[bytes_read] = '\0';
        shell_print(shell, (char *)buf);
    }
    
    shell_print(shell, "\n");
    libresd_fat_close(shell->fat, &file);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_head(libresd_shell_t *shell, const char *path, uint32_t bytes) {
    libresd_file_t file;
    libresd_err_t err;
    uint8_t buf[128];
    uint32_t bytes_read;
    uint32_t total = 0;
    
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    if (bytes == 0) bytes = 1024;
    
    err = libresd_fat_open(shell->fat, &file, path, LIBRESD_READ);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot open file\n");
        return err;
    }
    
    while (total < bytes) {
        uint32_t to_read = sizeof(buf) - 1;
        if (to_read > bytes - total) to_read = bytes - total;
        
        err = libresd_fat_read(shell->fat, &file, buf, to_read, &bytes_read);
        if (err != LIBRESD_OK || bytes_read == 0) break;
        
        buf[bytes_read] = '\0';
        shell_print(shell, (char *)buf);
        total += bytes_read;
    }
    
    shell_print(shell, "\n");
    libresd_fat_close(shell->fat, &file);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_hexdump(libresd_shell_t *shell, const char *path,
                                     uint32_t offset, uint32_t length) {
    libresd_file_t file;
    libresd_err_t err;
    uint8_t buf[16];
    uint32_t bytes_read;
    uint32_t addr = offset;
    uint32_t total = 0;
    
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    if (length == 0) length = 256;
    
    err = libresd_fat_open(shell->fat, &file, path, LIBRESD_READ);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot open file\n");
        return err;
    }
    
    if (offset > 0) {
        libresd_fat_seek(shell->fat, &file, offset, LIBRESD_SEEK_SET);
    }
    
    while (total < length) {
        uint32_t to_read = 16;
        if (to_read > length - total) to_read = length - total;
        
        err = libresd_fat_read(shell->fat, &file, buf, to_read, &bytes_read);
        if (err != LIBRESD_OK || bytes_read == 0) break;
        
        /* Print address */
        shell_printf(shell, "%08X  ", addr);
        
        /* Print hex bytes */
        for (uint32_t i = 0; i < 16; i++) {
            if (i < bytes_read) {
                shell_printf(shell, "%02X ", buf[i]);
            } else {
                shell_print(shell, "   ");
            }
            if (i == 7) shell_print(shell, " ");
        }
        
        /* Print ASCII */
        shell_print(shell, " |");
        for (uint32_t i = 0; i < bytes_read; i++) {
            char c = (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.';
            shell_printf(shell, "%c", c);
        }
        shell_print(shell, "|\n");
        
        addr += bytes_read;
        total += bytes_read;
    }
    
    libresd_fat_close(shell->fat, &file);
    return LIBRESD_OK;
}

#if LIBRESD_ENABLE_WRITE

libresd_err_t libresd_shell_touch(libresd_shell_t *shell, const char *path) {
    libresd_file_t file;
    libresd_err_t err;
    
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    
    /* Open with create flag */
    err = libresd_fat_open(shell->fat, &file, path, LIBRESD_READ | LIBRESD_WRITE | LIBRESD_CREATE);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot create file\n");
        return err;
    }
    
    libresd_fat_close(shell->fat, &file);
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_rm(libresd_shell_t *shell, const char *path) {
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    
    libresd_err_t err = libresd_fat_unlink(shell->fat, path);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot remove file\n");
    }
    return err;
}

libresd_err_t libresd_shell_cp(libresd_shell_t *shell, const char *src, const char *dst) {
    libresd_file_t src_file, dst_file;
    libresd_err_t err;
    uint8_t buf[512];
    uint32_t bytes_read, bytes_written;
    
    if (!shell || !shell->fat || !src || !dst) return LIBRESD_ERR_INVALID_PARAM;
    
    /* Open source */
    err = libresd_fat_open(shell->fat, &src_file, src, LIBRESD_READ);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot open source file\n");
        return err;
    }
    
    /* Open/create destination */
    err = libresd_fat_open(shell->fat, &dst_file, dst, 
                           LIBRESD_WRITE | LIBRESD_CREATE | LIBRESD_TRUNCATE);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot create destination file\n");
        libresd_fat_close(shell->fat, &src_file);
        return err;
    }
    
    /* Copy data */
    while (libresd_fat_read(shell->fat, &src_file, buf, sizeof(buf), &bytes_read) == LIBRESD_OK 
           && bytes_read > 0) {
        err = libresd_fat_write(shell->fat, &dst_file, buf, bytes_read, &bytes_written);
        if (err != LIBRESD_OK || bytes_written != bytes_read) {
            shell_error(shell, "Error: Write failed\n");
            break;
        }
    }
    
    libresd_fat_close(shell->fat, &src_file);
    libresd_fat_close(shell->fat, &dst_file);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_mv(libresd_shell_t *shell, const char *src, const char *dst) {
    if (!shell || !shell->fat || !src || !dst) return LIBRESD_ERR_INVALID_PARAM;
    
    libresd_err_t err = libresd_fat_rename(shell->fat, src, dst);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot move/rename file\n");
    }
    return err;
}

libresd_err_t libresd_shell_write(libresd_shell_t *shell, const char *path,
                                   const char *content, bool append) {
    libresd_file_t file;
    libresd_err_t err;
    uint32_t bytes_written;
    uint8_t mode = LIBRESD_WRITE | LIBRESD_CREATE;
    
    if (!shell || !shell->fat || !path || !content) return LIBRESD_ERR_INVALID_PARAM;
    
    if (append) {
        mode |= LIBRESD_APPEND;
    } else {
        mode |= LIBRESD_TRUNCATE;
    }
    
    err = libresd_fat_open(shell->fat, &file, path, mode);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot open file for writing\n");
        return err;
    }
    
    err = libresd_fat_write(shell->fat, &file, content, strlen(content), &bytes_written);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Write failed\n");
    }
    
    libresd_fat_close(shell->fat, &file);
    return err;
}

#endif /* LIBRESD_ENABLE_WRITE */

/*============================================================================
 * INFORMATION COMMANDS
 *============================================================================*/

libresd_err_t libresd_shell_stat(libresd_shell_t *shell, const char *path) {
    libresd_fileinfo_t info;
    libresd_err_t err;
    char size_buf[16];
    char date_buf[32];
    
    if (!shell || !shell->fat || !path) return LIBRESD_ERR_INVALID_PARAM;
    
    err = libresd_fat_stat(shell->fat, path, &info);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot stat path\n");
        return err;
    }
    
    shell_printf(shell, "Name: %s\n", info.name);
    shell_printf(shell, "Type: %s\n", (info.attr & LIBRESD_ATTR_DIRECTORY) ? "Directory" : "File");
    
    format_size(info.size, size_buf, sizeof(size_buf), false);
    shell_printf(shell, "Size: %s bytes\n", size_buf);
    
    shell_print(shell, "Attr: ");
    if (info.attr & LIBRESD_ATTR_READ_ONLY) shell_print(shell, "ReadOnly ");
    if (info.attr & LIBRESD_ATTR_HIDDEN) shell_print(shell, "Hidden ");
    if (info.attr & LIBRESD_ATTR_SYSTEM) shell_print(shell, "System ");
    if (info.attr & LIBRESD_ATTR_ARCHIVE) shell_print(shell, "Archive ");
    shell_print(shell, "\n");
    
    format_datetime(&info.created, date_buf, sizeof(date_buf));
    shell_printf(shell, "Created: %s\n", date_buf);
    
    format_datetime(&info.modified, date_buf, sizeof(date_buf));
    shell_printf(shell, "Modified: %s\n", date_buf);
    
    shell_printf(shell, "Cluster: %lu\n", (unsigned long)info.first_cluster);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_df(libresd_shell_t *shell) {
    libresd_info_t info;
    libresd_err_t err;
    char total_buf[16], free_buf[16], used_buf[16];
    
    if (!shell || !shell->fat) return LIBRESD_ERR_INVALID_PARAM;
    
    err = libresd_fat_get_info(shell->fat, &info);
    if (err != LIBRESD_OK) {
        shell_error(shell, "Error: Cannot get filesystem info\n");
        return err;
    }
    
    /* Calculate free space if not known */
    uint64_t free_bytes = libresd_fat_get_free(shell->fat);
    uint64_t total_bytes = info.total_bytes;
    uint64_t used_bytes = total_bytes - free_bytes;
    
    format_size(total_bytes, total_buf, sizeof(total_buf), shell->human_readable);
    format_size(free_bytes, free_buf, sizeof(free_buf), shell->human_readable);
    format_size(used_bytes, used_buf, sizeof(used_buf), shell->human_readable);
    
    shell_print(shell, "Filesystem    Size    Used    Avail   Use%\n");
    
    uint32_t percent = (total_bytes > 0) ? (used_bytes * 100 / total_bytes) : 0;
    shell_printf(shell, "%-12s  %6s  %6s  %6s  %3lu%%\n",
                 info.volume_label[0] ? info.volume_label : "SDCARD",
                 total_buf, used_buf, free_buf, (unsigned long)percent);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_sdinfo(libresd_shell_t *shell) {
    if (!shell || !shell->sd || !shell->fat) return LIBRESD_ERR_INVALID_PARAM;
    
    libresd_sd_t *sd = shell->sd;
    libresd_fat_t *fat = shell->fat;
    char size_buf[16];
    
    shell_print(shell, "=== SD Card Information ===\n");
    shell_printf(shell, "Card Type: %s\n", libresd_sd_type_str(sd->card_type));
    
    format_size(sd->capacity, size_buf, sizeof(size_buf), shell->human_readable);
    shell_printf(shell, "Capacity: %s\n", size_buf);
    
    shell_printf(shell, "Sectors: %lu\n", (unsigned long)sd->sector_count);
    shell_printf(shell, "Block Addr: %s\n", sd->block_addr ? "Yes" : "No");
    
    shell_print(shell, "\n=== Filesystem Information ===\n");
    
    const char *fs_type = "Unknown";
    switch (fat->fs_type) {
        case LIBRESD_FS_FAT12: fs_type = "FAT12"; break;
        case LIBRESD_FS_FAT16: fs_type = "FAT16"; break;
        case LIBRESD_FS_FAT32: fs_type = "FAT32"; break;
        default: break;
    }
    shell_printf(shell, "Filesystem: %s\n", fs_type);
    shell_printf(shell, "Volume Label: %s\n", fat->volume_label[0] ? fat->volume_label : "(none)");
    shell_printf(shell, "Volume Serial: %08lX\n", (unsigned long)fat->volume_serial);
    shell_printf(shell, "Cluster Size: %lu bytes\n", (unsigned long)fat->cluster_size);
    shell_printf(shell, "Clusters: %lu\n", (unsigned long)fat->cluster_count);
    
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_find(libresd_shell_t *shell, const char *path,
                                  const char *pattern) {
    /* Simple recursive find - limited depth to avoid stack overflow */
    libresd_dir_t dir;
    libresd_fileinfo_t info;
    libresd_err_t err;
    char full_path[LIBRESD_MAX_PATH];
    
    if (!shell || !shell->fat || !pattern) return LIBRESD_ERR_INVALID_PARAM;
    if (!path) path = "/";
    
    err = libresd_fat_opendir(shell->fat, &dir, path);
    if (err != LIBRESD_OK) return err;
    
    while (libresd_fat_readdir(shell->fat, &dir, &info) == LIBRESD_OK) {
        /* Skip . and .. */
        if (info.name[0] == '.' && (info.name[1] == '\0' || 
            (info.name[1] == '.' && info.name[2] == '\0'))) {
            continue;
        }
        
        /* Build full path */
        if (strcmp(path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", info.name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, info.name);
        }
        
        /* Check if name matches pattern */
        if (glob_match(pattern, info.name)) {
            shell_printf(shell, "%s\n", full_path);
        }
        
        /* Recurse into directories */
        if (info.attr & LIBRESD_ATTR_DIRECTORY) {
            libresd_shell_find(shell, full_path, pattern);
        }
    }
    
    libresd_fat_closedir(&dir);
    return LIBRESD_OK;
}

/*============================================================================
 * UTILITY COMMANDS
 *============================================================================*/

static libresd_err_t tree_recursive(libresd_shell_t *shell, const char *path, 
                                     int depth, int max_depth, const char *prefix) {
    libresd_dir_t dir;
    libresd_fileinfo_t info;
    libresd_err_t err;
    char new_prefix[128];
    char full_path[LIBRESD_MAX_PATH];
    int count = 0;
    
    if (max_depth > 0 && depth >= max_depth) return LIBRESD_OK;
    
    err = libresd_fat_opendir(shell->fat, &dir, path);
    if (err != LIBRESD_OK) return err;
    
    /* First pass: count entries */
    int total = 0;
    while (libresd_fat_readdir(shell->fat, &dir, &info) == LIBRESD_OK) {
        if (info.name[0] != '.') total++;
    }
    libresd_fat_closedir(&dir);
    
    /* Second pass: display */
    err = libresd_fat_opendir(shell->fat, &dir, path);
    if (err != LIBRESD_OK) return err;
    
    while (libresd_fat_readdir(shell->fat, &dir, &info) == LIBRESD_OK) {
        if (info.name[0] == '.') continue;
        
        count++;
        bool is_last = (count == total);
        
        shell_printf(shell, "%s%s%s%s\n", 
                     prefix,
                     is_last ? "└── " : "├── ",
                     info.name,
                     (info.attr & LIBRESD_ATTR_DIRECTORY) ? "/" : "");
        
        if (info.attr & LIBRESD_ATTR_DIRECTORY) {
            snprintf(new_prefix, sizeof(new_prefix), "%s%s", 
                     prefix, is_last ? "    " : "│   ");
            
            if (strcmp(path, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", info.name);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", path, info.name);
            }
            
            tree_recursive(shell, full_path, depth + 1, max_depth, new_prefix);
        }
    }
    
    libresd_fat_closedir(&dir);
    return LIBRESD_OK;
}

libresd_err_t libresd_shell_tree(libresd_shell_t *shell, const char *path, int depth) {
    if (!shell || !shell->fat) return LIBRESD_ERR_INVALID_PARAM;
    if (!path) path = ".";
    
    shell_printf(shell, "%s\n", path);
    return tree_recursive(shell, path, 0, depth, "");
}

bool libresd_shell_exists(libresd_shell_t *shell, const char *path) {
    if (!shell || !shell->fat || !path) return false;
    return libresd_fat_exists(shell->fat, path);
}

bool libresd_shell_isdir(libresd_shell_t *shell, const char *path) {
    libresd_fileinfo_t info;
    if (!shell || !shell->fat || !path) return false;
    if (libresd_fat_stat(shell->fat, path, &info) != LIBRESD_OK) return false;
    return (info.attr & LIBRESD_ATTR_DIRECTORY) != 0;
}

bool libresd_shell_isfile(libresd_shell_t *shell, const char *path) {
    libresd_fileinfo_t info;
    if (!shell || !shell->fat || !path) return false;
    if (libresd_fat_stat(shell->fat, path, &info) != LIBRESD_OK) return false;
    return (info.attr & LIBRESD_ATTR_DIRECTORY) == 0;
}

/*============================================================================
 * COMMAND PARSER
 *============================================================================*/

#if LIBRESD_ENABLE_SHELL

/* Simple tokenizer */
static int tokenize(char *str, char **tokens, int max_tokens) {
    int count = 0;
    char *p = str;
    
    while (*p && count < max_tokens) {
        /* Skip whitespace */
        while (*p && isspace(*p)) p++;
        if (!*p) break;
        
        tokens[count++] = p;
        
        /* Find end of token */
        while (*p && !isspace(*p)) p++;
        if (*p) *p++ = '\0';
    }
    
    return count;
}

libresd_err_t libresd_shell_exec(libresd_shell_t *shell, const char *cmdline) {
    char buf[256];
    char *tokens[16];
    int argc;
    
    if (!shell || !cmdline) return LIBRESD_ERR_INVALID_PARAM;
    
    /* Copy command line (we'll modify it) */
    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    /* Tokenize */
    argc = tokenize(buf, tokens, 16);
    if (argc == 0) return LIBRESD_OK;
    
    const char *cmd = tokens[0];
    
    /* ls command */
    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        const char *path = NULL;
        bool old_show_hidden = shell->show_hidden;
        bool old_long_format = shell->long_format;
        
        for (int i = 1; i < argc; i++) {
            if (strcmp(tokens[i], "-a") == 0) {
                shell->show_hidden = true;
            } else if (strcmp(tokens[i], "-l") == 0) {
                shell->long_format = true;
            } else if (strcmp(tokens[i], "-la") == 0 || strcmp(tokens[i], "-al") == 0) {
                shell->show_hidden = true;
                shell->long_format = true;
            } else if (tokens[i][0] != '-') {
                path = tokens[i];
            }
        }
        
        libresd_err_t err = libresd_shell_ls(shell, path);
        
        shell->show_hidden = old_show_hidden;
        shell->long_format = old_long_format;
        return err;
    }
    
    /* cd command */
    if (strcmp(cmd, "cd") == 0) {
        return libresd_shell_cd(shell, argc > 1 ? tokens[1] : NULL);
    }
    
    /* pwd command */
    if (strcmp(cmd, "pwd") == 0) {
        return libresd_shell_pwd(shell);
    }
    
#if LIBRESD_ENABLE_WRITE && LIBRESD_ENABLE_DIRS
    /* mkdir command */
    if (strcmp(cmd, "mkdir") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: mkdir <path>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_mkdir(shell, tokens[1]);
    }
    
    /* rmdir command */
    if (strcmp(cmd, "rmdir") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: rmdir <path>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_rmdir(shell, tokens[1]);
    }
#endif
    
    /* cat command */
    if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "type") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: cat <file>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_cat(shell, tokens[1]);
    }
    
    /* head command */
    if (strcmp(cmd, "head") == 0) {
        uint32_t bytes = 1024;
        const char *file = NULL;
        
        for (int i = 1; i < argc; i++) {
            if (strcmp(tokens[i], "-n") == 0 && i + 1 < argc) {
                bytes = atoi(tokens[++i]);
            } else {
                file = tokens[i];
            }
        }
        
        if (!file) {
            shell_error(shell, "Usage: head [-n bytes] <file>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_head(shell, file, bytes);
    }
    
    /* hexdump command */
    if (strcmp(cmd, "hexdump") == 0 || strcmp(cmd, "hd") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: hexdump <file> [offset] [length]\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        uint32_t offset = argc > 2 ? atoi(tokens[2]) : 0;
        uint32_t length = argc > 3 ? atoi(tokens[3]) : 256;
        return libresd_shell_hexdump(shell, tokens[1], offset, length);
    }
    
#if LIBRESD_ENABLE_WRITE
    /* touch command */
    if (strcmp(cmd, "touch") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: touch <file>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_touch(shell, tokens[1]);
    }
    
    /* rm command */
    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "del") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: rm <file>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_rm(shell, tokens[1]);
    }
    
    /* cp command */
    if (strcmp(cmd, "cp") == 0 || strcmp(cmd, "copy") == 0) {
        if (argc < 3) {
            shell_error(shell, "Usage: cp <src> <dst>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_cp(shell, tokens[1], tokens[2]);
    }
    
    /* mv command */
    if (strcmp(cmd, "mv") == 0 || strcmp(cmd, "move") == 0 || strcmp(cmd, "ren") == 0) {
        if (argc < 3) {
            shell_error(shell, "Usage: mv <src> <dst>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_mv(shell, tokens[1], tokens[2]);
    }
#endif
    
    /* stat command */
    if (strcmp(cmd, "stat") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: stat <path>\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_stat(shell, tokens[1]);
    }
    
    /* df command */
    if (strcmp(cmd, "df") == 0) {
        return libresd_shell_df(shell);
    }
    
    /* tree command */
    if (strcmp(cmd, "tree") == 0) {
        return libresd_shell_tree(shell, argc > 1 ? tokens[1] : ".", 0);
    }
    
    /* find command */
    if (strcmp(cmd, "find") == 0) {
        if (argc < 2) {
            shell_error(shell, "Usage: find <pattern> [path]\n");
            return LIBRESD_ERR_INVALID_PARAM;
        }
        return libresd_shell_find(shell, argc > 2 ? tokens[2] : "/", tokens[1]);
    }
    
    /* sdinfo command */
    if (strcmp(cmd, "sdinfo") == 0 || strcmp(cmd, "info") == 0) {
        return libresd_shell_sdinfo(shell);
    }
    
    /* help command */
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        libresd_shell_help(shell);
        return LIBRESD_OK;
    }
    
    shell_error(shell, "Unknown command. Type 'help' for available commands.\n");
    return LIBRESD_ERR_NOT_SUPPORTED;
}

void libresd_shell_help(libresd_shell_t *shell) {
    shell_print(shell, "LibreSD Shell Commands:\n");
    shell_print(shell, "  ls [-l] [-a] [path]  - List directory\n");
    shell_print(shell, "  cd [path]            - Change directory\n");
    shell_print(shell, "  pwd                  - Print working directory\n");
    shell_print(shell, "  cat <file>           - Display file contents\n");
    shell_print(shell, "  head [-n N] <file>   - Display first N bytes\n");
    shell_print(shell, "  hexdump <file>       - Hex dump of file\n");
#if LIBRESD_ENABLE_WRITE
    shell_print(shell, "  touch <file>         - Create empty file\n");
    shell_print(shell, "  rm <file>            - Remove file\n");
    shell_print(shell, "  cp <src> <dst>       - Copy file\n");
    shell_print(shell, "  mv <src> <dst>       - Move/rename file\n");
#if LIBRESD_ENABLE_DIRS
    shell_print(shell, "  mkdir <path>         - Create directory\n");
    shell_print(shell, "  rmdir <path>         - Remove empty directory\n");
#endif
#endif
    shell_print(shell, "  stat <path>          - File/dir info\n");
    shell_print(shell, "  df                   - Disk free space\n");
    shell_print(shell, "  tree [path]          - Directory tree\n");
    shell_print(shell, "  find <pattern>       - Find files\n");
    shell_print(shell, "  sdinfo               - SD card info\n");
    shell_print(shell, "  help                 - This help\n");
}

#endif /* LIBRESD_ENABLE_SHELL */
