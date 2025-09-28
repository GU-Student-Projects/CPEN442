#include <stdint.h>
#include "os.h"
#include "tm4c123gh6pm.h"


// Global variables
volatile uint8_t CurrentColor;      // Color being displayed
volatile uint8_t NextColor;         // Next color in queue
volatile uint8_t FormedColor;       // Color formed by switches
volatile uint32_t ColorTimer;       // 15-second timer
volatile uint8_t BufferFull;        // Flag for full buffer

// Semaphores
Sema4Type LCDmutex;                // LCD mutual exclusion
Sema4Type ColorUpdate;             // Signal for color update
Sema4Type ButtonPressed;           // Signal for button press

// External LCD functions
extern void Init_LCD_Ports(void);
extern void Init_LCD(void);
extern void Set_Position(uint32_t pos);
extern void Display_Msg(char *str);
extern void Display_Char(char c);

// Initialize GPIO ports
void GPIO_Init(void) {
    SYSCTL_RCGCGPIO_R |= 0x38;    // Enable Ports D, E, F
    while((SYSCTL_PRGPIO_R & 0x38) == 0) {}; // Wait
    
    // Port D for switches (inputs with pull-ups)
    GPIO_PORTD_DIR_R &= ~0x0F;     // PD0-3 are inputs
    GPIO_PORTD_DEN_R |= 0x0F;      // Digital enable
    GPIO_PORTD_PUR_R |= 0x0F;      // Pull-up resistors
    
    // Port F for RGB LED (outputs)
    GPIO_PORTF_LOCK_R = 0x4C4F434B;  // Unlock Port F
    GPIO_PORTF_CR_R = 0x0E;          // Allow changes to PF1-3
    GPIO_PORTF_DIR_R |= 0x0E;        // PF1-3 are outputs
    GPIO_PORTF_DEN_R |= 0x0E;        // Digital enable
    GPIO_PORTF_DATA_R &= ~0x0E;      // LEDs off initially
}

// Read switch states and form color
uint8_t ReadSwitches(void) {
    uint32_t switches = ~GPIO_PORTD_DATA_R;  // Active low switches
    uint8_t color = 0;
    
    if(switches & 0x08) color |= COLOR_GREEN;  // SW2 - PD3
    if(switches & 0x04) color |= COLOR_BLUE;   // SW3 - PD2
    if(switches & 0x02) color |= COLOR_RED;    // SW4 - PD1
    
    return color;
}

// Set LED color
void SetLED(uint8_t color) {
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~0x0E) | ((color & 0x07) << 1);
}

// Get color name string
const char* GetColorName(uint8_t color) {
    switch(color) {
        case COLOR_BLACK:   return "Blk";
        case COLOR_RED:     return "Red";
        case COLOR_BLUE:    return "Blu";
        case COLOR_GREEN:   return "Grn";
        case COLOR_CYAN:    return "Cyn";
        case COLOR_MAGENTA: return "Mag";
        case COLOR_YELLOW:  return "Yel";
        case COLOR_WHITE:   return "Wht";
        default:            return "???";
    }
}

// Update first line of LCD
void UpdateLCDLine1(void) {
    OS_Wait(&LCDmutex);
    Set_Position(0x00);  // First line, leftmost position
    
    if(BufferFull) {
        Display_Msg("  Buffer Full!  ");
    } else {
        Display_Msg("Switches: ");
        Display_Msg((char*)GetColorName(FormedColor));
        Display_Msg("    ");  // Clear rest of line
    }
    
    OS_Signal(&LCDmutex);
}

// Update second line of LCD
void UpdateLCDLine2(void) {
    OS_Wait(&LCDmutex);
    Set_Position(0x40);  // Second line
    
    if(ColorFifo_IsEmpty()) {
        Display_Msg("Input a Color!  ");
    } else {
        Display_Msg("C:");
        Display_Msg((char*)GetColorName(CurrentColor));
        Display_Msg(" N:");
        if(ColorFifo_Size() > 0) {
            Display_Msg((char*)GetColorName(NextColor));
        } else {
            Display_Msg("??");
        }
        Display_Msg("     ");  // Clear rest
    }
    
    OS_Signal(&LCDmutex);
}

// Debounce delay (10ms minimum)
void Debounce(void) {
    OS_Sleep(10);
}

