// *****************************************************************************
// OS_File_System.h - Write-Once File System Header
// Runs on LM4F120/TM4C123
// Simple file system stored in flash memory with FAT-like structure
// 
// *****************************************************************************

#ifndef __OS_FILE_SYSTEM_H__
#define __OS_FILE_SYSTEM_H__

#include <stdint.h>

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================

// Disk geometry
#define DISK_START_ADDRESS      0x00020000U     // First address in flash (128 KB mark)
#define DISK_END_ADDRESS        0x00040000U     // Last address (256 KB mark)
#define SECTOR_SIZE             512U            // Bytes per sector
#define NUM_SECTORS             256U            // Total sectors (0-255)
#define DIRECTORY_SIZE          256U            // Directory entries
#define FAT_SIZE                256U            // FAT entries

// Special values
#define SECTOR_FREE             0xFFU           // Indicates free/unused sector
#define FILE_EMPTY              0xFFU           // Indicates empty file
#define MAX_FILE_NUMBER         254U            // Maximum file number (0-254)
#define METADATA_SECTOR         255U            // Sector 255 stores directory + FAT

// Error codes
#define FS_SUCCESS              0x00U           // Operation successful
#define FS_ERROR                0xFFU           // Operation failed
#define FS_DISK_FULL            0xFFU           // No free sectors available
#define FS_NO_DATA              0xFFU           // No data to read
#define FS_FILE_NOT_FOUND       0xFFU           // File does not exist

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

typedef struct {
    uint8_t totalFiles;         // Number of files in directory
    uint8_t freeSectors;        // Number of free sectors
    uint8_t usedSectors;        // Number of used sectors
} FS_Status_t;

// =============================================================================
// CORE FILE SYSTEM FUNCTIONS
// =============================================================================

void OS_FS_Init(void);

uint8_t OS_File_New(void);

uint8_t OS_File_Size(uint8_t num);

uint8_t OS_File_Append(uint8_t num, uint8_t buf[512]);

uint8_t OS_File_Read(uint8_t num, uint8_t location, uint8_t buf[512]);

uint8_t OS_File_Flush(void);

uint8_t OS_File_Format(void);

uint8_t OS_File_Mount(void);

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

uint8_t find_free_sector(void);

uint8_t last_sector(uint8_t start);

void append_fat(uint8_t num, uint8_t n);

// =============================================================================
// LOW-LEVEL DISK FUNCTIONS
// =============================================================================

uint8_t eDisk_WriteSector(uint8_t buf[512], uint8_t n);

uint8_t eDisk_ReadSector(uint8_t buf[512], uint8_t n);

// =============================================================================
// FLASH PROGRAMMING FUNCTIONS (from FlashProgram.h)
// =============================================================================

int Flash_Write(uint32_t addr, uint32_t data);

int Flash_Erase(uint32_t addr);

void Flash_Init(uint8_t systemClockFreqMHz);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

void OS_FS_GetStatus(FS_Status_t *status);

uint8_t OS_File_Exists(uint8_t num);

uint8_t OS_FS_FreeSectors(void);

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
extern uint8_t RAM_Directory[DIRECTORY_SIZE];   // Directory in RAM
extern uint8_t RAM_FAT[FAT_SIZE];               // FAT in RAM

#endif // __OS_FILE_SYSTEM_H__