#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "tm4c123gh6pm_def.h"

// OS Configuration
#define NUMTHREADS  6        // Maximum number of threads
#define STACKSIZE   256      // Number of 32-bit words in stack
#define FIFOSIZE    10       // Size of color FIFO queue
#define TIMESLICE   32000    // 2ms at 16MHz (16MHz * 0.002s)

// System Registers
#define NVIC_ST_CTRL_R          (*((volatile uint32_t *)0xE000E010))
#define NVIC_ST_RELOAD_R        (*((volatile uint32_t *)0xE000E014))
#define NVIC_ST_CURRENT_R       (*((volatile uint32_t *)0xE000E018))
#define NVIC_SYS_PRI3_R         (*((volatile uint32_t *)0xE000ED20))
#define NVIC_ST_CTRL_CLK_SRC    0x00000004
#define NVIC_ST_CTRL_INTEN      0x00000002
#define NVIC_ST_CTRL_ENABLE     0x00000001

// Thread Control Block
struct tcb {
    int32_t *sp;
    struct tcb *next;
    uint32_t sleepCount;
    uint32_t blockSemaPt;
    uint32_t id;
    uint32_t priority;
};
typedef struct tcb tcbType;

// Semaphore structure
struct sema {
    int32_t value;        
    tcbType *blockedList;
};
typedef struct sema Sema4Type;

// Global OS variables
tcbType tcbs[NUMTHREADS];
tcbType *RunPt;
tcbType *NextPt;
int32_t Stacks[NUMTHREADS][STACKSIZE];
uint32_t NumThreads;
static uint32_t MSTime;

// Color FIFO structure
struct colorFifo {
    uint8_t buffer[FIFOSIZE];
    uint8_t *putPt;      
    uint8_t *getPt;
    Sema4Type currentSize;
    Sema4Type roomLeft; 
    Sema4Type mutex;
};
typedef struct colorFifo ColorFifoType;

// Global FIFO for colors
ColorFifoType ColorQueue;

// Function prototypes from assembly
void OS_DisableInterrupts(void); // Disable interrupts
void OS_EnableInterrupts(void);  // Enable interrupts
int32_t StartCritical(void);
void EndCritical(int32_t primask);
void Clock_Init(void);
void StartOS(void);

// Initialize stack for thread
void SetInitialStack(uint32_t i) {
    tcbs[i].sp = &Stacks[i][STACKSIZE-16]; // thread stack pointer
    Stacks[i][STACKSIZE-1] = 0x01000000;  // Thumb bit
    Stacks[i][STACKSIZE-3] = 0x14141414;  // R14
    Stacks[i][STACKSIZE-4] = 0x12121212;  // R12
    Stacks[i][STACKSIZE-5] = 0x03030303;  // R3
    Stacks[i][STACKSIZE-6] = 0x02020202;  // R2
    Stacks[i][STACKSIZE-7] = 0x01010101;  // R1
    Stacks[i][STACKSIZE-8] = 0x00000000;  // R0
    Stacks[i][STACKSIZE-9] = 0x11111111;  // R11
    Stacks[i][STACKSIZE-10] = 0x10101010; // R10
    Stacks[i][STACKSIZE-11] = 0x09090909; // R9
    Stacks[i][STACKSIZE-12] = 0x08080808; // R8
    Stacks[i][STACKSIZE-13] = 0x07070707; // R7
    Stacks[i][STACKSIZE-14] = 0x06060606; // R6
    Stacks[i][STACKSIZE-15] = 0x05050505; // R5
    Stacks[i][STACKSIZE-16] = 0x04040404; // R4
}

// ******** OS_Init ************
// Initialize operating system
void OS_Init(void) {
    OS_DisableInterrupts();
    // Initialize PLL for 16 MHz
    SYSCTL_RCC_R |= 0x00000800;   // BYPASS PLL
    SYSCTL_RCC_R &= ~0x00400000;  // Clear USESYSDIV
    
    MSTime = 0;
    NumThreads = 0;
    
    // Configure SysTick
    NVIC_ST_CTRL_R = 0;         // Disable SysTick during setup
    NVIC_ST_CURRENT_R = 0;      // Any write to current clears it
    // Set SysTick priority to 7 (lowest)
    NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R & 0x00FFFFFF) | 0xE0000000;
}