// Thread 1: Switch Monitor
// Continuously reads switches and updates formed color
void SwitchMonitorThread(void) {
    uint8_t lastColor = 0xFF;
    
    while(1) {
        uint8_t newColor = ReadSwitches();
        if(newColor != lastColor) {
            FormedColor = newColor;
            lastColor = newColor;
            UpdateLCDLine1();
        }
        OS_Sleep(50);  // Check every 50ms
    }
}

// Thread 2: Button Handler
// Handles SW5 button press with debouncing
void ButtonHandlerThread(void) {
    uint32_t lastState = 0;
    
    while(1) {
        uint32_t currentState = GPIO_PORTD_DATA_R & 0x01;  // Read PD0 (SW5)
        
        if((lastState != 0) && (currentState == 0)) {  // Positive edge (active low)
            Debounce();  // 10ms debounce
            
            // Check if still pressed after debounce
            if((GPIO_PORTD_DATA_R & 0x01) == 0) {
                if(!ColorFifo_IsFull()) {
                    uint8_t colorToAdd = FormedColor;
                    ColorFifo_Put(colorToAdd);
                    
                    // Check if buffer is now full
                    BufferFull = ColorFifo_IsFull();
                    UpdateLCDLine1();
                    
                    // Fast start: If queue was empty, update immediately
                    if(ColorFifo_Size() == 1 && CurrentColor == COLOR_BLACK) {
                        OS_Signal(&ColorUpdate);
                    }
                }
            }
        }
        
        lastState = currentState;
        OS_Sleep(20);  // Check every 20ms
    }
}

// Thread 3: Display Thread
// Updates LED and fetches colors from FIFO
void DisplayThread(void) {
    CurrentColor = COLOR_BLACK;
    NextColor = COLOR_BLACK;
    SetLED(COLOR_BLACK);
    
    while(1) {
        if(!ColorFifo_IsEmpty()) {
            CurrentColor = ColorFifo_Get();
            SetLED(CurrentColor);
            
            // Update buffer full status
            BufferFull = ColorFifo_IsFull();
            
            // Check for next color
            if(!ColorFifo_IsEmpty()) {
                NextColor = CurrentColor;
            } else {
                NextColor = COLOR_BLACK;
            }
            
            UpdateLCDLine2();
            UpdateLCDLine1();
        } else {
            CurrentColor = COLOR_BLACK;
            NextColor = COLOR_BLACK;
            SetLED(COLOR_BLACK);
            UpdateLCDLine2();
        }
        
        // Wait for 15 seconds or early update signal
        for(int i = 0; i < 150; i++) {  // 150 * 100ms = 15 seconds
            OS_Sleep(100);
            // Check for fast start signal
            if(ColorUpdate.value > 0) {
                OS_Wait(&ColorUpdate);
                break;
            }
        }
    }
}

// Thread 4: LCD Update Thread
// Periodic LCD updates
void LCDUpdateThread(void) {
    while(1) {
        UpdateLCDLine2();
        OS_Sleep(1000);  // Update every second
    }
}

// Main function
int main(void) {
    // Initialize hardware
    OS_Init();
    GPIO_Init();
    Init_LCD_Ports();
    Init_LCD();
    
    // Initialize semaphores
    OS_InitSemaphore(&LCDmutex, 1);
    OS_InitSemaphore(&ColorUpdate, 0);
    OS_InitSemaphore(&ButtonPressed, 0);
    
    // Initialize color FIFO
    ColorFifo_Init();
    
    // Initialize global variables
    CurrentColor = COLOR_BLACK;
    NextColor = COLOR_BLACK;
    FormedColor = COLOR_BLACK;
    BufferFull = 0;
    ColorTimer = 0;
    
    // Clear display
    SetLED(COLOR_BLACK);
    Set_Position(0x00);
    Display_Msg("Color Show");
    Set_Position(0x40);
    Display_Msg("Initializing... ");
    
    // Add threads
    OS_AddThread(SwitchMonitorThread, 1, 256);
    OS_AddThread(ButtonHandlerThread, 2, 256);
    OS_AddThread(DisplayThread, 3, 256);
    OS_AddThread(LCDUpdateThread, 4, 256);
    
    // Launch OS with 2ms time slice
    OS_Launch(32000);
    
    return 0;
}