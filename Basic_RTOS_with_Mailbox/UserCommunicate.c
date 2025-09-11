#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"

#define TIMESLICE 32000  // 500 Hz switching (2ms per slice at 16MHz)

// Global variables
uint32_t Count1;
uint32_t Count2;
uint32_t Count3;
uint32_t Switches_in;   // Data read from switches
uint32_t Switches_out;  // Data to output

// External function declarations
void OS_Init(void);
void OS_AddThreads(void f1(void), void f2(void), void f3(void));
void OS_Launch(uint32_t);
void SendMail(uint32_t data);
uint32_t RecvMail(void);

void Task1(void){
  Count1 = 0;
  for(;;){
    Count1++;
    GPIO_PORTF_DATA_R &= ~0x0E;  // Clear PF3-1
    
    if (Count1 == 750){  // Periodically read switches and send
      Switches_in = (GPIO_PORTD_DATA_R & 0x0E);  // Read PD3-1
      SendMail(Switches_in);  // Send to mailbox
      Count1 = 0;
    }
  }
}

void Task2(void){
  Count2 = 0;
  for(;;){
    Count2++;
    Switches_out = RecvMail();  // Block waiting for data
    GPIO_PORTF_DATA_R |= Switches_out;   // Set corresponding bits
    GPIO_PORTF_DATA_R &= Switches_out;   // Clear other bits
  }
}

void Task3(void){
  Count3 = 0;
  for(;;){
    Count3++;
    if (Count3 == 0xFFFF){
      Count3 = 0;
    }
    // Task3 can remain idle or do other work
  }
}

int main(void){
  OS_Init();           
  SYSCTL_RCGCGPIO_R |= 0x28;            
  while((SYSCTL_RCGCGPIO_R&0x28) == 0){} 
  
  GPIO_PORTD_DIR_R &= ~0x0E;   // PD3-1 input (switches)
  GPIO_PORTD_DEN_R |= 0x0E;    
  GPIO_PORTF_DIR_R |= 0x0E;    // PF3-1 output (LEDs)
  GPIO_PORTF_DEN_R |= 0x0E;    
    
  OS_AddThreads(&Task1, &Task2, &Task3);
  OS_Launch(TIMESLICE); 
  return 0;             
}