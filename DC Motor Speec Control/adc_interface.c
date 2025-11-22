// adc_interface.c
// ADS7806 12-bit ADC Serial Interface
// Uses periodic Timer interrupt at 100µs (10kHz sampling rate)
// Accumulates 100 samples every 10ms and computes average
//
// Pin connections:
// PB6 - R/C (Read/Convert control)
// PB7 - BUSY (conversion status input)
// PB4 - SDATA (serial data input from ADC)
// PB5 - DATACLK (data clock output to ADC, if using external clock mode)
//
// ADS7806 Configuration:
// - Using internal data clock mode (EXT/INT tied LOW)
// - ±10V input range
// - R/C pulse initiates conversion and serial data output

#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"
#include <stdint.h>

#include "system.h" 

// External semaphore (correctly NOT static - used by main.c)
extern int32_t ADC_Data_Ready;
extern void OS_Signal(int32_t *s);

// ADC Configuration
#define ADC_SAMPLES_PER_AVERAGE 100    // 100 samples at 10kHz = 10ms averaging

// Pin definitions for ADS7806
#define R_C_PIN         (1 << 6)       // PB6
#define BUSY_PIN        (1 << 7)       // PB7
#define SDATA_PIN       (1 << 4)       // PB4

static volatile int32_t ADC_Sample_Buffer[ADC_SAMPLES_PER_AVERAGE];
static volatile uint32_t Sample_Index = 0;
static volatile int32_t Average_Voltage_mV = 0;
static volatile uint8_t Average_Ready_Flag = 0;

static void Timer0A_Init(void);
static void PortB_ADC_Init(void);
static uint16_t ADC_Read_Serial(void);
static int32_t ADC_12bit_to_mV(uint16_t adc_value);

void Timer0A_Handler(void);
void PortB_ADC_Init(void);
uint16_t ADC_Read_Serial(void);
int32_t ADC_12bit_to_mV(uint16_t adc_value);

//******** ADC_Init ************
// Initialize ADS7806 interface
// Configure GPIO pins and Timer0A for 100µs periodic interrupt
void ADC_Init(void){
    // Initialize GPIO Port B for ADC interface
    PortB_ADC_Init();
    
    // Initialize Timer0A for 100µs periodic interrupts
    Timer0A_Init();
    
    // Clear sample buffer
    Sample_Index = 0;
    Average_Voltage_mV = 0;
    Average_Ready_Flag = 0;
}

//******** PortB_ADC_Init ************
// Configure Port B pins for ADS7806 interface
void PortB_ADC_Init(void){
    // Enable Port B clock
    SYSCTL_RCGCGPIO_R |= 0x02;
    while((SYSCTL_PRGPIO_R & 0x02) == 0){};  // Wait for Port B ready
    
    // Unlock Port B (should not be needed for PB4-7, but safe)
    GPIO_PORTB_LOCK_R = 0x4C4F434B;
    GPIO_PORTB_CR_R |= (R_C_PIN | BUSY_PIN | SDATA_PIN);
    
    // Configure pin directions
    GPIO_PORTB_DIR_R |= R_C_PIN;           // PB6 R/C output
    GPIO_PORTB_DIR_R &= ~(BUSY_PIN | SDATA_PIN); // PB7 BUSY, PB4 SDATA inputs
    
    // Enable digital function
    GPIO_PORTB_DEN_R |= (R_C_PIN | BUSY_PIN | SDATA_PIN);
    
    // Disable analog function
    GPIO_PORTB_AMSEL_R &= ~(R_C_PIN | BUSY_PIN | SDATA_PIN);
    
    // No pull-up/pull-down needed for R/C (output)
    // Enable pull-down for BUSY and SDATA
    GPIO_PORTB_PUR_R &= ~(BUSY_PIN | SDATA_PIN);
    GPIO_PORTB_PDR_R |= (BUSY_PIN | SDATA_PIN);
    
    // Initialize R/C HIGH (idle state)
    GPIO_PORTB_DATA_R |= R_C_PIN;
}

//******** Timer0A_Init ************
// Initialize Timer0A for 100µs periodic interrupts (10kHz)
// 16 MHz clock, 100µs = 1600 cycles
void Timer0A_Init(void){
    // Enable Timer0 clock
    SYSCTL_RCGCTIMER_R |= 0x01;
    while((SYSCTL_PRTIMER_R & 0x01) == 0){}; // Wait for Timer0 ready
    
    // Disable timer during setup
    TIMER0_CTL_R &= ~0x01;
    
    // Configure for 32-bit periodic mode
    TIMER0_CFG_R = 0x00;           // 32-bit mode
    TIMER0_TAMR_R = 0x02;          // Periodic mode
    
    // Set reload value for 100µs
    // 100µs * 16 MHz = 1600 cycles
    TIMER0_TAILR_R = 1600 - 1;
    
    // Clear timeout flag
    TIMER0_ICR_R = 0x01;
    
    // Enable timeout interrupt
    TIMER0_IMR_R |= 0x01;
    
    // Set interrupt priority (lower than SysTick priority 7)
    NVIC_PRI4_R = (NVIC_PRI4_R & 0x00FFFFFF) | 0x40000000; // Priority 2
    
    // Enable Timer0A interrupt in NVIC (interrupt 19)
    NVIC_EN0_R |= (1 << 19);
}

