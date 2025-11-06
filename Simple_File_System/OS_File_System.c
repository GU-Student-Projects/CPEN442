// *****************************************************************************
// OS_File_System.c - Write-Once File System Implementation
// Runs on LM4F120/TM4C123
// Simple file system stored in flash memory with FAT-like structure
// 
// *****************************************************************************

#include "OS_File_System.h"
#include "FlashProgram.h"
#include <stdint.h>

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
uint8_t RAM_Directory[DIRECTORY_SIZE];     // Directory loaded in RAM
uint8_t RAM_FAT[FAT_SIZE];                 // FAT loaded in RAM

// =============================================================================
// INITIALIZATION
// =============================================================================

/**
 * @brief Initialize file system structures in RAM
 */
void OS_FS_Init(void) {
    uint16_t i;
    
    // Mark all directory entries as free
    for (i = 0; i < DIRECTORY_SIZE; i++) {
        RAM_Directory[i] = FILE_EMPTY;
    }
    
    // Mark all FAT entries as free
    for (i = 0; i < FAT_SIZE; i++) {
        RAM_FAT[i] = SECTOR_FREE;
    }
}

// =============================================================================
// FILE OPERATIONS
// =============================================================================

/**
 * @brief Create a new file and return its file number
 */
uint8_t OS_File_New(void) {
    uint16_t i;
    
    // Check if disk has at least one free sector
    if (find_free_sector() == FS_DISK_FULL) {
        return FS_ERROR;
    }
    
    // Find first available file slot in directory
    for (i = 0; i <= MAX_FILE_NUMBER; i++) {
        if (RAM_Directory[i] == FILE_EMPTY) {
            // Mark as empty file (no sectors allocated yet)
            RAM_Directory[i] = FILE_EMPTY;
            return (uint8_t)i;
        }
    }
    
    // All file slots are in use
    return FS_ERROR;
}

/**
 * @brief Get the size of a file in sectors
 */
uint8_t OS_File_Size(uint8_t num) {
    uint8_t count = 0;
    uint8_t sector;
    
    // Validate file number
    if (num > MAX_FILE_NUMBER) {
        return 0;
    }
    
    sector = RAM_Directory[num];
    
    // File is empty or doesn't exist
    if (sector == FILE_EMPTY) {
        return 0;
    }
    
    // Follow the FAT chain and count sectors
    while (sector != SECTOR_FREE) {
        count++;
        sector = RAM_FAT[sector];
        
        // Sanity check to prevent infinite loop
        if (count > NUM_SECTORS) {
            return 0;  // Corrupted FAT
        }
    }
    
    return count;
}

/**
 * @brief Append 512 bytes to a file
 */
uint8_t OS_File_Append(uint8_t num, uint8_t buf[512]) {
    uint8_t freeSector;
    
    // Validate file number
    if (num > MAX_FILE_NUMBER) {
        return FS_ERROR;
    }
    
    // Find a free sector
    freeSector = find_free_sector();
    if (freeSector == FS_DISK_FULL) {
        return FS_DISK_FULL;
    }
    
    // Write data to flash sector
    if (eDisk_WriteSector(buf, freeSector) != FS_SUCCESS) {
        return FS_ERROR;  // Write failure
    }
    
    // Update directory/FAT to link this sector
    append_fat(num, freeSector);
    
    return FS_SUCCESS;
}

/**
 * @brief Read 512 bytes from a file at specified location
 */
uint8_t OS_File_Read(uint8_t num, uint8_t location, uint8_t buf[512]) {
    uint8_t i;
    uint8_t sector;
    uint32_t addr;
    uint8_t *flashPtr;
    
    // Validate file number
    if (num > MAX_FILE_NUMBER) {
        return FS_NO_DATA;
    }
    
    sector = RAM_Directory[num];
    
    // File is empty or doesn't exist
    if (sector == FILE_EMPTY) {
        return FS_NO_DATA;
    }
    
    // Traverse the FAT chain to reach the requested sector
    for (i = 0; i < location; i++) {
        sector = RAM_FAT[sector];
        
        // Requested location doesn't exist
        if (sector == SECTOR_FREE) {
            return FS_NO_DATA;
        }
    }
    
    // Compute physical address of this sector
    addr = DISK_START_ADDRESS + ((uint32_t)sector * SECTOR_SIZE);
    flashPtr = (uint8_t *)addr;
    
    // Copy data from flash to RAM buffer
    for (i = 0; i < SECTOR_SIZE; i++) {
        buf[i] = flashPtr[i];
    }
    
    return FS_SUCCESS;
}

