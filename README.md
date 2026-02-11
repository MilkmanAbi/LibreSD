# LibreSD

**High-Performance, Cross-Platform SD Card Library for Microcontrollers**

LibreSD is a fast, minimal-footprint SD card driver and FAT filesystem library written in pure C. Designed to work on any MCU from 8-bit AVRs to 32-bit ARM Cortex.

## Features

- **Fast** - Optimized for speed with DMA-friendly bulk transfers
- **Minimal footprint** - ~15-20KB flash for core functionality
- **Pure C** - Works with any compiler, any MCU
- **Full SD support** - SD v1, v2, SDHC, SDXC
- **FAT filesystem** - FAT12, FAT16, FAT32 with LFN support
- **Shell commands** - ls, cd, cat, mkdir, rm, cp, mv, and more
- **HAL abstraction** - Easy porting to any platform
- **Speed ramping** - 400kHz init → configurable fast speed (default 4MHz)

## Quick Start

### 1. Implement HAL for Your MCU

Create `libresd_hal_myplatform.c` with these functions:

```c
#include "libresd_hal.h"

void libresd_hal_spi_init(uint32_t speed_hz) {
    // Initialize SPI at specified speed
}

uint8_t libresd_hal_spi_transfer(uint8_t tx) {
    // Transfer single byte, return received byte
}

void libresd_hal_spi_transfer_bulk(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    // Bulk transfer (use DMA for best performance)
}

void libresd_hal_cs_low(void) {
    // Assert chip select (active low)
}

void libresd_hal_cs_high(void) {
    // Deassert chip select
}

void libresd_hal_delay_ms(uint32_t ms) {
    // Blocking delay
}

uint32_t libresd_hal_get_ms(void) {
    // Return millisecond counter
}
```

### 2. Use LibreSD

```c
#include "libresd.h"

int main(void) {
    libresd_sd_t sd;
    libresd_fat_t fat;
    
    // Initialize SD card (starts at 400kHz)
    if (libresd_sd_init(&sd) != LIBRESD_OK) {
        printf("SD init failed!\n");
        return 1;
    }
    
    // Ramp up to fast speed
    libresd_sd_set_speed(&sd, 4000000);  // 4 MHz
    
    // Mount filesystem
    if (libresd_fat_mount(&fat, &sd) != LIBRESD_OK) {
        printf("Mount failed!\n");
        return 1;
    }
    
    // Read a file
    libresd_file_t file;
    char buf[256];
    uint32_t bytes_read;
    
    if (libresd_fat_open(&fat, &file, "/hello.txt", LIBRESD_READ) == LIBRESD_OK) {
        libresd_fat_read(&fat, &file, buf, sizeof(buf)-1, &bytes_read);
        buf[bytes_read] = '\0';
        printf("Contents: %s\n", buf);
        libresd_fat_close(&fat, &file);
    }
    
    // Write a file
    if (libresd_fat_open(&fat, &file, "/output.txt", 
                         LIBRESD_WRITE | LIBRESD_CREATE) == LIBRESD_OK) {
        const char *data = "Hello from LibreSD!";
        libresd_fat_write(&fat, &file, data, strlen(data), NULL);
        libresd_fat_close(&fat, &file);
    }
    
    // Unmount
    libresd_fat_unmount(&fat);
    
    return 0;
}
```

### 3. Use Shell Commands

```c
libresd_shell_t shell;
libresd_shell_init(&shell, &sd, &fat);

libresd_shell_exec(&shell, "ls -la");
libresd_shell_exec(&shell, "cd /logs");
libresd_shell_exec(&shell, "cat data.txt");
libresd_shell_exec(&shell, "mkdir /backup");
libresd_shell_exec(&shell, "cp file.txt /backup/");
```

## File Structure

```
LibreSD/
├── include/
│   ├── libresd.h           # Main header (include this)
│   ├── libresd_config.h    # Configuration options
│   ├── libresd_types.h     # Types and error codes
│   ├── libresd_hal.h       # HAL interface to implement
│   ├── libresd_sd.h        # SD card protocol
│   ├── libresd_fat.h       # FAT filesystem
│   └── libresd_shell.h     # Shell commands
├── src/
│   ├── libresd_sd.c        # SD card implementation
│   ├── libresd_fat.c       # FAT implementation
│   ├── libresd_file.c      # File operations
│   └── libresd_shell.c     # Shell implementation
└── examples/
    └── rp2040/             # RP2040 example with HAL
```

