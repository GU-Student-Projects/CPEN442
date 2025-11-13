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

void OS_Launch(uint32_t theTimeSlice);

void OS_Suspend(void);

void OS_Sleep(uint32_t sleepTime);

// =============================================================================
// SEMAPHORE FUNCTIONS
// =============================================================================

void OS_InitSemaphore(Sema4Type *semaPt, int32_t value);

void OS_Wait(Sema4Type *semaPt);

void OS_Signal(Sema4Type *semaPt);

// =============================================================================
// FIFO FUNCTIONS
// =============================================================================

void OS_Fifo_Init(void);

int OS_Fifo_Put(uint32_t data);

uint32_t OS_Fifo_Get(void);

uint32_t Get_Next(void);

// =============================================================================
// INTERRUPT CONTROL FUNCTIONS
// =============================================================================

void OS_DisableInterrupts(void);

void OS_EnableInterrupts(void);

int32_t StartCritical(void);

void EndCritical(int32_t primask);

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
extern tcbType tcbs[NUMTHREADS];    // Thread control blocks
extern tcbType *RunPt;              // Pointer to currently running thread
extern int32_t CurrentSize;         // Current FIFO size (semaphore)
extern uint32_t LostData;           // Count of data lost due to full FIFO
extern uint32_t Fifo[FIFOSIZE];     // FIFO buffer (for Get_Next access)
extern uint32_t GetI;               // Get index (for Get_Next access)
extern uint32_t PutI;               // Put index (for Get_Next access)

#endif // __OS_H