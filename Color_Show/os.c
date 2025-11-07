// *****************************************************************************
// os.c - Real-Time Operating System Implementation
// Runs on LM4F120/TM4C123
// A simple real time operating system with minimal features
// 
// *****************************************************************************

#include <stdint.h>
#include "os.h"
#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"

// =============================================================================
// PRIVATE FUNCTION PROTOTYPES
// =============================================================================
static void SetInitialStack(int threadIndex);
static void Clock_Init(void);
void Scheduler(void);           // Called from assembly (osasm.s)

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
tcbType tcbs[NUMTHREADS];                   // Thread control blocks
tcbType *RunPt;                             // Pointer to currently running thread
int32_t Stacks[NUMTHREADS][STACKSIZE];     // Thread stacks

// FIFO variables
uint32_t PutI;                              // Index for next put (exported for Get_Next)
uint32_t GetI;                              // Index for next get (exported for Get_Next)
uint32_t Fifo[FIFOSIZE];                    // FIFO buffer (exported for Get_Next)
int32_t CurrentSize;                        // Semaphore: current FIFO size
uint32_t LostData;                          // Count of lost data (overflow)

// =============================================================================
// OS INITIALIZATION
// =============================================================================

/**
 * @brief Initialize operating system
 */
void OS_Init(void) {
    OS_DisableInterrupts();
    Clock_Init();                           // Set processor clock to 16 MHz
    
    // Configure SysTick
    NVIC_ST_CTRL_R = 0;                    // Disable SysTick during setup
    NVIC_ST_CURRENT_R = 0;                 // Clear current value
    NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R & 0x00FFFFFF) | 0xE0000000; // Priority 7
}

/**
 * @brief Initialize clock (matches working configuration)
 */
static void Clock_Init(void) {
    SYSCTL_RCC_R |= 0x810;
    SYSCTL_RCC_R &= ~(0x400020);
}

// =============================================================================
// THREAD MANAGEMENT
// =============================================================================

/**
 * @brief Initialize stack for a new thread
 * @param threadIndex Index of thread (0 to NUMTHREADS-1)
 */
static void SetInitialStack(int threadIndex) {
    tcbs[threadIndex].sp = &Stacks[threadIndex][STACKSIZE - 16]; // Set SP
    
    // Initialize stack frame for context switch
    Stacks[threadIndex][STACKSIZE - 1]  = 0x01000000;   // PSR (Thumb bit set)
    Stacks[threadIndex][STACKSIZE - 3]  = 0x14141414;   // R14 (LR)
    Stacks[threadIndex][STACKSIZE - 4]  = 0x12121212;   // R12
    Stacks[threadIndex][STACKSIZE - 5]  = 0x03030303;   // R3
    Stacks[threadIndex][STACKSIZE - 6]  = 0x02020202;   // R2
    Stacks[threadIndex][STACKSIZE - 7]  = 0x01010101;   // R1
    Stacks[threadIndex][STACKSIZE - 8]  = 0x00000000;   // R0
    Stacks[threadIndex][STACKSIZE - 9]  = 0x11111111;   // R11
    Stacks[threadIndex][STACKSIZE - 10] = 0x10101010;   // R10
    Stacks[threadIndex][STACKSIZE - 11] = 0x09090909;   // R9
    Stacks[threadIndex][STACKSIZE - 12] = 0x08080808;   // R8
    Stacks[threadIndex][STACKSIZE - 13] = 0x07070707;   // R7
    Stacks[threadIndex][STACKSIZE - 14] = 0x06060606;   // R6
    Stacks[threadIndex][STACKSIZE - 15] = 0x05050505;   // R5
    Stacks[threadIndex][STACKSIZE - 16] = 0x04040404;   // R4
}

/**
 * @brief Add three threads to the scheduler
 */
int OS_AddThreads(void(*task0)(void),
                  void(*task1)(void),
                  void(*task2)(void)) {
    int32_t status;
    
    status = StartCritical();
    
    // Create circular linked list
    tcbs[0].next = &tcbs[1];
    tcbs[1].next = &tcbs[2];
    tcbs[2].next = &tcbs[0];
    
    // Initialize stacks and set PC for each thread
    SetInitialStack(0);
    Stacks[0][STACKSIZE - 2] = (int32_t)(task0);  // PC
    
    SetInitialStack(1);
    Stacks[1][STACKSIZE - 2] = (int32_t)(task1);  // PC
    
    SetInitialStack(2);
    Stacks[2][STACKSIZE - 2] = (int32_t)(task2);  // PC
    
    // Initialize thread states - CRITICAL for proper operation
    tcbs[0].blocked = 0;
    tcbs[0].sleep = 0;
    tcbs[1].blocked = 0;
    tcbs[1].sleep = 0;
    tcbs[2].blocked = 0;
    tcbs[2].sleep = 0;
    
    RunPt = &tcbs[0];  // Thread 0 runs first
    
    EndCritical(status);
    return 1;  // Success
}