// =============================================================================
// PERSISTENCE OPERATIONS
// =============================================================================

/**
 * @brief Write directory and FAT to flash (sector 255)
 */
uint8_t OS_File_Flush(void) {
    uint8_t buffer[SECTOR_SIZE];
    uint16_t i;
    
    // Pack directory and FAT into one 512-byte buffer
    // First 256 bytes: directory
    for (i = 0; i < DIRECTORY_SIZE; i++) {
        buffer[i] = RAM_Directory[i];
    }
    
    // Second 256 bytes: FAT
    for (i = 0; i < FAT_SIZE; i++) {
        buffer[i + DIRECTORY_SIZE] = RAM_FAT[i];
    }
    
    // Write to metadata sector (sector 255)
    if (eDisk_WriteSector(buffer, METADATA_SECTOR) != FS_SUCCESS) {
        return FS_ERROR;
    }
    
    return FS_SUCCESS;
}

/**
 * @brief Load directory and FAT from flash (sector 255)
 */
uint8_t OS_File_Mount(void) {
    uint8_t buffer[SECTOR_SIZE];
    uint16_t i;
    uint32_t addr;
    uint8_t *flashPtr;
    
    // Read metadata sector from flash
    addr = DISK_START_ADDRESS + ((uint32_t)METADATA_SECTOR * SECTOR_SIZE);
    flashPtr = (uint8_t *)addr;
    
    // Copy from flash to temporary buffer
    for (i = 0; i < SECTOR_SIZE; i++) {
        buffer[i] = flashPtr[i];
    }
    
    // Unpack directory (first 256 bytes)
    for (i = 0; i < DIRECTORY_SIZE; i++) {
        RAM_Directory[i] = buffer[i];
    }
    
    // Unpack FAT (second 256 bytes)
    for (i = 0; i < FAT_SIZE; i++) {
        RAM_FAT[i] = buffer[i + DIRECTORY_SIZE];
    }
    
    return FS_SUCCESS;
}

/**
 * @brief Erase all files and format the disk
 */
