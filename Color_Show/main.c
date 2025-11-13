// *****************************************************************************
// main.c - Color Show RTOS Application
// Runs on LM4F120/TM4C123
// Uses three threads to demonstrate RTOS with LCD display and RGB LED
// 
// Hardware Connections:
// - Port D (PD0-PD3): Input switches (active high with pull-down)
//   * PD0: SW5 (button to queue color)
//   * PD1: SW4 (Red)
//   * PD2: SW3 (Blue)
//   * PD3: SW2 (Green)
// - Port F (PF1-PF3): RGB LED outputs
//   * PF1: Red LED
//   * PF2: Blue LED
//   * PF3: Green LED
// - LCD connected via LCD.s driver functions
// *****************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include "os.h"
#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================
#define TIMESLICE               32000U       // Time slice for OS
#define TASK1_SLEEP_MS          10U          // Switch check rate
#define TASK3_TICK_MS           500U         // Timer tick (500ms)
#define COUNTDOWN_INPUT_SEC     15U          // Wait time when "Input a Color"
#define COUNTDOWN_DISPLAY_SEC   5U           // Display duration per color
#define DEBOUNCE_COUNT          5U           // Debounce counter threshold

// Port D switch masks
#define PD_SW5_MASK             0x01U        // PD0 - Queue button
#define PD_COLOR_MASK           0x0FU        // PD0-PD3 all switches

// Port F LED masks
#define PF_LED_MASK             0x0EU        // PF1-PF3 (RGB LED)
#define PF_RED                  0x02U        // PF1
#define PF_BLUE                 0x04U        // PF2
#define PF_GREEN                0x08U        // PF3

// LCD positions
#define LCD_LINE1               0x00U        // First line start
#define LCD_LINE2               0x40U        // Second line start
#define LCD_SWITCH_POS          0x09U        // "Switches: XXX"
#define LCD_CURRENT_POS         0x42U        // "C:XXX"
#define LCD_NEXT_POS            0x49U        // "N:XXX"
#define LCD_TIMER_POS           0x4EU        // Timer position

// Color encoding (matches switch hardware: GBR format)
#define COLOR_OFF               0x00U        // 000
#define COLOR_RED               0x02U        // 010
#define COLOR_BLUE              0x04U        // 100
#define COLOR_GREEN             0x08U        // 001
#define COLOR_CYAN              0x0CU        // 110 (Green + Blue)
#define COLOR_MAGENTA           0x06U        // 011 (Red + Blue)
#define COLOR_YELLOW            0x0AU        // 101 (Red + Green)
#define COLOR_WHITE             0x0EU        // 111 (All)

// =============================================================================
// EXTERNAL FUNCTIONS (from LCD.s)
// =============================================================================
extern void Init_LCD_Ports(void);
extern void Init_LCD(void);
extern void Set_Position(uint32_t pos);
extern void Display_Msg(char *str);
extern void Display_Char(int c);

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
Sema4Type LCD_Mutex;                        // LCD mutual exclusion
static uint32_t CurrentSwitchData = 0U;     // Current switch state
static uint32_t DebounceCtr = 0U;           // Debounce counter
static bool ButtonPressed = false;          // Button state tracker
static bool DisplayActive = false;          // True when displaying colors

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static inline uint32_t ReadSwitches(void) {
    return (GPIO_PORTD_DATA_R & PD_COLOR_MASK);
}

static inline bool IsButtonPressed(void) {
    return ((GPIO_PORTD_DATA_R & PD_SW5_MASK) != 0U);
}

static void SetLED(uint32_t color) {
    uint32_t ledValue = 0U;
    
    if (color & 0x08U) ledValue |= PF_GREEN;   // Green bit
    if (color & 0x04U) ledValue |= PF_BLUE;    // Blue bit
    if (color & 0x02U) ledValue |= PF_RED;     // Red bit
    
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~PF_LED_MASK) | ledValue;
}

static const char* GetColorName(uint32_t color) {
    switch (color) {
        case COLOR_OFF:     return "Off";
        case COLOR_RED:     return "Red";
        case COLOR_BLUE:    return "Blu";
        case COLOR_GREEN:   return "Grn";
        case COLOR_CYAN:    return "Cya";
        case COLOR_MAGENTA: return "Mag";
        case COLOR_YELLOW:  return "Yel";
        case COLOR_WHITE:   return "Wht";
        default:            return "???";
    }
}

static inline bool IsFifoFull(void) {
    return (CurrentSize >= FIFOSIZE);
}

static inline bool IsFifoEmpty(void) {
    return (CurrentSize <= 0);
}

// =============================================================================
// THREAD 1: SWITCH MONITOR AND BUTTON HANDLER
// =============================================================================