// ******** OS_AddThread ************
// Add a thread to the scheduler
// Input: pointer to thread function, thread ID, initial stack size
// Output: 1 if successful, 0 if failed
int OS_AddThread(void(*task)(void), uint32_t id, uint32_t stackSize) {
    int32_t status;
    status = StartCritical();
    
    if(NumThreads >= NUMTHREADS) {
        EndCritical(status);
        return 0; // No room for more threads
    }
    
    // Initialize TCB
    tcbs[NumThreads].id = id;
    tcbs[NumThreads].sleepCount = 0;
    tcbs[NumThreads].blockSemaPt = 0;
    tcbs[NumThreads].priority = 0;
    
    SetInitialStack(NumThreads);
    Stacks[NumThreads][STACKSIZE-2] = (int32_t)(task); // PC
    
    if(NumThreads == 0) {
        tcbs[0].next = &tcbs[0]; // Point to self if only thread
        RunPt = &tcbs[0];
    } else {
        // Insert into circular linked list
        tcbs[NumThreads].next = tcbs[NumThreads-1].next;
        tcbs[NumThreads-1].next = &tcbs[NumThreads];
    }
    
    NumThreads++;
    EndCritical(status);
    return 1;
}

// ******** OS_Launch ************
// Start the scheduler
void OS_Launch(uint32_t theTimeSlice) {
    NVIC_ST_RELOAD_R = theTimeSlice - 1; // Reload value
    NVIC_ST_CTRL_R = 0x00000007; // Enable with core clock and interrupts
    StartOS();                    // Assembly routine to start first thread
}

// ******** OS_Sleep ************
// Put thread to sleep for specified time
// Input: sleep time in milliseconds
void OS_Sleep(uint32_t sleepTime) {
    RunPt->sleepCount = sleepTime; // Set sleep counter
    OS_Suspend();                   // Give up CPU
}

// ******** OS_Suspend ************
// Suspend current thread and run scheduler
void OS_Suspend(void) {
    // Trigger SysTick to perform context switch
    NVIC_ST_CURRENT_R = 0; // Any write to current triggers SysTick
}

// ******** OS_InitSemaphore ************
// Initialize counting semaphore
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value) {
    OS_DisableInterrupts();
    semaPt->value = value;
    semaPt->blockedList = 0;
    OS_EnableInterrupts();
}

// ******** OS_Wait ************
// Decrement semaphore, block if less than zero
void OS_Wait(Sema4Type *semaPt) {
    OS_DisableInterrupts();
    (semaPt->value)--;
    
    if(semaPt->value < 0) {
        // Block this thread
        RunPt->blockSemaPt = (uint32_t)semaPt;
        
        // Add to blocked list
        tcbType *pt = semaPt->blockedList;
        if(pt == 0) {
            semaPt->blockedList = RunPt;
        } else {
            while(pt->next != 0) {
                pt = pt->next;
            }
            pt->next = RunPt;
        }
        
        OS_EnableInterrupts();
        OS_Suspend(); // Switch threads
    } else {
        OS_EnableInterrupts();
    }
}

// ******** OS_Signal ************
// Increment semaphore, wake up blocked thread if any
void OS_Signal(Sema4Type *semaPt) {
    tcbType *pt;
    
    OS_DisableInterrupts();
    (semaPt->value)++;
    
    if(semaPt->value <= 0) {
        // Wake up one blocked thread
        pt = semaPt->blockedList;
        if(pt != 0) {
            semaPt->blockedList = pt->next;
            pt->blockSemaPt = 0; // No longer blocked
        }
    }
    OS_EnableInterrupts();
}