uint8_t OS_File_Format(void) {
    uint32_t address;
    int result;
    
    // Erase from start to end of disk in 1KB blocks
    address = DISK_START_ADDRESS;
    
    while (address < DISK_END_ADDRESS) {
        result = Flash_Erase(address);
        
        if (result != NOERROR) {  // NOERROR defined in FlashProgram.h as 0
            return FS_ERROR;  // Erase failure
        }
        
        address += 1024;  // Move to next 1KB block
    }
    
    // Reinitialize RAM structures after format
    OS_FS_Init();
    
    return FS_SUCCESS;
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Find the first free (unused) sector
 */
uint8_t find_free_sector(void) {
    int16_t firstFreeSector = -1;
    int16_t lastUsedSector = -1;
    uint8_t lastSectorInFile;
    uint16_t i;
    
    // Scan all files to find the highest used sector
    for (i = 0; i < DIRECTORY_SIZE; i++) {
        if (RAM_Directory[i] != FILE_EMPTY) {
            lastSectorInFile = last_sector(RAM_Directory[i]);
            
            if (lastSectorInFile != SECTOR_FREE) {
                if ((int16_t)lastSectorInFile > lastUsedSector) {
                    lastUsedSector = (int16_t)lastSectorInFile;
                }
            }
        }
    }
    
    // First free sector is one after the last used sector
    firstFreeSector = lastUsedSector + 1;
    
    // Check if we have space (can't use sector 255 - reserved for metadata)
    if (firstFreeSector >= METADATA_SECTOR) {
        return FS_DISK_FULL;
    }
    
    return (uint8_t)firstFreeSector;
}

/**
 * @brief Find the last sector in a file's chain
 */
uint8_t last_sector(uint8_t start) {
    uint8_t current;
    uint8_t next;
    uint8_t count = 0;
    
    // File is empty
    if (start == FILE_EMPTY) {
        return SECTOR_FREE;
    }
    
    current = start;
    
    // Follow the FAT chain to the end
    while (1) {
        next = RAM_FAT[current];
        
        // Found end of chain
        if (next == SECTOR_FREE) {
            return current;
        }
        
        current = next;
        count++;
        
        // Sanity check to prevent infinite loop
        if (count > NUM_SECTORS) {
            return SECTOR_FREE;  // Corrupted FAT
        }
    }
}

/**
 * @brief Append a sector to a file's FAT chain
 */
void append_fat(uint8_t num, uint8_t n) {
    uint8_t current;
    uint8_t next;
    
    // Mark new sector as end of chain
    RAM_FAT[n] = SECTOR_FREE;
    
    // If file is empty, this is the first sector
    if (RAM_Directory[num] == FILE_EMPTY) {
        RAM_Directory[num] = n;
        return;
    }
    
    // File already has sectors - find the last one
    current = RAM_Directory[num];
    
    while (1) {
        next = RAM_FAT[current];
        
        // Found the last sector in chain
        if (next == SECTOR_FREE) {
            RAM_FAT[current] = n;  // Link new sector
            return;
        }
        
        current = next;
    }
}

// =============================================================================
// LOW-LEVEL DISK FUNCTIONS
// =============================================================================

/**
 * @brief Write 512 bytes to a flash sector
 */
uint8_t eDisk_WriteSector(uint8_t buf[512], uint8_t n) {
    uint32_t addr;
    uint32_t dataWord;
    uint16_t i;
    int result;
    
    // Calculate physical address
    addr = DISK_START_ADDRESS + ((uint32_t)n * SECTOR_SIZE);
    
    // Write data 4 bytes at a time (flash is word-addressable)
    for (i = 0; i < SECTOR_SIZE; i += 4) {
        // Pack 4 bytes into 32-bit word (little-endian)
        dataWord = (uint32_t)buf[i] | 
                   ((uint32_t)buf[i + 1] << 8) | 
                   ((uint32_t)buf[i + 2] << 16) | 
                   ((uint32_t)buf[i + 3] << 24);
        
        result = Flash_Write(addr + i, dataWord);
        
        if (result != NOERROR) {  // NOERROR defined in FlashProgram.h as 0
            return 1;  // Write failure
        }
    }
    
    return 0;  // Success
}

/**
 * @brief Read 512 bytes from a flash sector
 */
uint8_t eDisk_ReadSector(uint8_t buf[512], uint8_t n) {
    uint32_t addr;
    uint8_t *flashPtr;
    uint16_t i;
    
    // Calculate physical address
    addr = DISK_START_ADDRESS + ((uint32_t)n * SECTOR_SIZE);
    flashPtr = (uint8_t *)addr;
    
    // Copy from flash to RAM
    for (i = 0; i < SECTOR_SIZE; i++) {
        buf[i] = flashPtr[i];
    }
    
    return 0;  // Success
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * @brief Get file system status information
 */
void OS_FS_GetStatus(FS_Status_t *status) {
    uint16_t i;
    uint8_t totalFiles = 0;
    uint8_t usedSectors = 0;
    
    if (status == 0) {
        return;  // Null pointer
    }
    
    // Count files
    for (i = 0; i <= MAX_FILE_NUMBER; i++) {
        if (RAM_Directory[i] != FILE_EMPTY) {
            totalFiles++;
        }
    }
    
    // Count used sectors by scanning FAT
    for (i = 0; i < METADATA_SECTOR; i++) {
        // Check if sector is referenced in directory
        for (uint8_t j = 0; j <= MAX_FILE_NUMBER; j++) {
            if (RAM_Directory[j] == i) {
                usedSectors++;
                break;
            }
        }
        
        // Check if sector is referenced in FAT
        for (uint8_t j = 0; j < METADATA_SECTOR; j++) {
            if (RAM_FAT[j] == i) {
                usedSectors++;
                break;
            }
        }
    }
    
    status->totalFiles = totalFiles;
    status->usedSectors = usedSectors;
    status->freeSectors = METADATA_SECTOR - usedSectors;  // Sector 255 reserved
}

/**
 * @brief Check if a file number is valid and exists
 */
uint8_t OS_File_Exists(uint8_t num) {
    if (num > MAX_FILE_NUMBER) {
        return 0;  // Invalid file number
    }
    
    return (RAM_Directory[num] != FILE_EMPTY) ? 1 : 0;
}

/**
 * @brief Get number of free sectors remaining
 */
uint8_t OS_FS_FreeSectors(void) {
    uint8_t freeSector = find_free_sector();
    
    if (freeSector == FS_DISK_FULL) {
        return 0;
    }
    
    return (METADATA_SECTOR - freeSector);
}