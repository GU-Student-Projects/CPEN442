// os.h
// Header file for Real-Time Operating System
// Color Show Project

#ifndef __OS_H
#define __OS_H

#include <stdint.h>

// Semaphore type
struct sema {
    int32_t value;
    void *blockedList;
};
typedef struct sema Sema4Type;

// Thread function type
typedef void (*ThreadFunc)(void);

// OS Core Functions
void OS_Init(void);
int OS_AddThread(void(*task)(void), uint32_t id, uint32_t stackSize);
void OS_Launch(uint32_t theTimeSlice);
void OS_Suspend(void);
void OS_Sleep(uint32_t sleepTime);
uint32_t OS_Time(void);

// Semaphore Functions
void OS_InitSemaphore(Sema4Type *semaPt, int32_t value);
void OS_Wait(Sema4Type *semaPt);
void OS_Signal(Sema4Type *semaPt);
void OS_bWait(Sema4Type *semaPt);
void OS_bSignal(Sema4Type *semaPt);

// Color FIFO Functions
void ColorFifo_Init(void);
uint32_t ColorFifo_Put(uint8_t color);
uint8_t ColorFifo_Get(void);
uint32_t ColorFifo_Size(void);
uint32_t ColorFifo_IsFull(void);
uint32_t ColorFifo_IsEmpty(void);

// Color definitions (3-bit RGB)
#define COLOR_BLACK   0x00  // 000
#define COLOR_RED     0x01  // 001
#define COLOR_BLUE    0x02  // 010
#define COLOR_GREEN   0x04  // 100
#define COLOR_CYAN    0x06  // 110 (Green + Blue)
#define COLOR_MAGENTA 0x03  // 011 (Red + Blue)
#define COLOR_YELLOW  0x05  // 101 (Red + Green)
#define COLOR_WHITE   0x07  // 111 (All colors)

#endif // __OS_H