// ******** OS_bWait ************
// Binary semaphore wait
void OS_bWait(Sema4Type *semaPt) {
    OS_DisableInterrupts();
    while(semaPt->value == 0) {
        OS_EnableInterrupts();
        OS_Suspend();
        OS_DisableInterrupts();
    }
    semaPt->value = 0;
    OS_EnableInterrupts();
}

// ******** OS_bSignal ************
// Binary semaphore signal
void OS_bSignal(Sema4Type *semaPt) {
    OS_DisableInterrupts();
    semaPt->value = 1;
    OS_EnableInterrupts();
}

// ******** Scheduler ************
// Select next thread to run
// Called from SysTick_Handler every 2ms
void Scheduler(void) {
    tcbType *pt;
    tcbType *bestPt;
    uint32_t max;
    
    // Update sleep counters every 2ms
    for(uint32_t i = 0; i < NumThreads; i++) {
        if(tcbs[i].sleepCount > 0) {
            tcbs[i].sleepCount--;
        }
    }
    
    // Update system time
    MSTime += 2;
    
    // Find next runnable thread (round-robin among ready threads)
    pt = RunPt->next;
    while(1) {
        if((pt->sleepCount == 0) && (pt->blockSemaPt == 0)) {
            // Thread is ready to run
            RunPt = pt;
            return;
        }
        pt = pt->next;
        if(pt == RunPt->next) {

            return;
        }
    }
}


// ******** OS_Time ************
// Get system time in milliseconds
uint32_t OS_Time(void) {
    return MSTime;
}

// ******** ColorFifo_Init ************
// Initialize color FIFO
void ColorFifo_Init(void) {
    ColorQueue.putPt = ColorQueue.getPt = &ColorQueue.buffer[0];
    OS_InitSemaphore(&ColorQueue.currentSize, 0);  // Initially empty
    OS_InitSemaphore(&ColorQueue.roomLeft, FIFOSIZE); // All room available
    OS_InitSemaphore(&ColorQueue.mutex, 1);        // Binary semaphore for mutual exclusion
}

// ******** ColorFifo_Put ************
// Put color into FIFO (blocking if full)
// Returns: 1 if successful, 0 if failed
uint32_t ColorFifo_Put(uint8_t color) {
    OS_Wait(&ColorQueue.roomLeft);  // Block if no room
    OS_Wait(&ColorQueue.mutex);     // Mutual exclusion
    
    *(ColorQueue.putPt) = color;
    ColorQueue.putPt++;
    if(ColorQueue.putPt == &ColorQueue.buffer[FIFOSIZE]) {
        ColorQueue.putPt = &ColorQueue.buffer[0]; // Wrap
    }
    
    OS_Signal(&ColorQueue.mutex);
    OS_Signal(&ColorQueue.currentSize); // One more element
    return 1;
}

// ******** ColorFifo_Get ************
// Get color from FIFO (blocking if empty)
// Returns: color value
uint8_t ColorFifo_Get(void) {
    uint8_t color;
    
    OS_Wait(&ColorQueue.currentSize);  // Block if empty
    OS_Wait(&ColorQueue.mutex);        // Mutual exclusion
    
    color = *(ColorQueue.getPt);
    ColorQueue.getPt++;
    if(ColorQueue.getPt == &ColorQueue.buffer[FIFOSIZE]) {
        ColorQueue.getPt = &ColorQueue.buffer[0]; // Wrap
    }
    
    OS_Signal(&ColorQueue.mutex);
    OS_Signal(&ColorQueue.roomLeft);  // One more room available
    return color;
}

// ******** ColorFifo_Size ************
// Get current size of FIFO
uint32_t ColorFifo_Size(void) {
    return ColorQueue.currentSize.value;
}

// ******** ColorFifo_IsFull ************
// Check if FIFO is full
uint32_t ColorFifo_IsFull(void) {
    return (ColorQueue.roomLeft.value == 0);
}

// ******** ColorFifo_IsEmpty ************
// Check if FIFO is empty
uint32_t ColorFifo_IsEmpty(void) {
    return (ColorQueue.currentSize.value == 0);
}