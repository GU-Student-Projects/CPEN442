// os_v1.c
// Runs on LM4F120/TM4C123
// A very simple real time operating system with minimal features.
// Updated with blocking semaphores and scheduler support
// Gabriel DiMartino
// September 11, 2025

#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"

/* 
#define NVIC_ST_CTRL_R          (*((volatile uint32_t *)0xE000E010))
#define NVIC_ST_CTRL_CLK_SRC    0x00000004  // Clock Source
#define NVIC_ST_CTRL_INTEN      0x00000002  // Interrupt enable
#define NVIC_ST_CTRL_ENABLE     0x00000001  // Counter mode
#define NVIC_ST_RELOAD_R        (*((volatile uint32_t *)0xE000E014))
#define NVIC_ST_CURRENT_R       (*((volatile uint32_t *)0xE000E018))
#define NVIC_INT_CTRL_R         (*((volatile uint32_t *)0xE000ED04))
#define NVIC_INT_CTRL_PENDSTSET 0x04000000  // Set pending SysTick interrupt
#define NVIC_SYS_PRI3_R         (*((volatile uint32_t *)0xE000ED20))  // Sys. Handlers 12 to 15 Priority
*/

// function definitions in osasm.s
void OS_DisableInterrupts(void); // Disable interrupts
void OS_EnableInterrupts(void);  // Enable interrupts
int32_t StartCritical(void);
void EndCritical(int32_t primask);
void Clock_Init(void);
void StartOS(void);

#define NUMTHREADS  3        // maximum number of threads
#define STACKSIZE   100      // number of 32-bit words in stack

// TCB structure with blocking support
struct tcb{
  int32_t *sp;       // pointer to stack (valid for threads not running)
  struct tcb *next;  // linked-list pointer for round-robin
  struct tcb *blocked;  // linked-list pointer for blocked threads
  uint32_t *blockPt; // pointer to semaphore thread is blocked on (0 if not blocked)
  uint32_t sleep;    // sleep counter (0 if not sleeping)
  uint8_t priority;  // thread priority (if needed for future use)
};

typedef struct tcb tcbType;
tcbType tcbs[NUMTHREADS];
tcbType *RunPt;      // Pointer to currently running thread

int32_t Stacks[NUMTHREADS][STACKSIZE];

// Semaphore structure
struct sema{
  int32_t Value;     // Semaphore value
  tcbType *BlockedThreads; // Linked list of blocked threads
};
typedef struct sema semaType;

uint32_t Mail;      // Shared mailbox data
semaType SendSema;  // Semaphore for mailbox
uint32_t Lost;      // Counter for lost messages

// ******** OS_Init ************
// initialize operating system, disable interrupts until OS_Launch
// initialize OS controlled I/O: systick, 16 MHz clock
// input:  none
// output: none
void OS_Init(void){
  OS_DisableInterrupts();
  Clock_Init();                 // set processor clock to 16 MHz
  NVIC_ST_CTRL_R = 0;          // disable SysTick during setup
  NVIC_ST_CURRENT_R = 0;       // any write to current clears it
  NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0x00FFFFFF)|0xE0000000; // priority 7
  
  // Initialize mailbox semaphore
  SendSema.Value = 0;           // Mailbox initially empty
  SendSema.BlockedThreads = 0;  // No blocked threads
  Lost = 0;                     // No messages lost yet
}

// ******** SetInitialStack ************
// Initialize stack for a thread
void SetInitialStack(int i){
  tcbs[i].sp = &Stacks[i][STACKSIZE-16]; // thread stack pointer
  Stacks[i][STACKSIZE-1] = 0x01000000;   // thumb bit
  Stacks[i][STACKSIZE-3] = 0x14141414;   // R14
  Stacks[i][STACKSIZE-4] = 0x12121212;   // R12
  Stacks[i][STACKSIZE-5] = 0x03030303;   // R3
  Stacks[i][STACKSIZE-6] = 0x02020202;   // R2
  Stacks[i][STACKSIZE-7] = 0x01010101;   // R1
  Stacks[i][STACKSIZE-8] = 0x00000000;   // R0
  Stacks[i][STACKSIZE-9] = 0x11111111;   // R11
  Stacks[i][STACKSIZE-10] = 0x10101010;  // R10
  Stacks[i][STACKSIZE-11] = 0x09090909;  // R9
  Stacks[i][STACKSIZE-12] = 0x08080808;  // R8
  Stacks[i][STACKSIZE-13] = 0x07070707;  // R7
  Stacks[i][STACKSIZE-14] = 0x06060606;  // R6
  Stacks[i][STACKSIZE-15] = 0x05050505;  // R5
  Stacks[i][STACKSIZE-16] = 0x04040404;  // R4
}

// ******** OS_AddThreads ***************
// add three foreground threads to the scheduler
// Inputs: three pointers to a void/void foreground tasks
// Outputs: 1 if successful, 0 if this thread can not be added
int OS_AddThreads(void(*task0)(void),
                 void(*task1)(void),
                 void(*task2)(void)){ 
  int32_t status;
  status = StartCritical();
  
  // Initialize TCBs
  tcbs[0].next = &tcbs[1]; // 0 points to 1
  tcbs[1].next = &tcbs[2]; // 1 points to 2
  tcbs[2].next = &tcbs[0]; // 2 points to 0
  
  // Initialize blocking pointers
  tcbs[0].blocked = 0;
  tcbs[0].blockPt = 0;
  tcbs[0].sleep = 0;
  tcbs[1].blocked = 0;
  tcbs[1].blockPt = 0;
  tcbs[1].sleep = 0;
  tcbs[2].blocked = 0;
  tcbs[2].blockPt = 0;
  tcbs[2].sleep = 0;
  
  SetInitialStack(0); 
  Stacks[0][STACKSIZE-2] = (int32_t)(task0); // PC
  SetInitialStack(1); 
  Stacks[1][STACKSIZE-2] = (int32_t)(task1); // PC
  SetInitialStack(2); 
  Stacks[2][STACKSIZE-2] = (int32_t)(task2); // PC
  
  RunPt = &tcbs[0];       // thread 0 will run first
  EndCritical(status);
  return 1;               // successful
}

