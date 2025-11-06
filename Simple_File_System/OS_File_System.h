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

/**
 * @brief File system status structure
 */
typedef struct {
    uint8_t totalFiles;         // Number of files in directory
    uint8_t freeSectors;        // Number of free sectors
    uint8_t usedSectors;        // Number of used sectors
} FS_Status_t;

// =============================================================================
// CORE FILE SYSTEM FUNCTIONS
// =============================================================================

/**
 * @brief Initialize file system structures in RAM
 * @details Clears directory and FAT, marking all entries as free (0xFF)
 * @note Must be called before any file operations
 */
void OS_FS_Init(void);

/**
 * @brief Create a new file and return its file number
 * @return File number (0-254) on success, 0xFF on failure or disk full
 * @note Does not allocate sectors until first append
 */
uint8_t OS_File_New(void);

/**
 * @brief Get the size of a file in sectors
 * @param num File number (0-254)
 * @return Number of sectors in file, 0 if empty
 */
uint8_t OS_File_Size(uint8_t num);

/**
 * @brief Append 512 bytes to a file
 * @param num File number (0-254)
 * @param buf Pointer to 512 bytes of data to write
 * @return 0 on success, 0xFF on failure or disk full
 * @note Allocates a new sector and updates FAT
 */
uint8_t OS_File_Append(uint8_t num, uint8_t buf[512]);

/**
 * @brief Read 512 bytes from a file at specified location
 * @param num File number (0-254)
 * @param location Sector index within file (0 = first sector)
 * @param buf Pointer to 512-byte buffer to receive data
 * @return 0 on success, 0xFF if no data or location out of range
 */
uint8_t OS_File_Read(uint8_t num, uint8_t location, uint8_t buf[512]);

/**
 * @brief Write directory and FAT to flash (sector 255)
 * @return 0 on success, 0xFF on write failure
 * @note Call this before power loss to persist file system state
 */
uint8_t OS_File_Flush(void);

/**
 * @brief Erase all files and format the disk
 * @return 0 on success, 0xFF on failure
 * @note Erases all sectors from 0x20000 to 0x40000
 */
uint8_t OS_File_Format(void);

/**
 * @brief Load directory and FAT from flash (sector 255)
 * @return 0 on success, 0xFF on failure
 * @note Call after power-up to restore file system state
 */
uint8_t OS_File_Mount(void);

// =============================================================================
// HELPER FUNCTIONS (Internal Use)
// =============================================================================

/**
 * @brief Find the first free (unused) sector
 * @return Sector number (0-254) or 0xFF if disk full
 * @note Scans directory and FAT to find unused sector
 */
uint8_t find_free_sector(void);

/**
 * @brief Find the last sector in a file's chain
 * @param start First sector of file (from directory)
 * @return Last sector number or 0xFF if file empty
 */
uint8_t last_sector(uint8_t start);

/**
 * @brief Append a sector to a file's FAT chain
 * @param num File number (0-254)
 * @param n Sector number to append
 * @note Updates directory or FAT to link new sector
 */
void append_fat(uint8_t num, uint8_t n);

// =============================================================================
// LOW-LEVEL DISK FUNCTIONS
// =============================================================================

/**
 * @brief Write 512 bytes to a flash sector
 * @param buf Pointer to 512 bytes of data
 * @param n Logical sector number (0-255)
 * @return 0 on success, 1 on write failure
 * @note Uses Flash_Write() to program flash memory
 */
uint8_t eDisk_WriteSector(uint8_t buf[512], uint8_t n);

/**
 * @brief Read 512 bytes from a flash sector
 * @param buf Pointer to 512-byte buffer
 * @param n Logical sector number (0-255)
 * @return 0 on success, 1 on read failure
 */
uint8_t eDisk_ReadSector(uint8_t buf[512], uint8_t n);

// =============================================================================
// FLASH PROGRAMMING FUNCTIONS (from FlashProgram.h)
// =============================================================================

/**
 * @brief Write a 32-bit word to flash
 * @param addr Flash address (must be word-aligned)
 * @param data 32-bit data to write
 * @return 0 on success, non-zero on failure
 */
int Flash_Write(uint32_t addr, uint32_t data);

/**
 * @brief Erase a 1KB block of flash
 * @param addr Start address of block (must be 1KB-aligned)
 * @return 0 on success, non-zero on failure
 */
int Flash_Erase(uint32_t addr);

/**
 * @brief Initialize flash programming hardware
 * @param clk_khz System clock in kHz
 * @return 0 on success, non-zero on failure
 */
int Flash_Init(uint32_t clk_khz);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * @brief Get file system status information
 * @param status Pointer to FS_Status_t structure to fill
 */
void OS_FS_GetStatus(FS_Status_t *status);

/**
 * @brief Check if a file number is valid and exists
 * @param num File number to check
 * @return 1 if valid and exists, 0 otherwise
 */
uint8_t OS_File_Exists(uint8_t num);

/**
 * @brief Get number of free sectors remaining
 * @return Count of free sectors (0-254)
 */
uint8_t OS_FS_FreeSectors(void);

// =============================================================================
// GLOBAL VARIABLES (Exported)
// =============================================================================
extern uint8_t RAM_Directory[DIRECTORY_SIZE];   // Directory in RAM
extern uint8_t RAM_FAT[FAT_SIZE];               // FAT in RAM

// =============================================================================
// FILE SYSTEM ARCHITECTURE
// =============================================================================
/*
 * DISK LAYOUT (128 KB total):
 * - Address 0x00020000 - 0x0003FFFF: 256 sectors × 512 bytes
 * - Sector 0-254: Data sectors
 * - Sector 255: Metadata (256 bytes directory + 256 bytes FAT)
 *
 * DIRECTORY STRUCTURE:
 * - Array of 256 bytes (one per potential file)
 * - Entry value = first sector of file, or 0xFF if file doesn't exist
 * - Index = file number (0-254)
 *
 * FAT STRUCTURE:
 * - Array of 256 bytes (one per sector)
 * - Entry value = next sector in chain, or 0xFF if end of file
 * - Forms linked list of sectors for each file
 *
 * EXAMPLE:
 * File 0 with 3 sectors (5, 12, 8):
 *   RAM_Directory[0] = 5
 *   RAM_FAT[5] = 12
 *   RAM_FAT[12] = 8
 *   RAM_FAT[8] = 0xFF (end of chain)
 */

#endif // __OS_FILE_SYSTEM_H__