//******** ADC_Start_Sampling ************
// Enable Timer0A to start 100µs periodic sampling
void ADC_Start_Sampling(void){
    TIMER0_CTL_R |= 0x01;  // Enable Timer0A
}

//******** ADC_Read_Serial ************
// Read 12-bit data from ADS7806 using internal data clock
// Waits for BUSY to go HIGH indicating conversion complete
// Returns: 12-bit ADC value (0x000 to 0xFFF)
uint16_t ADC_Read_Serial(void){
    uint16_t adc_value = 0;
    uint8_t bit_count;
    uint32_t timeout;
    
    // Trigger conversion: R/C LOW for at least 40ns
    GPIO_PORTB_DATA_R &= ~R_C_PIN;
    // Short delay (at 16MHz, a few NOPs = 62.5ns each)
    __asm("NOP");
    __asm("NOP");
    GPIO_PORTB_DATA_R |= R_C_PIN;
    
    // Wait for BUSY to go HIGH (conversion complete)
    // Typical conversion time ~15µs, timeout at 30µs
    timeout = 500; // Conservative timeout
    while(!(GPIO_PORTB_DATA_R & BUSY_PIN) && timeout > 0){
        timeout--;
    }
    
    if(timeout == 0){
        return 0; // Timeout error
    }
    
    // Read 12 bits serially
    // Internal clock mode: data clocked out automatically after conversion
    // Data valid on both rising and falling edges
    // We'll sample on each bit period
    
    for(bit_count = 0; bit_count < 12; bit_count++){
        adc_value <<= 1;
        
        // Small delay to allow data to settle
        __asm("NOP");
        __asm("NOP");
        __asm("NOP");
        __asm("NOP");
        
        // Read SDATA bit
        if(GPIO_PORTB_DATA_R & SDATA_PIN){
            adc_value |= 0x01;
        }
        
        // Delay for next bit (internal clock ~900kHz, ~1.1µs period)
        // At 16MHz: 1.1µs = ~18 cycles
        for(int i = 0; i < 3; i++){
            __asm("NOP");
        }
    }
    
    return adc_value;
}

//******** ADC_12bit_to_mV ************
// Convert 12-bit ADC value to millivolts for ±10V range
// ADS7806 in Binary Two's Complement format (±10V range)
// Input: 12-bit value (0x000 to 0xFFF)
// Output: Voltage in mV (-10000 to +9995 mV)
int32_t ADC_12bit_to_mV(uint16_t adc_value){
    int32_t voltage_mV;
    
    // Check if value is negative (MSB = 1 in two's complement)
    if(adc_value & 0x800){
        // Negative value: convert from 12-bit two's complement
        // Sign extend to 32-bit
        voltage_mV = adc_value | 0xFFFFF000;
    }
    else{
        // Positive value
        voltage_mV = adc_value;
    }
    
    // Scale to millivolts
    // Full scale = 20V = 20000mV for 4096 codes
    // 1 LSB = 20000mV / 4096 ≈ 4.88 mV
    voltage_mV = (voltage_mV * 20000) / 4096;
    
    return voltage_mV;
}

//******** ADC_Get_Average_Voltage ************
// Return the most recent averaged voltage in millivolts
// Should be called after ADC_Average_Ready() returns true
int32_t ADC_Get_Average_Voltage(void){
    return Average_Voltage_mV;
}

//******** ADC_Average_Ready ************
// Check if new averaged voltage data is available
uint8_t ADC_Average_Ready(void){
    return Average_Ready_Flag;
}

//******** Timer0A_Handler ************
// ISR for Timer0A - executes every 100µs
// Samples ADC, accumulates 100 samples, computes average every 10ms
void Timer0A_Handler(void){
    uint16_t adc_raw;
    int32_t voltage_sample;
    int64_t sum;
    uint32_t i;
    
    // Clear interrupt flag
    TIMER0_ICR_R = 0x01;
    
    // Read ADC value
    adc_raw = ADC_Read_Serial();
    
    // Convert to millivolts
    voltage_sample = ADC_12bit_to_mV(adc_raw);
    
    // Store in buffer
    ADC_Sample_Buffer[Sample_Index] = voltage_sample;
    Sample_Index++;
    
    // Check if 100 samples collected (10ms elapsed)
    if(Sample_Index >= ADC_SAMPLES_PER_AVERAGE){
        // Compute average
        sum = 0;
        for(i = 0; i < ADC_SAMPLES_PER_AVERAGE; i++){
            sum += ADC_Sample_Buffer[i];
        }
        Average_Voltage_mV = (int32_t)(sum / ADC_SAMPLES_PER_AVERAGE);
        
        // Reset sample index
        Sample_Index = 0;
        
        // Set flag and signal semaphore
        Average_Ready_Flag = 1;
        OS_Signal(&ADC_Data_Ready);
    }
}