// ******** OS_Launch ***************
// start the scheduler, enable interrupts
// Inputs: number of 60ns clock cycles for each time slice
//         (maximum of 24 bits)
// Outputs: none (does not return)
void OS_Launch(uint32_t theTimeSlice){
  NVIC_ST_RELOAD_R = theTimeSlice - 1; // reload value
  NVIC_ST_CTRL_R = 0x00000007; // enable, core clock and interrupt arm
  StartOS();                   // start on the first task
}

// ******** Clock_Init ************
// Initialize the PLL to run at 16 MHz
void Clock_Init(void){
  SYSCTL_RCC_R |= 0x810;
  SYSCTL_RCC_R &= ~(0x400020);
}

// ******** Scheduler ************
// Select next thread to run
// This is called from SysTick_Handler in assembly
// Returns: pointer to next thread to run
tcbType* Scheduler(void){
  tcbType *pt;
  tcbType *nextPt;
  
  pt = RunPt;           // Current thread
  nextPt = pt->next;    // Start looking at next thread
  
  // Find next non-blocked thread
  while(nextPt->blockPt != 0){
    nextPt = nextPt->next;
    if(nextPt == pt){
      return pt;
    }
  }
  
  return nextPt;  // Return next runnable thread
}

// ******** OS_Suspend ************
// Suspend execution of current thread and run scheduler
// Used for cooperative multitasking
void OS_Suspend(void){
  // Trigger PendSV to perform context switch
  NVIC_INT_CTRL_R = NVIC_INT_CTRL_PENDSTSET;
}

// ******** OS_InitSemaphore ************
// Initialize counting semaphore
// Inputs: pointer to semaphore, initial value
void OS_InitSemaphore(semaType *semaPt, int32_t value){
  int32_t status;
  status = StartCritical();
  semaPt->Value = value;
  semaPt->BlockedThreads = 0;
  EndCritical(status);
}

// ******** OS_Wait ************
// Decrement semaphore, block if less than zero
// Uses blocking semaphore implementation
// Input: pointer to counting semaphore
void OS_Wait(semaType *semaPt){
  int32_t status;
  tcbType *pt;
  
  status = StartCritical();
  (semaPt->Value)--;
  
  if(semaPt->Value < 0){  // Block this thread
    RunPt->blockPt = (uint32_t*)semaPt;  // Mark thread as blocked on this semaphore
    
    // Add RunPt to semaphore's blocked list
    pt = semaPt->BlockedThreads;
    if(pt == 0){  // First blocked thread
      semaPt->BlockedThreads = RunPt;
      RunPt->blocked = 0;
    } else {  // Add to end of blocked list
      while(pt->blocked != 0){
        pt = pt->blocked;
      }
      pt->blocked = RunPt;
      RunPt->blocked = 0;
    }
    
    EndCritical(status);
    OS_Suspend();  // Run scheduler to switch threads
  } else {
    EndCritical(status);
  }
}

// ******** OS_Signal ************
// Increment semaphore, wake up one blocked thread if any
// Input: pointer to counting semaphore
void OS_Signal(semaType *semaPt){
  int32_t status;
  tcbType *pt;
  
  status = StartCritical();
  (semaPt->Value)++;
  
  if(semaPt->Value <= 0){  // Wake up one blocked thread
    pt = semaPt->BlockedThreads;
    if(pt != 0){  // There are blocked threads
      semaPt->BlockedThreads = pt->blocked;  // Remove from blocked list
      pt->blocked = 0;
      pt->blockPt = 0;  // Thread no longer blocked
    }
  }
  
  EndCritical(status);
}

// ******** OS_bWait ************
// Binary semaphore wait
// Input: pointer to binary semaphore (0 or 1)
void OS_bWait(uint32_t *s){
  int32_t status;
  status = StartCritical();
  while((*s) == 0){
    EndCritical(status);
    OS_Suspend();  // Cooperative: give up CPU
    status = StartCritical();
  }
  (*s) = 0;
  EndCritical(status);
}

// ******** OS_bSignal ************
// Binary semaphore signal
// Input: pointer to binary semaphore
void OS_bSignal(uint32_t *s){
  int32_t status;
  status = StartCritical();
  (*s) = 1;
  EndCritical(status);
}

// ******** SendMail ************
// Send data through mailbox
// Input: data to send
void SendMail(uint32_t data){
  Mail = data;
  if(SendSema.Value > 0){  // Mailbox already has data
    Lost++;
  } else {
    OS_Signal(&SendSema);
  }
}

// ******** RecvMail ************
// Receive data from mailbox (blocking)
// Output: data received
uint32_t RecvMail(void){
  OS_Wait(&SendSema);
  return Mail;
}

// For backward compatibility with existing code that uses simple semaphores
void OS_Wait_Simple(uint32_t *S){
  OS_DisableInterrupts();
  while((*S) == 0){
    OS_EnableInterrupts();
    OS_DisableInterrupts();
  }
  (*S) = (*S) - 1;
  OS_EnableInterrupts();
}

void OS_Signal_Simple(uint32_t *S){
  OS_DisableInterrupts();
  (*S) = (*S) + 1;
  OS_EnableInterrupts();
}