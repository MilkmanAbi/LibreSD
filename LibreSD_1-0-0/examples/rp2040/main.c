/**
 * @file main.c
 * @brief LibreSD Example for RP2040 (Raspberry Pi Pico)
 * 
 * Demonstrates basic SD card operations using LibreSD.
 * 
 * Wiring:
 *   - GPIO 16: MISO (SD card DO)
 *   - GPIO 17: CS   (SD card CS)
 *   - GPIO 18: SCK  (SD card CLK)
 *   - GPIO 19: MOSI (SD card DI)
 *   - 3.3V: VCC
 *   - GND: GND
 * 
 * Build with CMake (see CMakeLists.txt)
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

/* Include LibreSD */
#include "libresd.h"

/* HAL initialization (defined in libresd_hal_rp2040.c) */
extern void libresd_hal_rp2040_init(void);
extern void libresd_hal_rp2040_set_pins(uint miso, uint cs, uint sck, uint mosi);

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Uncomment to change default pins */
// #define SD_PIN_MISO 16
// #define SD_PIN_CS   17
// #define SD_PIN_SCK  18
// #define SD_PIN_MOSI 19

/* Fast SPI speed (after init) - default 4MHz, max ~25MHz for most SD cards */
#define SD_FAST_SPEED_HZ    (4 * 1000 * 1000)

/*============================================================================
 * DEMO FUNCTIONS
 *============================================================================*/

static libresd_sd_t sd;
static libresd_fat_t fat;
static libresd_shell_t shell;

/**
 * @brief Initialize SD card and filesystem
 */
static bool init_sd(void) {
    printf("\n=== LibreSD Demo for RP2040 ===\n");
    printf("Version: %s\n\n", libresd_version());
    
    /* Initialize HAL */
    printf("Initializing SPI...\n");
    libresd_hal_rp2040_init();
    
    /* Initialize SD card */
    printf("Initializing SD card...\n");
    libresd_err_t err = libresd_sd_init(&sd, SD_FAST_SPEED_HZ);
    if (err != LIBRESD_OK) {
        printf("SD init failed: %s\n", libresd_error_str(err));
        return false;
    }
    
    printf("Card type: %s\n", libresd_sd_type_str(sd.card_type));
    printf("Capacity: %llu MB\n", sd.capacity / (1024 * 1024));
    
    /* Mount filesystem */
    printf("Mounting filesystem...\n");
    err = libresd_fat_mount(&fat, &sd);
    if (err != LIBRESD_OK) {
        printf("Mount failed: %s\n", libresd_error_str(err));
        return false;
    }
    
    printf("Filesystem: ");
    switch (fat.fs_type) {
        case LIBRESD_FS_FAT12: printf("FAT12\n"); break;
        case LIBRESD_FS_FAT16: printf("FAT16\n"); break;
        case LIBRESD_FS_FAT32: printf("FAT32\n"); break;
        default: printf("Unknown\n"); break;
    }
    
    if (fat.volume_label[0]) {
        printf("Volume: %s\n", fat.volume_label);
    }
    
    printf("\n");
    return true;
}

/**
 * @brief Demo: List root directory
 */
static void demo_list_directory(void) {
    printf("=== Directory Listing ===\n");
    
    libresd_dir_t dir;
    libresd_fileinfo_t info;
    
    if (libresd_fat_opendir(&fat, &dir, "/") != LIBRESD_OK) {
        printf("Cannot open root directory\n");
        return;
    }
    
    printf("%-20s %10s  Type\n", "Name", "Size");
    printf("--------------------------------------------\n");
    
    while (libresd_fat_readdir(&fat, &dir, &info) == LIBRESD_OK) {
        if (info.name[0] == '.') continue;  /* Skip . and .. */
        
        printf("%-20s %10lu  %s\n", 
               info.name, 
               (unsigned long)info.size,
               (info.attr & LIBRESD_ATTR_DIRECTORY) ? "<DIR>" : "");
    }
    
    libresd_fat_closedir(&dir);
    printf("\n");
}

/**
 * @brief Demo: Read a text file
 */
