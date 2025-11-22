// pwm_control.c
// PWM Generation for DC Motor Control
// Uses M1PWM6 module on PF2
// PWM Frequency: 100 Hz (10ms period)
// Duty Cycle Range: 18% - 99.5% (constrained by motor specifications)
// Direction Control: PB0, PB1
//
// Motor Driver U1 connections:
// PF2 - PWM signal input
// PB1, PB0 - Direction control (01 = forward, 10 = reverse)

#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"
#include <stdint.h>

#include "system.h" 

// Direction definitions
#define MOTOR_FORWARD  0x02    // PB1=1, PB0=0
#define MOTOR_REVERSE  0x01    // PB1=0, PB0=1
#define MOTOR_BRAKE    0x00    // PB1=0, PB0=0

// PWM limits (in tenths of percent for precision)
#define PWM_DUTY_MIN   180     // 18.0%
#define PWM_DUTY_MAX   995     // 99.5%

// Current duty cycle (in tenths of percent)
static volatile uint16_t Current_Duty_Cycle = 0;

// Function prototypes
static void PWM_Module1_Init(void);
static void PWM_PF2_Init(void);
static void Direction_Pins_Init(void);

//******** PWM_Init ************
// Initialize PWM Module 1, Generator 3 for M1PWM6 output on PF2
// Configure for 100 Hz frequency
// System clock: 16 MHz
void PWM_Init(void){
    // Initialize direction control pins
    Direction_Pins_Init();
    
    // Initialize PF2 for PWM output
    PWM_PF2_Init();
    
    // Initialize PWM Module 1
    PWM_Module1_Init();
    
    // Set initial duty cycle to minimum
    PWM_SetDutyCycle(PWM_DUTY_MIN);
}

//******** Direction_Pins_Init ************
// Initialize PB0 and PB1 for motor direction control
void Direction_Pins_Init(void){
    // Enable Port B clock (may already be enabled by ADC module)
    SYSCTL_RCGCGPIO_R |= 0x02;
    while((SYSCTL_PRGPIO_R & 0x02) == 0){};
    
    // Configure PB0 and PB1 as outputs
    GPIO_PORTB_DIR_R |= 0x03;      // PB1, PB0 outputs
    GPIO_PORTB_DEN_R |= 0x03;      // Digital enable
    GPIO_PORTB_AMSEL_R &= ~0x03;   // Disable analog
    
    // Initialize to brake (both LOW)
    GPIO_PORTB_DATA_R &= ~0x03;
}

//******** PWM_PF2_Init ************
// Configure PF2 as M1PWM6 output
void PWM_PF2_Init(void){
    // Enable Port F clock
    SYSCTL_RCGCGPIO_R |= 0x20;
    while((SYSCTL_PRGPIO_R & 0x20) == 0){};
    
    // Unlock PF2 (not needed for PF2, but safe practice)
    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= 0x04;
    
    // Configure PF2 for PWM alternate function
    GPIO_PORTF_AFSEL_R |= 0x04;    // Enable alt function on PF2
    GPIO_PORTF_PCTL_R = (GPIO_PORTF_PCTL_R & 0xFFFFF0FF) | 0x00000500; // M1PWM6
    GPIO_PORTF_DEN_R |= 0x04;      // Digital enable PF2
    GPIO_PORTF_AMSEL_R &= ~0x04;   // Disable analog on PF2
}

//******** PWM_Module1_Init ************
// Initialize PWM Module 1, Generator 3 for 100 Hz output
// System Clock: 16 MHz
// PWM Clock: 16 MHz / 2 = 8 MHz (using /2 divider for finer resolution)
// 100 Hz: Period = 10ms = 80,000 cycles at 8 MHz
void PWM_Module1_Init(void){
    // Enable PWM Module 1 clock
    SYSCTL_RCGCPWM_R |= 0x02;
    while((SYSCTL_PRPWM_R & 0x02) == 0){};
    
    // Configure PWM clock divider
    // Use /2 divider for better resolution: 16MHz / 2 = 8MHz PWM clock
    SYSCTL_RCC_R |= SYSCTL_RCC_USEPWMDIV;
    SYSCTL_RCC_R = (SYSCTL_RCC_R & ~SYSCTL_RCC_PWMDIV_M) | SYSCTL_RCC_PWMDIV_2;
    
    // Disable PWM Generator 3 during configuration
    PWM1_3_CTL_R = 0;
    
    // Configure Generator 3 for count-down mode
    PWM1_3_GENA_R = 0x0000008C;    // Drive PWM6 (M1PWM6) High on LOAD, Low on CMPA down
    
    // Set PWM period for 100 Hz
    // PWM Clock = 8 MHz
    // Period = 8,000,000 / 100 = 80,000 cycles
    PWM1_3_LOAD_R = 80000 - 1;
    
    // Set initial duty cycle to 18% (minimum)
    // 18% of 80000 = 14,400
    PWM1_3_CMPA_R = 14400;
    Current_Duty_Cycle = PWM_DUTY_MIN;
    
    // Enable PWM Generator 3
    PWM1_3_CTL_R = 0x01;
    
    // Enable PWM6 output (bit 6 in ENABLE register)
    PWM1_ENABLE_R |= 0x40;
}

//******** PWM_SetDutyCycle ************
// Set PWM duty cycle with safety limits
// Input: duty_percent_x10 = duty cycle in tenths of percent (180 to 995)
//        Example: 185 = 18.5%, 500 = 50.0%, 995 = 99.5%
void PWM_SetDutyCycle(uint16_t duty_percent_x10){
    uint32_t compare_value;
    
    // Apply safety limits
    if(duty_percent_x10 < PWM_DUTY_MIN){
        duty_percent_x10 = PWM_DUTY_MIN;
    }
    else if(duty_percent_x10 > PWM_DUTY_MAX){
        duty_percent_x10 = PWM_DUTY_MAX;
    }
    
    // Calculate compare value
    // LOAD = 80000 (for 100 Hz at 8 MHz PWM clock)
    // Duty cycle = (LOAD - CMPA) / LOAD
    // Therefore: CMPA = LOAD - (LOAD * duty / 1000)
    // Using integer math: CMPA = LOAD - (LOAD * duty_percent_x10 / 10000)
    
    compare_value = 80000 - ((80000UL * duty_percent_x10) / 10000);
    
    // Update compare register
    PWM1_3_CMPA_R = compare_value;
    
    // Store current duty cycle
    Current_Duty_Cycle = duty_percent_x10;
}

//******** PWM_GetDutyCycle ************
// Get current PWM duty cycle
// Returns: Duty cycle in tenths of percent
uint16_t PWM_GetDutyCycle(void){
    return Current_Duty_Cycle;
}

//******** PWM_SetDirection ************
// Set motor direction
// Input: forward = 1 for forward, 0 for reverse
void PWM_SetDirection(uint8_t forward){
    if(forward){
        GPIO_PORTB_DATA_R = (GPIO_PORTB_DATA_R & ~0x03) | MOTOR_FORWARD;
    }
    else{
        GPIO_PORTB_DATA_R = (GPIO_PORTB_DATA_R & ~0x03) | MOTOR_REVERSE;
    }
}

//******** PWM_Brake ************
// Apply motor brake by setting both direction pins LOW
void PWM_Brake(void){
    GPIO_PORTB_DATA_R &= ~0x03;
}

//******** PWM_Stop ************
// Stop PWM output (set duty cycle to 0)
void PWM_Stop(void){
    PWM1_3_CMPA_R = 80000;  // 0% duty cycle
    Current_Duty_Cycle = 0;
}