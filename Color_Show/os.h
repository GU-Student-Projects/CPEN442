// *****************************************************************************
// os.h - Real-Time Operating System Header
// Runs on LM4F120/TM4C123
// A simple real time operating system with minimal features
// 
// *****************************************************************************

#ifndef __OS_H
#define __OS_H

#include <stdint.h>

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================
#define NUMTHREADS  3           // Maximum number of threads
#define STACKSIZE   100         // Number of 32-bit words in stack per thread
#define FIFOSIZE    10          // Size of general-purpose FIFO

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

// Thread Control Block (TCB)
typedef struct tcb {
    int32_t *sp;                // Stack pointer (valid for non-running threads)
    struct tcb *next;           // Linked-list pointer to next thread
    uint32_t *blocked;          // Pointer to semaphore if blocked, NULL otherwise
    int32_t sleep;              // Sleep counter in milliseconds (0 = not sleeping)
} tcbType;

// Semaphore Type
typedef int32_t Sema4Type;

// =============================================================================
// CORE OS FUNCTIONS
// =============================================================================

/**
 * @brief Initialize operating system
 * @details Disables interrupts, sets up 16 MHz clock, configures SysTick
 * @note Must be called before any other OS functions
 */
void OS_Init(void);

/**
 * @brief Add three foreground threads to the scheduler
 * @param task0 Pointer to first thread function
 * @param task1 Pointer to second thread function
 * @param task2 Pointer to third thread function
 * @return 1 if successful, 0 if threads cannot be added
 * @note All three threads must be specified
 */
int OS_AddThreads(void(*task0)(void), 
                  void(*task1)(void), 
                  void(*task2)(void));

/**
 * @brief Start the scheduler and enable interrupts
 * @param theTimeSlice Number of bus cycles for each time slice (max 24 bits)
 * @note This function does not return - it starts the RTOS
 * @example OS_Launch(160000) for 2ms time slice at 16 MHz (actually ~10ms at 16MHz)
 */
void OS_Launch(uint32_t theTimeSlice);

/**
 * @brief Force immediate context switch
 * @details Triggers SysTick interrupt to switch to next ready thread
 */
void OS_Suspend(void);

/**
 * @brief Put calling thread to sleep
 * @param sleepTime Sleep duration in milliseconds
 * @note Thread becomes unschedulable for specified time
 * @note Uses time slice for timing (set by OS_Launch)
 */
void OS_Sleep(uint32_t sleepTime);

// =============================================================================
// SEMAPHORE FUNCTIONS
// =============================================================================

/**
 * @brief Initialize a semaphore
 * @param semaPt Pointer to semaphore variable
 * @param value Initial value (typically 0 for signaling, 1 for mutex)
 * @note Must be called before using semaphore
 */
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value);

/**
 * @brief Wait on a semaphore (P operation)
 * @param semaPt Pointer to semaphore variable
 * @details Decrements semaphore; blocks if result is negative
 * @note Blocking puts thread in blocked state until OS_Signal called
 */
void OS_Wait(Sema4Type *semaPt);

/**
 * @brief Signal a semaphore (V operation)
 * @param semaPt Pointer to semaphore variable
 * @details Increments semaphore; wakes one blocked thread if any
 */
void OS_Signal(Sema4Type *semaPt);

// =============================================================================
// FIFO FUNCTIONS (Producer-Consumer Buffer)
// =============================================================================

/**
 * @brief Initialize the FIFO buffer
 * @details Resets put/get indices and initializes size semaphore
 * @note Must be called before using FIFO
 */
void OS_Fifo_Init(void);

/**
 * @brief Put data into FIFO (non-blocking)
 * @param data 32-bit data to store
 * @return 0 if successful, -1 if FIFO is full
 * @note Does not block if full - returns error instead
 */
int OS_Fifo_Put(uint32_t data);

/**
 * @brief Get data from FIFO (blocking)
 * @return 32-bit data from FIFO
 * @note Blocks if FIFO is empty until data available
 */
uint32_t OS_Fifo_Get(void);

/**
 * @brief Peek at next item in FIFO without removing it
 * @return Next value in FIFO or 0 if empty
 * @note Non-blocking, returns 0 if queue empty
 */
uint32_t Get_Next(void);

// =============================================================================
// INTERRUPT CONTROL FUNCTIONS
// =============================================================================

/**
 * @brief Disable interrupts (set I bit in PRIMASK)
 * @note Used for critical sections
 */
void OS_DisableInterrupts(void);

/**
 * @brief Enable interrupts (clear I bit in PRIMASK)
 * @note Re-enables interrupts after critical section
 */
void OS_EnableInterrupts(void);

/**
 * @brief Start critical section (returns previous interrupt state)
 * @return Previous PRIMASK value
 * @note Use with EndCritical for nested critical sections
 */
int32_t StartCritical(void);

/**
 * @brief End critical section (restore previous interrupt state)
 * @param primask Previous PRIMASK value from StartCritical
 */
void EndCritical(int32_t primask);

// =============================================================================
// GLOBAL VARIABLES (Exported from os.c)
// =============================================================================
extern tcbType tcbs[NUMTHREADS];    // Thread control blocks
extern tcbType *RunPt;              // Pointer to currently running thread
extern int32_t CurrentSize;         // Current FIFO size (semaphore)
extern uint32_t LostData;           // Count of data lost due to full FIFO
extern uint32_t Fifo[FIFOSIZE];     // FIFO buffer (for Get_Next access)
extern uint32_t GetI;               // Get index (for Get_Next access)
extern uint32_t PutI;               // Put index (for Get_Next access)

#endif // __OS_H