static void demo_read_file(const char *path) {
    printf("=== Reading File: %s ===\n", path);
    
    libresd_file_t file;
    
    if (libresd_fat_open(&fat, &file, path, LIBRESD_READ) != LIBRESD_OK) {
        printf("Cannot open file\n\n");
        return;
    }
    
    char buffer[256];
    uint32_t bytes_read;
    
    while (libresd_fat_read(&fat, &file, buffer, sizeof(buffer)-1, &bytes_read) == LIBRESD_OK 
           && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    
    libresd_fat_close(&fat, &file);
    printf("\n\n");
}

#if LIBRESD_ENABLE_WRITE
/**
 * @brief Demo: Create and write a file
 */
static void demo_write_file(void) {
    printf("=== Creating File ===\n");
    
    const char *filename = "/test.txt";
    const char *content = "Hello from LibreSD on RP2040!\n"
                          "This file was created by the demo.\n"
                          "LibreSD is fast and efficient!\n";
    
    libresd_file_t file;
    
    if (libresd_fat_open(&fat, &file, filename, 
                         LIBRESD_WRITE | LIBRESD_CREATE | LIBRESD_TRUNCATE) != LIBRESD_OK) {
        printf("Cannot create file\n\n");
        return;
    }
    
    uint32_t bytes_written;
    libresd_fat_write(&fat, &file, content, strlen(content), &bytes_written);
    libresd_fat_close(&fat, &file);
    
    printf("Created %s (%lu bytes)\n\n", filename, (unsigned long)bytes_written);
    
    /* Read it back to verify */
    demo_read_file(filename);
}
#endif

/**
 * @brief Demo: Filesystem info
 */
static void demo_fs_info(void) {
    printf("=== Filesystem Info ===\n");
    
    libresd_info_t info;
    libresd_fat_get_info(&fat, &info);
    
    printf("Total: %llu MB\n", info.total_bytes / (1024 * 1024));
    
    uint64_t free_bytes = libresd_fat_get_free(&fat);
    printf("Free:  %llu MB\n", free_bytes / (1024 * 1024));
    printf("Used:  %llu MB\n", (info.total_bytes - free_bytes) / (1024 * 1024));
    
    printf("Cluster size: %lu bytes\n", (unsigned long)info.cluster_size);
    printf("Total clusters: %lu\n", (unsigned long)info.total_clusters);
    printf("\n");
}

/**
 * @brief Demo: Shell interface
 */
static void demo_shell(void) {
    printf("=== Shell Demo ===\n\n");
    
    /* Initialize shell */
    libresd_shell_init(&shell, &sd, &fat);
    shell.human_readable = true;
    
    /* Execute some commands */
    printf("> pwd\n");
    libresd_shell_exec(&shell, "pwd");
    printf("\n");
    
    printf("> ls -l\n");
    libresd_shell_exec(&shell, "ls -l");
    printf("\n");
    
    printf("> df\n");
    libresd_shell_exec(&shell, "df");
    printf("\n");
    
    printf("> sdinfo\n");
    libresd_shell_exec(&shell, "sdinfo");
    printf("\n");
}

/**
 * @brief Interactive shell mode
 */
static void interactive_shell(void) {
    printf("=== Interactive Shell ===\n");
    printf("Type 'help' for commands, 'exit' to quit\n\n");
    
    char line[256];
    
    while (1) {
        /* Print prompt with current directory */
        char cwd[64];
        libresd_fat_getcwd(&fat, cwd, sizeof(cwd));
        printf("sd:%s> ", cwd);
        
        /* Read line (blocking) */
        int i = 0;
        while (i < sizeof(line) - 1) {
            int c = getchar();
            if (c == '\n' || c == '\r') {
                putchar('\n');
                break;
            }
            if (c == '\b' || c == 127) {  /* Backspace */
                if (i > 0) {
                    i--;
                    printf("\b \b");
                }
                continue;
            }
            if (c >= 32 && c < 127) {
                line[i++] = c;
                putchar(c);
            }
        }
        line[i] = '\0';
        
        /* Check for exit */
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }
        
        /* Execute command */
        if (line[0]) {
            libresd_shell_exec(&shell, line);
        }
    }
    
    printf("Goodbye!\n");
}

/*============================================================================
 * MAIN
 *============================================================================*/

int main(void) {
    /* Initialize stdio (for USB serial or UART) */
    stdio_init_all();
    
    /* Wait for USB serial connection (optional) */
    sleep_ms(2000);
    
    /* Optionally configure custom pins here:
     * libresd_hal_rp2040_set_pins(MISO, CS, SCK, MOSI);
     */
    
    /* Initialize SD card */
    if (!init_sd()) {
        printf("\nSD card initialization failed!\n");
        printf("Check wiring and card insertion.\n");
        while (1) {
            tight_loop_contents();
        }
    }
    
    /* Run demos */
    demo_fs_info();
    demo_list_directory();
    
#if LIBRESD_ENABLE_WRITE
    demo_write_file();
#endif
    
    demo_shell();
    
    /* Enter interactive shell */
    interactive_shell();
    
    /* Cleanup */
    libresd_fat_unmount(&fat);
    libresd_sd_deinit(&sd);
    
    return 0;
}
