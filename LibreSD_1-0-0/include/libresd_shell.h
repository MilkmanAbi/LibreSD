/**
 * @file libresd_shell.h
 * @brief LibreSD Shell Commands Interface
 * 
 * High-level shell-like commands for easy filesystem interaction.
 * Perfect for use in embedded shells, debug consoles, and CLI interfaces.
 */

#ifndef LIBRESD_SHELL_H
#define LIBRESD_SHELL_H

#include "libresd_fat.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * SHELL CONTEXT
 *============================================================================*/

/**
 * @brief Shell context structure
 * 
 * Maintains state for shell operations.
 */
typedef struct {
    libresd_sd_t    *sd;        /**< SD card reference */
    libresd_fat_t   *fat;       /**< FAT filesystem reference */
    
    /* Output callback - set to NULL to use printf */
    void (*print)(const char *str);
    
    /* Error callback - set to NULL to use print */
    void (*error)(const char *str);
    
    /* Options */
    bool show_hidden;           /**< Show hidden files in ls */
    bool long_format;           /**< Use long format in ls */
    bool human_readable;        /**< Human readable sizes */
} libresd_shell_t;

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * @brief Initialize shell context
 * 
 * @param shell Shell context to initialize
 * @param sd SD card (must be initialized)
 * @param fat FAT filesystem (must be mounted)
 */
void libresd_shell_init(libresd_shell_t *shell, libresd_sd_t *sd, libresd_fat_t *fat);

/**
 * @brief Set output callback
 * 
 * @param shell Shell context
 * @param print Print function (NULL = use default printf)
 */
void libresd_shell_set_output(libresd_shell_t *shell, void (*print)(const char *));

/*============================================================================
 * DIRECTORY COMMANDS
 *============================================================================*/

/**
 * @brief List directory contents (ls)
 * 
 * Options are controlled via shell->show_hidden and shell->long_format
 * 
 * @param shell Shell context
 * @param path Directory path (NULL = current directory)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_ls(libresd_shell_t *shell, const char *path);

/**
 * @brief Change directory (cd)
 * 
 * @param shell Shell context
 * @param path Target directory (NULL = root)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_cd(libresd_shell_t *shell, const char *path);

/**
 * @brief Print working directory (pwd)
 * 
 * @param shell Shell context
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_pwd(libresd_shell_t *shell);

#if LIBRESD_ENABLE_WRITE && LIBRESD_ENABLE_DIRS

/**
 * @brief Create directory (mkdir)
 * 
 * @param shell Shell context
 * @param path Directory to create
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_mkdir(libresd_shell_t *shell, const char *path);

/**
 * @brief Remove empty directory (rmdir)
 * 
 * @param shell Shell context
 * @param path Directory to remove
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_rmdir(libresd_shell_t *shell, const char *path);

#endif /* LIBRESD_ENABLE_WRITE && LIBRESD_ENABLE_DIRS */

/*============================================================================
 * FILE COMMANDS
 *============================================================================*/

/**
 * @brief Display file contents (cat)
 * 
 * @param shell Shell context
 * @param path File to display
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_cat(libresd_shell_t *shell, const char *path);

/**
 * @brief Display first N bytes of file (head)
 * 
 * @param shell Shell context
 * @param path File to display
 * @param bytes Number of bytes (default 1024 if 0)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_head(libresd_shell_t *shell, const char *path, uint32_t bytes);

/**
 * @brief Display hex dump of file (hexdump)
 * 
 * @param shell Shell context
 * @param path File to dump
 * @param offset Starting offset
 * @param length Bytes to dump (0 = all)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_hexdump(libresd_shell_t *shell, const char *path,
                                     uint32_t offset, uint32_t length);

#if LIBRESD_ENABLE_WRITE

/**
 * @brief Create empty file or update timestamp (touch)
 * 
 * @param shell Shell context
 * @param path File to create/touch
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_touch(libresd_shell_t *shell, const char *path);

/**
 * @brief Remove file (rm)
 * 
 * @param shell Shell context
 * @param path File to remove
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_rm(libresd_shell_t *shell, const char *path);

/**
 * @brief Copy file (cp)
 * 
 * @param shell Shell context
 * @param src Source file
 * @param dst Destination file
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_cp(libresd_shell_t *shell, const char *src, const char *dst);

/**
 * @brief Move/rename file (mv)
 * 
 * @param shell Shell context
 * @param src Source path
 * @param dst Destination path
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_mv(libresd_shell_t *shell, const char *src, const char *dst);

/**
 * @brief Write string to file (echo > file)
 * 
 * @param shell Shell context
 * @param path File to write
 * @param content String to write
 * @param append true to append, false to overwrite
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_write(libresd_shell_t *shell, const char *path,
                                   const char *content, bool append);

#endif /* LIBRESD_ENABLE_WRITE */

