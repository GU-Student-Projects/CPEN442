// main.c
#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"
#include <stdint.h>

#include "system.h" 

// Global variables
volatile uint16_t Target_RPM = 0;           // Target speed in RPM
volatile int32_t Current_RPM = 0;           // Current measured speed in RPM

// Local
static uint32_t Current_RPM_Accumulator = 0; // For 1-second averaging
static uint16_t Current_RPM_Count = 0;    // Count for averaging (100 samples per second)

// Semaphores
int32_t LCD_Mutex;                          // Protects LCD access
int32_t ADC_Data_Ready;                     // Signals when new averaged voltage available
int32_t New_Target_Speed;                   // Signals when new target speed entered

// Keypad input buffer
uint8_t Keypad_Buffer[5];                   // 4 digits + null terminator
uint8_t Keypad_Index = 0;

// Function prototypes for threads
void Keypad_Thread(void);
void Controller_LCD_Thread(void);

//******** Keypad_Thread ************
// Handles keypad input for target speed
// Accepts 4-digit decimal numbers
// '#' applies the speed, 'C' clears entry
// Valid range: 0 or 400-2400 RPM
void Keypad_Thread(void){
    uint8_t key;
    uint16_t raw_value;
    
    while(1){
        // Scan for keypress
        Scan_Keypad();
        key = Key_ASCII;
        
        if(key != 0){
            // Key was pressed
            if(key >= '0' && key <= '9'){
                // Valid digit
                if(Keypad_Index < 4){
                    Keypad_Buffer[Keypad_Index] = key;
                    Keypad_Index++;
                    
                    // Display on LCD Line 1
                    OS_Wait(&LCD_Mutex);
                    LCD_GoTo(0, 10 + Keypad_Index - 1); // Position after "Input RPM: "
                    LCD_OutChar(key);
                    OS_Signal(&LCD_Mutex);
                }
                
                // Auto-apply if 4 digits entered
                if(Keypad_Index == 4){
                    Keypad_Buffer[4] = '\0';
                    raw_value = ASCII2Hex(Keypad_Buffer);
                    
                    // Apply range constraints
                    if(raw_value > 2400){
                        Target_RPM = 2400;
                    }
                    else if(raw_value > 0 && raw_value < 400){
                        Target_RPM = 400;
                    }
                    else{
                        Target_RPM = raw_value;
                    }
                    
                    // Clear input display
                    Keypad_Index = 0;
                    OS_Wait(&LCD_Mutex);
                    LCD_GoTo(0, 10);
                    LCD_OutString("    "); // Clear 4 digits
                    OS_Signal(&LCD_Mutex);
                    
                    // Signal new target speed
                    OS_Signal(&New_Target_Speed);
                }
            }
            else if(key == '#'){
                // Apply current entry (if less than 4 digits)
                if(Keypad_Index > 0){
                    Keypad_Buffer[Keypad_Index] = '\0';
                    raw_value = ASCII2Hex(Keypad_Buffer);
                    
                    // Apply range constraints
                    if(raw_value > 2400){
                        Target_RPM = 2400;
                    }
                    else if(raw_value > 0 && raw_value < 400){
                        Target_RPM = 400;
                    }
                    else{
                        Target_RPM = raw_value;
                    }
                    
                    // Clear input display
                    Keypad_Index = 0;
                    OS_Wait(&LCD_Mutex);
                    LCD_GoTo(0, 10);
                    LCD_OutString("    ");
                    OS_Signal(&LCD_Mutex);
                    
                    // Signal new target speed
                    OS_Signal(&New_Target_Speed);
                }
            }
            else if(key == 'C'){
                // Clear current entry
                Keypad_Index = 0;
                OS_Wait(&LCD_Mutex);
                LCD_GoTo(0, 10);
                LCD_OutString("    ");
                OS_Signal(&LCD_Mutex);
            }
            // Ignore 'A', 'B', 'D', '*'
            
            // Debounce delay
            OS_Sleep(100); // 200ms delay (100 * 2ms timeslice)
        }
        
        OS_Sleep(5); // 10ms scan rate
    }
}

//******** Controller_LCD_Thread ************
// Runs controller every 10ms when new ADC data available
// Updates LCD display every 1 second with averaged current speed
void Controller_LCD_Thread(void){
    int32_t avg_voltage;
    int32_t current_rpm_instant;
    uint32_t display_counter = 0;
    uint8_t ascii_buffer[6];
    
    // Initialize LCD
    OS_Wait(&LCD_Mutex);
    LCD_Init();
    LCD_Clear();
    LCD_GoTo(0, 0);
    LCD_OutString("Input RPM:");
    LCD_GoTo(1, 0);
    LCD_OutString("T:0000 C:0000");
    OS_Signal(&LCD_Mutex);
    
    while(1){
        // Wait for new averaged voltage data (signals every 10ms)
        OS_Wait(&ADC_Data_Ready);
        
        // Get averaged voltage in millivolts
        avg_voltage = ADC_Get_Average_Voltage();
        
        // Convert to RPM
        current_rpm_instant = Current_speed(avg_voltage);
        Current_RPM = current_rpm_instant;
        
        // Accumulate for 1-second average display
        Current_RPM_Accumulator += current_rpm_instant;
        Current_RPM_Count++;
        
        // Update controller
        Controller_Update(Target_RPM, current_rpm_instant);
        
        // Update LCD every 1 second (100 controller cycles)
        display_counter++;
        if(display_counter >= 100){
            display_counter = 0;
            
            // Compute 1-second average
            int32_t avg_display_rpm = Current_RPM_Accumulator / Current_RPM_Count;
            Current_RPM_Accumulator = 0;
            Current_RPM_Count = 0;
            
            // Update LCD Line 2 with target and current speeds
            OS_Wait(&LCD_Mutex);
            
            // Display Target Speed
            Hex2ASCII(ascii_buffer, Target_RPM);
            LCD_GoTo(1, 2);
            // Display 4 digits
            LCD_OutChar(ascii_buffer[0]);
            LCD_OutChar(ascii_buffer[1]);
            LCD_OutChar(ascii_buffer[2]);
            LCD_OutChar(ascii_buffer[3]);
            
            // Display Current Speed
            Hex2ASCII(ascii_buffer, (uint16_t)avg_display_rpm);
            LCD_GoTo(1, 9);
            LCD_OutChar(ascii_buffer[0]);
            LCD_OutChar(ascii_buffer[1]);
            LCD_OutChar(ascii_buffer[2]);
            LCD_OutChar(ascii_buffer[3]);
            
            OS_Signal(&LCD_Mutex);
        }
    }
}

//******** main ************
int main(void){
    // Initialize OS
    OS_Init();
    
    // Initialize semaphores
    OS_InitSemaphore(&LCD_Mutex, 1);        // Binary semaphore (mutex)
    OS_InitSemaphore(&ADC_Data_Ready, 0);   // Counting semaphore
    OS_InitSemaphore(&New_Target_Speed, 0); // Counting semaphore
    
    // Initialize peripherals
    ADC_Init();
    PWM_Init();
    Controller_Init();
    
    // Set initial motor direction (forward)
    PWM_SetDirection(1);
    
    // Add threads to RTOS
    OS_AddThreads(&Keypad_Thread, &Controller_LCD_Thread);
    
    // Start ADC sampling (100µs periodic interrupt)
    ADC_Start_Sampling();
    
    // Launch RTOS with 2ms timeslice
    // 2ms = 2000µs = 2000 * 16 cycles = 32,000 cycles
    OS_Launch(32000);
    
    // Should never reach here
    while(1);
}