void Task1(void) {
    uint32_t switchSnapshot = 0U;
    
    while (1) {
        // Reset button state when released
        if ((GPIO_PORTD_DATA_R & PD_SW5_MASK) == 0U) {
            ButtonPressed = false;
        }
        
        // Read current switch state only when not displaying a color
        if (!DisplayActive) {
            CurrentSwitchData = GPIO_PORTD_DATA_R & PD_COLOR_MASK;
        } else {
            CurrentSwitchData = 0U;  // Force "Off" during color display
        }
        
        // Check if switches are being pressed (non-zero) and not displaying
        if (!DisplayActive && (GPIO_PORTD_DATA_R & PD_COLOR_MASK) != 0x00U) {
            switchSnapshot = (GPIO_PORTD_DATA_R & PD_COLOR_MASK);
            
            // Debounce: ensure stable reading
            while (DebounceCtr < DEBOUNCE_COUNT) {
                if ((GPIO_PORTD_DATA_R & PD_COLOR_MASK) == switchSnapshot) {
                    DebounceCtr++;
                } else {
                    DebounceCtr = 0U;
                }
            }
            
            // After debouncing, check for button press
            if ((DebounceCtr == DEBOUNCE_COUNT) && 
                ((GPIO_PORTD_DATA_R & PD_SW5_MASK) == 1U) && 
                !ButtonPressed) {
                
                DebounceCtr = 0U;
                ButtonPressed = true;
                
                // Try to add color to FIFO
                if (!IsFifoFull()) {
                    OS_Fifo_Put(CurrentSwitchData);
                }
            }
        }
        
        OS_Sleep(TASK1_SLEEP_MS);
    }
}

// =============================================================================
// THREAD 2: LCD DISPLAY UPDATE
// =============================================================================

void Task2(void) {
    uint32_t lastSwitchData = 0xFFU;  // Track last displayed value
    bool lastBufferFull = false;       // Track last buffer state
    
    while (1) {
        bool currentBufferFull = IsFifoFull();
        
        // Only update if something changed
        if ((CurrentSwitchData != lastSwitchData) || (currentBufferFull != lastBufferFull)) {
            OS_Wait(&LCD_Mutex);
            
            // Clear and update line 1
            Set_Position(LCD_LINE1);
            
            if (currentBufferFull) {
                Display_Msg("  Buffer Full!  ");
            } else {
                Display_Msg("Switches:");
                Display_Msg((char*)GetColorName(CurrentSwitchData));
                Display_Msg("    ");  // Clear trailing characters
            }
            
            OS_Signal(&LCD_Mutex);
            
            lastSwitchData = CurrentSwitchData;
            lastBufferFull = currentBufferFull;
        }
        
        // No sleep - run continuously to ensure scheduler always has a ready thread
    }
}

// =============================================================================
// THREAD 3: COLOR DISPLAY AND TIMER
// =============================================================================

