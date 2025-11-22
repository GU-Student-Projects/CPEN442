#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

//******** Configuration Constants ************

// System Clock
#define SYSTEM_CLOCK_HZ     16000000    // 16 MHz

// RTOS Configuration
#define RTOS_TIMESLICE_US   2000        // 2ms timeslice
#define RTOS_TIMESLICE_CYCLES (RTOS_TIMESLICE_US * (SYSTEM_CLOCK_HZ / 1000000))

// ADC Sampling Configuration
#define ADC_SAMPLE_PERIOD_US    100     // 100µs sampling period
#define ADC_SAMPLE_RATE_HZ      10000   // 10 kHz sampling rate
#define ADC_SAMPLES_PER_AVG     100     // Average over 100 samples (10ms)

// PWM Configuration
#define PWM_FREQUENCY_HZ    100         // 100 Hz PWM frequency
#define PWM_PERIOD_MS       10          // 10ms period

// Motor Speed Limits (RPM)
#define MOTOR_SPEED_MIN     400         // Minimum non-zero speed
#define MOTOR_SPEED_MAX     2400        // Maximum speed
#define MOTOR_SPEED_OFF     0           // Motor off

// Controller Configuration
#define CONTROLLER_UPDATE_RATE_HZ   100     // 100 Hz (10ms updates)
#define CONTROLLER_TARGET_ERROR     15      // ±15 RPM target error

// Display Configuration
#define LCD_UPDATE_RATE_HZ      1           // 1 Hz (1 second updates)
#define LCD_ROWS                2           // 2-line LCD
#define LCD_COLS                16          // 16 characters per line

// Keypad Configuration
#define KEYPAD_MAX_DIGITS       4           // 4-digit input
#define KEYPAD_SCAN_RATE_HZ     100         // 100 Hz scan rate
#define KEYPAD_DEBOUNCE_MS      200         // 200ms debounce


//******** ADC Interface (adc_interface.c) ************

// Initialize ADS7806 interface and Timer0A
void ADC_Init(void);

// Start periodic ADC sampling
void ADC_Start_Sampling(void);

// Get averaged voltage in millivolts
int32_t ADC_Get_Average_Voltage(void);

// Check if new average is ready
uint8_t ADC_Average_Ready(void);


//******** PWM Control (pwm_control.c) ************

// Initialize PWM Module 1 for M1PWM6 output
void PWM_Init(void);

// Set PWM duty cycle (input in tenths of percent: 180-995)
void PWM_SetDutyCycle(uint16_t duty_percent_x10);

// Get current PWM duty cycle
uint16_t PWM_GetDutyCycle(void);

// Set motor direction (1=forward, 0=reverse)
void PWM_SetDirection(uint8_t forward);

// Apply motor brake
void PWM_Brake(void);

// Stop PWM output
void PWM_Stop(void);


//******** Controller (controller.c) ************

// Initialize controller state
void Controller_Init(void);

// Update controller (called every 10ms)
void Controller_Update(int32_t target_rpm, int32_t current_rpm);

// Get current error value (for debugging)
int32_t Controller_GetError(void);

// Get integral term (for debugging)
int32_t Controller_GetIntegral(void);

// Get derivative term (for debugging)
int32_t Controller_GetDerivative(void);

// Reset integral term
void Controller_ResetIntegral(void);

// Get controller statistics
uint32_t Controller_GetStatistics(void);


//******** RTOS Functions (os_v2.c) ************

// Initialize operating system
void OS_Init(void);

// Add threads to scheduler
int OS_AddThreads(void(*task0)(void), void(*task1)(void));

// Launch RTOS with specified timeslice
void OS_Launch(uint32_t theTimeSlice);

// Initialize semaphore
void OS_InitSemaphore(int32_t *Sem, int32_t val);

// Wait on semaphore (blocking)
void OS_Wait(int32_t *s);

// Signal semaphore
void OS_Signal(int32_t *s);

// Sleep for specified time slices
void OS_Sleep(uint32_t SleepCtr);

// Suspend current thread
void OS_Suspend(void);


//******** Keypad Functions (Keypad.s) ************

// Scan keypad and store ASCII value in Key_ASCII
void Scan_Keypad(void);

// Global variable for keypress result
extern uint8_t Key_ASCII;


//******** LCD Functions (LCD.s) ************

// Initialize LCD
void LCD_Init(void);

// Clear LCD display
void LCD_Clear(void);

// Move cursor to row (0-1) and column (0-15)
void LCD_GoTo(uint8_t row, uint8_t col);

// Output single character
void LCD_OutChar(char data);

// Output null-terminated string
void LCD_OutString(char *pt);


//******** ASCII Conversion Functions (ASCII_Conversions.s) ************

// Convert ASCII digit array to hexadecimal
uint16_t ASCII2Hex(uint8_t *ascii_array);

// Convert hexadecimal to ASCII digit array
void Hex2ASCII(uint8_t *ascii_array, uint16_t hex_value);


//******** Voltage to RPM Conversion (Voltage2RPM.c) ************

// Convert motor voltage (mV) to RPM
int32_t Current_speed(int32_t Avg_volt);


//******** Pin Assignments ************

/*
 * PORT A:
 *   PA2-PA5: Reserved for potential SSI0 use
 * 
 * PORT B:
 *   PB0: Motor Direction 0
 *   PB1: Motor Direction 1
 *   PB4: SDATA (serial data input from ADS7806)
 *   PB5: DATACLK (optional, if using external clock mode)
 *   PB6: R/C (Read/Convert control to ADS7806)
 *   PB7: BUSY (conversion status from ADS7806)
 * 
 * PORT C:
 *   PC4-PC7: Keypad columns (assumed from Keypad.s)
 * 
 * PORT D:
 *   PD0-PD3: LCD data (assumed from LCD.s)
 * 
 * PORT E:
 *   PE0-PE3: Keypad rows (assumed from Keypad.s)
 * 
 * PORT F:
 *   PF2: M1PWM6 output (PWM to motor driver)
 */


//******** Global Variables (main.c) ************

// Target and current speeds
extern volatile uint16_t Target_RPM;
extern volatile int32_t Current_RPM;

// Semaphores
extern int32_t LCD_Mutex;
extern int32_t ADC_Data_Ready;
extern int32_t New_Target_Speed;


//******** Utility Macros ************

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, min, max) (MAX(min, MIN(x, max)))

// Convert milliseconds to RTOS sleep ticks
#define MS_TO_TICKS(ms) ((ms) / 2)  // 2ms per tick

// Convert microseconds to clock cycles
#define US_TO_CYCLES(us) ((us) * (SYSTEM_CLOCK_HZ / 1000000))


#endif // SYSTEM_H