// controller.c
// DC Motor Speed Controller
// Implements PID control with anti-windup
// Updates every 10ms (100 Hz control rate)
// Target steady-state error: ±15 RPM
//
// Control equation:
// u(t) = Kp*e(t) + Ki*∫e(t)dt + Kd*de(t)/dt
// where e(t) = target_rpm - measured_rpm

#include "TM4C123GH6PM.h"
#include "tm4c123gh6pm_def.h"
#include <stdint.h>

#include "system.h" 

// PWM limits (in tenths of percent)
#define PWM_DUTY_MIN   180     // 18.0%
#define PWM_DUTY_MAX   995     // 99.5%
#define PWM_DUTY_ZERO  0       // 0% for target = 0 RPM

// PID Controller gains (tunable parameters)
// These values are starting points and should be tuned experimentally
#define KP  50      // Proportional gain (scaled by 100)
#define KI  10      // Integral gain (scaled by 100)
#define KD  20      // Derivative gain (scaled by 100)

// Anti-windup limits for integral term
#define INTEGRAL_MAX   5000    // Maximum integral accumulation
#define INTEGRAL_MIN   -5000   // Minimum integral accumulation

// Control state variables
static volatile int32_t Error_Current = 0;
static volatile int32_t Error_Previous = 0;
static volatile int32_t Error_Integral = 0;
static volatile int32_t Error_Derivative = 0;

// Control output
static volatile int32_t Control_Output = 0;

// Statistics
static volatile uint32_t Control_Updates = 0;

//******** Controller_Init ************
// Initialize controller state variables
void Controller_Init(void){
    Error_Current = 0;
    Error_Previous = 0;
    Error_Integral = 0;
    Error_Derivative = 0;
    Control_Output = 0;
    Control_Updates = 0;
}

//******** Controller_Update ************
// Update PID controller and adjust PWM duty cycle
// Called every 10ms by Controller_LCD_Thread
// Input: target_rpm - desired speed (0 or 400-2400)
//        current_rpm - measured speed from ADC
void Controller_Update(int32_t target_rpm, int32_t current_rpm){
    int32_t control_signal;
    uint16_t duty_cycle;
    
    Control_Updates++;
    
    // Handle special case: target = 0 RPM
    if(target_rpm == 0){
        PWM_SetDutyCycle(PWM_DUTY_ZERO);
        Controller_Init(); // Reset controller state
        return;
    }
    
    // Compute error
    Error_Current = target_rpm - current_rpm;
    
    // Compute integral term (with anti-windup)
    Error_Integral += Error_Current;
    if(Error_Integral > INTEGRAL_MAX){
        Error_Integral = INTEGRAL_MAX;
    }
    else if(Error_Integral < INTEGRAL_MIN){
        Error_Integral = INTEGRAL_MIN;
    }
    
    // Compute derivative term
    Error_Derivative = Error_Current - Error_Previous;
    
    // Compute PID control signal (scaled by 100)
    // control_signal = (Kp * error + Ki * integral + Kd * derivative) / 100
    control_signal = ((KP * Error_Current) + 
                     (KI * Error_Integral) + 
                     (KD * Error_Derivative)) / 100;
    
    // Get current duty cycle
    duty_cycle = PWM_GetDutyCycle();
    
    // Apply control signal to duty cycle
    // Assume ~10 RPM change per 1% duty cycle change (approximate)
    // Scale control signal to duty cycle adjustment
    duty_cycle += (control_signal / 10);
    
    // Apply safety limits
    if(duty_cycle < PWM_DUTY_MIN){
        duty_cycle = PWM_DUTY_MIN;
        // Anti-windup: don't let integral term grow if at saturation
        if(Error_Current > 0){
            Error_Integral -= Error_Current;
        }
    }
    else if(duty_cycle > PWM_DUTY_MAX){
        duty_cycle = PWM_DUTY_MAX;
        // Anti-windup: don't let integral term grow if at saturation
        if(Error_Current < 0){
            Error_Integral -= Error_Current;
        }
    }
    
    // Update PWM
    PWM_SetDutyCycle(duty_cycle);
    
    // Save current error for next iteration
    Error_Previous = Error_Current;
    
    // Store control output for debugging/monitoring
    Control_Output = control_signal;
}

//******** Controller_GetError ************
// Get current error value (for debugging/monitoring)
// Returns: Current error in RPM
int32_t Controller_GetError(void){
    return Error_Current;
}

//******** Controller_GetIntegral ************
// Get current integral term (for debugging/monitoring)
// Returns: Accumulated integral error
int32_t Controller_GetIntegral(void){
    return Error_Integral;
}

//******** Controller_GetDerivative ************
// Get current derivative term (for debugging/monitoring)
// Returns: Error rate of change
int32_t Controller_GetDerivative(void){
    return Error_Derivative;
}

//******** Controller_ResetIntegral ************
// Reset integral term (useful when changing target speed)
void Controller_ResetIntegral(void){
    Error_Integral = 0;
}

//******** Controller_TuneGains ************
// Adjust PID gains during runtime (for tuning)
// Note: This function would need to modify the const values
// For production, gains should be determined experimentally and hardcoded
void Controller_SetGains(int32_t kp, int32_t ki, int32_t kd){
    // This would require making KP, KI, KD non-const
    // Left as exercise for tuning phase
    // For now, modify the #define values in code
}

//******** Controller_GetStatistics ************
// Get controller statistics
// Returns: Number of control updates performed
uint32_t Controller_GetStatistics(void){
    return Control_Updates;
}


/******************************************************************************
 * CONTROLLER TUNING NOTES:
 * 
 * The PID gains (KP, KI, KD) provided above are starting values and MUST be
 * tuned experimentally for your specific motor and system.
 * 
 * TUNING PROCEDURE (Ziegler-Nichols method):
 * 
 * 1. Set KI = 0, KD = 0
 * 2. Increase KP until system oscillates consistently
 *    - Record this value as Ku (ultimate gain)
 *    - Record oscillation period as Tu
 * 
 * 3. Calculate PID gains:
 *    - KP = 0.6 * Ku
 *    - KI = 1.2 * Ku / Tu
 *    - KD = 0.075 * Ku * Tu
 * 
 * ALTERNATIVE: Manual tuning
 * 
 * 1. Start with KP only:
 *    - Increase KP until fast response with slight overshoot
 * 
 * 2. Add KI:
 *    - Increase KI to eliminate steady-state error
 *    - If system becomes unstable, reduce KI
 * 
 * 3. Add KD:
 *    - Increase KD to reduce overshoot
 *    - Too much KD causes instability from noise
 * 
 * MONITORING:
 * - Use LCD to display error values during tuning
 * - Observe system response to step changes in target speed
 * - Acceptable performance: ±15 RPM steady-state error
 * 
 * TROUBLESHOOTING:
 * - System oscillates: Reduce KP and/or KD
 * - Slow response: Increase KP
 * - Steady-state error: Increase KI
 * - Overshoot: Increase KD or reduce KP
 * 
 *****************************************************************************/