## Configuration

Edit `libresd_config.h` or define before including:

```c
// Feature flags
#define LIBRESD_ENABLE_WRITE    1    // Enable write support
#define LIBRESD_ENABLE_LFN      1    // Long filename support
#define LIBRESD_ENABLE_DIRS     1    // Directory operations
#define LIBRESD_ENABLE_SHELL    1    // Shell commands

// SPI speeds
#define LIBRESD_SPI_INIT_SPEED  400000     // 400 kHz init
#define LIBRESD_SPI_FAST_SPEED  4000000    // 4 MHz default
#define LIBRESD_SPI_MAX_SPEED   25000000   // 25 MHz max

// Buffer sizes
#define LIBRESD_SECTOR_SIZE     512
#define LIBRESD_MAX_PATH        256
#define LIBRESD_MAX_FILENAME    256
```

## Supported Operations

### File Operations
- `libresd_fat_open()` - Open file (READ, WRITE, CREATE, APPEND, TRUNCATE)
- `libresd_fat_close()` - Close file
- `libresd_fat_read()` - Read data
- `libresd_fat_write()` - Write data
- `libresd_fat_seek()` - Seek to position
- `libresd_fat_tell()` - Get position
- `libresd_fat_size()` - Get file size
- `libresd_fat_unlink()` - Delete file
- `libresd_fat_rename()` - Rename file

### Directory Operations
- `libresd_fat_opendir()` - Open directory
- `libresd_fat_readdir()` - Read entry
- `libresd_fat_closedir()` - Close directory
- `libresd_fat_chdir()` - Change directory
- `libresd_fat_getcwd()` - Get current directory
- `libresd_fat_mkdir()` - Create directory
- `libresd_fat_rmdir()` - Remove directory

### Shell Commands
- `ls [-l] [-a] [path]` - List directory
- `cd [path]` - Change directory  
- `pwd` - Print working directory
- `cat <file>` - Display file
- `head [-n N] <file>` - Display first N bytes
- `hexdump <file>` - Hex dump
- `touch <file>` - Create file
- `rm <file>` - Remove file
- `cp <src> <dst>` - Copy file
- `mv <src> <dst>` - Move/rename
- `mkdir <path>` - Create directory
- `rmdir <path>` - Remove directory
- `stat <path>` - File info
- `df` - Disk free space
- `tree [path]` - Directory tree
- `find <pattern>` - Find files
- `sdinfo` - SD card info

## Wiring (SPI Mode)

| SD Card Pin | Signal | Description |
|-------------|--------|-------------|
| 1 | CS | Chip Select (active low) |
| 2 | MOSI | Data In |
| 3 | GND | Ground |
| 4 | VCC | 3.3V Power |
| 5 | SCK | Clock |
| 6 | GND | Ground |
| 7 | MISO | Data Out |

**Note:** Most microSD breakout boards include level shifting. If wiring directly, ensure 3.3V logic levels.

## Performance Tips

1. **Use bulk transfers** - Implement `libresd_hal_spi_transfer_bulk()` with DMA
2. **Increase SPI speed** - After init, call `libresd_sd_set_speed()` for faster ops
3. **Buffer reads/writes** - Minimize individual read/write calls
4. **Use sector-aligned operations** when possible

## Memory Usage

| Configuration | Flash | RAM |
|--------------|-------|-----|
| Core only (read) | ~12 KB | ~600 bytes + buffers |
| With write | ~18 KB | ~600 bytes + buffers |
| Full (with shell) | ~25 KB | ~800 bytes + buffers |

Per-file overhead: ~560 bytes (512-byte buffer + handle)

## License

MIT License - Free for commercial and personal use.

## Contributing

Contributions welcome! Please submit issues and pull requests.

## Acknowledgments

- SD Card Physical Layer Simplified Specification
- Microsoft FAT32 File System Specification
- ChaN's FatFs (inspiration for API design)

## Why? 
- Because god is speaking to me, I'm going insane with using SD, and I need to make, the craving is all consuming.