/*============================================================================
 * INFORMATION COMMANDS
 *============================================================================*/

/**
 * @brief Display file/directory info (stat)
 * 
 * @param shell Shell context
 * @param path Path to examine
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_stat(libresd_shell_t *shell, const char *path);

/**
 * @brief Display filesystem info (df)
 * 
 * Shows total, used, and free space.
 * 
 * @param shell Shell context
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_df(libresd_shell_t *shell);

/**
 * @brief Display SD card info (sdinfo)
 * 
 * @param shell Shell context
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_sdinfo(libresd_shell_t *shell);

/**
 * @brief Find files matching pattern (find)
 * 
 * Simple glob pattern: * matches anything
 * 
 * @param shell Shell context
 * @param path Starting directory
 * @param pattern Pattern to match
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_find(libresd_shell_t *shell, const char *path,
                                  const char *pattern);

/*============================================================================
 * UTILITY COMMANDS
 *============================================================================*/

/**
 * @brief Print directory tree
 * 
 * @param shell Shell context
 * @param path Starting directory
 * @param depth Max depth (0 = unlimited)
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_tree(libresd_shell_t *shell, const char *path, int depth);

/**
 * @brief Check if path exists
 * 
 * @param shell Shell context
 * @param path Path to check
 * @return true if exists
 */
bool libresd_shell_exists(libresd_shell_t *shell, const char *path);

/**
 * @brief Check if path is a directory
 * 
 * @param shell Shell context
 * @param path Path to check
 * @return true if directory
 */
bool libresd_shell_isdir(libresd_shell_t *shell, const char *path);

/**
 * @brief Check if path is a file
 * 
 * @param shell Shell context
 * @param path Path to check
 * @return true if file
 */
bool libresd_shell_isfile(libresd_shell_t *shell, const char *path);

/*============================================================================
 * COMMAND PARSER
 *============================================================================*/

#if LIBRESD_ENABLE_SHELL

/**
 * @brief Execute shell command string
 * 
 * Parses and executes a shell command like "ls -la /folder"
 * 
 * Supported commands:
 *   ls [-l] [-a] [path]     - List directory
 *   cd [path]               - Change directory
 *   pwd                     - Print working directory
 *   mkdir <path>            - Create directory
 *   rmdir <path>            - Remove directory
 *   cat <file>              - Display file
 *   head [-n N] <file>      - Display first N bytes
 *   touch <file>            - Create file
 *   rm <file>               - Remove file
 *   cp <src> <dst>          - Copy file
 *   mv <src> <dst>          - Move/rename
 *   stat <path>             - File info
 *   df                      - Disk free space
 *   tree [path]             - Directory tree
 *   sdinfo                  - SD card info
 *   help                    - Show help
 * 
 * @param shell Shell context
 * @param cmdline Command line to execute
 * @return LIBRESD_OK or error
 */
libresd_err_t libresd_shell_exec(libresd_shell_t *shell, const char *cmdline);

/**
 * @brief Print shell help
 * 
 * @param shell Shell context
 */
void libresd_shell_help(libresd_shell_t *shell);

#endif /* LIBRESD_ENABLE_SHELL */

#ifdef __cplusplus
}
#endif

#endif /* LIBRESD_SHELL_H */