void Task3(void) {
    uint32_t currentColor = COLOR_OFF;
    uint32_t nextColor = COLOR_OFF;
    uint32_t secondsRemaining = 0U;
    uint32_t displayTimer = COUNTDOWN_INPUT_SEC;  // Start in input mode
    
    SetLED(COLOR_OFF);
    
    while (1) {
        // Timer expired - check for new color
        if (secondsRemaining == 0U) {
            GPIO_PORTF_DATA_R = 0x00U;
            
            if (!IsFifoEmpty()) {
                // COLOR DISPLAY MODE - use shorter timer
                secondsRemaining = COUNTDOWN_DISPLAY_SEC;
                displayTimer = COUNTDOWN_DISPLAY_SEC;
                
                currentColor = OS_Fifo_Get();
                
                // Set LED based on color (mask out button bit and map to LED pins)
                uint32_t ledValue = 0U;
                if (currentColor & 0x08U) ledValue |= PF_GREEN;  // Green
                if (currentColor & 0x04U) ledValue |= PF_BLUE;   // Blue
                if (currentColor & 0x02U) ledValue |= PF_RED;    // Red
                GPIO_PORTF_DATA_R = ledValue;
                
                // Get next color from queue
                if (CurrentSize > 0) {
                    nextColor = Get_Next();
                } else {
                    nextColor = 8U;  // No next color
                }
                
                OS_Wait(&LCD_Mutex);
                Set_Position(LCD_LINE2);
                Display_Msg("C:");
                
                // Display current color
                switch (currentColor) {
                    case 0x09:  // green + button
                        Display_Msg("Grn");
                        break;
                    case 0x05:  // blue + button
                        Display_Msg("Blu");
                        break;
                    case 0x03:  // red + button
                        Display_Msg("Red");
                        break;
                    case 0x0D:  // cyan + button
                        Display_Msg("Cya");
                        break;
                    case 0x0B:  // yellow + button
                        Display_Msg("Yel");
                        break;
                    case 0x07:  // magenta + button
                        Display_Msg("Mag");
                        break;
                    case 0x0F:  // white + button
                        Display_Msg("Wht");
                        break;
                    default:
                        Display_Msg("???");
                        break;
                }
                
                Display_Msg(" N:");
                
                // Display next color
                if (nextColor != 8U) {
                    switch (nextColor) {
                        case 0x09:  // green
                            Display_Msg("Grn");
                            break;
                        case 0x05:  // blue
                            Display_Msg("Blu");
                            break;
                        case 0x03:  // red
                            Display_Msg("Red");
                            break;
                        case 0x0D:  // cyan
                            Display_Msg("Cya");
                            break;
                        case 0x0B:  // yellow
                            Display_Msg("Yel");
                            break;
                        case 0x07:  // magenta
                            Display_Msg("Mag");
                            break;
                        case 0x0F:  // white
                            Display_Msg("Wht");
                            break;
                        default:
                            Display_Msg("???");
                            break;
                    }
                } else {
                    Display_Msg("???");
                }
                
                Display_Msg("  ");
                OS_Signal(&LCD_Mutex);
                
                DisplayActive = true;
            } else {
                // INPUT MODE - use longer timer
                secondsRemaining = COUNTDOWN_INPUT_SEC;
                displayTimer = COUNTDOWN_INPUT_SEC;
                
                OS_Wait(&LCD_Mutex);
                Set_Position(LCD_LINE2);
                Display_Msg("Input a Color   ");
                OS_Signal(&LCD_Mutex);
                
                DisplayActive = false;
            }
        }
        
        // Update timer display ALWAYS
        OS_Wait(&LCD_Mutex);
        Set_Position(LCD_TIMER_POS);
        if (displayTimer > 9U) {
            Display_Char('1');
            Display_Char((char)((displayTimer - 10U) + '0'));
        } else {
            Display_Char('0');
            Display_Char((char)(displayTimer + '0'));
        }
        OS_Signal(&LCD_Mutex);
        
        // Sleep for 500ms
        OS_Sleep(TASK3_TICK_MS);
        
        // Decrement both counters
        if (secondsRemaining > 0U) {
            secondsRemaining--;
        }
        if (displayTimer > 0U) {
            displayTimer--;
        }
    }
}

// =============================================================================
// HARDWARE INITIALIZATION
// =============================================================================

static void PortD_Init(void) {
    SYSCTL_RCGCGPIO_R |= 0x08U;                     // Enable clock for Port D
    while ((SYSCTL_RCGCGPIO_R & 0x08U) == 0U) {}   // Wait for clock
    
    GPIO_PORTD_DIR_R &= ~PD_COLOR_MASK;             // PD0-3 as inputs
    GPIO_PORTD_DEN_R |= PD_COLOR_MASK;              // Digital enable
    GPIO_PORTD_PDR_R |= PD_COLOR_MASK;              // Pull-down resistors
}

static void PortF_Init(void) {
    SYSCTL_RCGCGPIO_R |= 0x20U;                     // Enable clock for Port F
    while ((SYSCTL_RCGCGPIO_R & 0x20U) == 0U) {}   // Wait for clock
    
    GPIO_PORTF_DIR_R |= PF_LED_MASK;                // PF1-3 as outputs
    GPIO_PORTF_DEN_R |= PF_LED_MASK;                // Digital enable
    GPIO_PORTF_DATA_R &= ~PF_LED_MASK;              // LEDs off initially
}

// =============================================================================
// MAIN FUNCTION
// =============================================================================
int main(void) {
    // Initialize OS
    OS_Init();
    
    // Initialize hardware
    PortD_Init();
    PortF_Init();
    Init_LCD_Ports();
    Init_LCD();
    
    // Initialize synchronization primitives
    OS_InitSemaphore(&LCD_Mutex, 1);
    OS_Fifo_Init();
    
    // Display startup message
    Set_Position(LCD_LINE1);
    Display_Msg("  Color Show!   ");
    Set_Position(LCD_LINE2);
    Display_Msg(" RTOS Active... ");
    
    // Small delay to show startup message
    for (volatile int i = 0; i < 1000000; i++) {}
    
    // Clear display - set initial state
    Set_Position(LCD_LINE1);
    Display_Msg("Switches:Off    ");
    Set_Position(LCD_LINE2);
    Display_Msg("Input a Color!  ");
    
    // Add threads to scheduler
    OS_AddThreads(&Task1, &Task2, &Task3);
    
    // Launch OS (does not return)
    OS_Launch(TIMESLICE);
    
    return 0;  // Never reached
}

// =============================================================================
// END OF FILE
// =============================================================================