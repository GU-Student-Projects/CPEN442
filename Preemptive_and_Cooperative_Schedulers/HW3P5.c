// HW3P5.c
// Test file for comparing cooperative and preemptive scheduling
// Runs on LM4F120/TM4C123

#include <stdint.h>

#define TIME_SLICE   32000

volatile uint32_t Count1;
volatile uint32_t Count2;
volatile uint32_t Count3;

void OS_Init(void);
void OS_AddThreads(void f1(void), void f2(void), void f3(void));
void OS_Launch(uint32_t);
// void OS_Suspend(void);

void Task1(void){
    Count1 = 0;
    for(;;){
        Count1++;
        if(Count1 == 0xFFFF){
            Count1 = 0;
        }
        // OS_Suspend();  
    }
}

void Task2(void){
    Count2 = 0;
    for(;;){
        Count2++;
        if(Count2 == 0xFFFF){
            Count2 = 0;
        }
        // OS_Suspend();
    }
}

void Task3(void){
    Count3 = 0;
    for(;;){
        Count3++;
        if(Count3 == 0xFFFF){
            Count3 = 0;
        }
        // OS_Suspend();
    }
}

int main(void){
    OS_Init();
    
    OS_AddThreads(Task1, Task2, Task3);
    
    OS_Launch(TIME_SLICE);
    
    return 0;
}