/**
 * @brief Launch the operating system
 */
void OS_Launch(uint32_t theTimeSlice) {
    NVIC_ST_RELOAD_R = theTimeSlice - 1;   // Set reload value
    NVIC_ST_CTRL_R = 0x00000007;           // Enable SysTick, core clock, interrupt
    StartOS();                              // Start first task (defined in osasm.s)
}

/**
 * @brief Force context switch by triggering SysTick
 */
void OS_Suspend(void) {
    NVIC_ST_CURRENT_R = 0;                 // Reset counter
    NVIC_INT_CTRL_R |= 0x04000000;         // Trigger SysTick interrupt
}

/**
 * @brief Put current thread to sleep
 */
void OS_Sleep(uint32_t sleepTime) {
    RunPt->sleep = (int32_t)sleepTime;
    OS_Suspend();  // Give up CPU
}

// =============================================================================
// SCHEDULER
// =============================================================================

/**
 * @brief Round-robin scheduler with sleep and blocking support
 * @note Called from SysTick_Handler in osasm.s
 */
void Scheduler(void){
  // Decrement sleep counters for all threads every 2 ms timeslice
  tcbType *pt = RunPt;
  for(int i=0;i<NUMTHREADS;i++){
    if(pt->sleep > 0){ pt->sleep--; }
    pt = pt->next;
  }
  // Find the next thread that is not blocked and not sleeping
  RunPt = RunPt->next;
  while((RunPt->blocked != 0) || (RunPt->sleep > 0)){
    RunPt = RunPt->next;
  }
}

// =============================================================================
// SEMAPHORE IMPLEMENTATION
// =============================================================================

/**
 * @brief Initialize semaphore
 */
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value) {
    OS_DisableInterrupts();
    *semaPt = value;
    OS_EnableInterrupts();
}

/**
 * @brief Wait on semaphore (P operation, blocking)
 */
void OS_Wait(Sema4Type *semaPt) {
    OS_DisableInterrupts();
    
    (*semaPt) = (*semaPt) - 1;
    
    if ((*semaPt) < 0) {
        // Block this thread
        RunPt->blocked = (uint32_t *)semaPt;
        OS_EnableInterrupts();
        OS_Suspend();  // Switch to another thread
    } else {
        OS_EnableInterrupts();
    }
}

/**
 * @brief Signal semaphore (V operation, unblocking)
 */
void OS_Signal(Sema4Type *semaPt) {
    tcbType *pt;
    
    OS_DisableInterrupts();
    
    (*semaPt) = (*semaPt) + 1;
    
    if ((*semaPt) <= 0) {
        // Wake up one blocked thread
        pt = RunPt->next;
        
        while (pt->blocked != (uint32_t *)semaPt) {
            pt = pt->next;
        }
        
        pt->blocked = 0;  // Unblock the thread
    }
    
    OS_EnableInterrupts();
}

// =============================================================================
// FIFO IMPLEMENTATION
// =============================================================================

/**
 * @brief Initialize FIFO
 */
void OS_Fifo_Init(void) {
    PutI = 0;
    GetI = 0;
    OS_InitSemaphore(&CurrentSize, 0);  // Initially empty
    LostData = 0;
}

/**
 * @brief Put data into FIFO (non-blocking)
 */
int OS_Fifo_Put(uint32_t data) {
    if (CurrentSize == FIFOSIZE) {
        LostData++;
        return -1;  // FIFO full
    }
    
    Fifo[PutI] = data;
    PutI = (PutI + 1) % FIFOSIZE;
    OS_Signal(&CurrentSize);  // Increment size
    
    return 0;  // Success
}

/**
 * @brief Get data from FIFO (blocking)
 */
uint32_t OS_Fifo_Get(void) {
    uint32_t data;
    
    OS_Wait(&CurrentSize);  // Block if empty
    
    data = Fifo[GetI];
    GetI = (GetI + 1) % FIFOSIZE;
    
    return data;
}

/**
 * @brief Peek at next value without removing
 * @note This is called AFTER OS_Fifo_Get, so GetI already points to the next item
 */
uint32_t Get_Next(void){
    if(CurrentSize <= 0){
        return 8;  // No next item (queue empty)
    }
    else if(CurrentSize > FIFOSIZE){
        return 8;  // Error
    }
    else{
        // GetI already points to next item (it was incremented by OS_Fifo_Get)
        return Fifo[GetI];
    }
}