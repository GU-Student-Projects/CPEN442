; *****************************************************************************
; osasm.s - Low-level OS Assembly Functions
; Runs on LM4F120/TM4C123
; A simple real time operating system with minimal features
; 
; *****************************************************************************

        IMPORT  Scheduler           ; Scheduler function in os.c
        IMPORT  RunPt               ; Pointer to currently running thread

        AREA    |.text|, CODE, READONLY, ALIGN=2
        THUMB
        REQUIRE8
        PRESERVE8

        EXPORT  OS_DisableInterrupts
        EXPORT  OS_EnableInterrupts
        EXPORT  StartCritical
        EXPORT  EndCritical
        EXPORT  StartOS
        EXPORT  SysTick_Handler

; =============================================================================
; INTERRUPT CONTROL FUNCTIONS
; =============================================================================

; OS_DisableInterrupts
; Disable interrupts (set I bit in PRIMASK)
; Input:  none
; Output: none
OS_DisableInterrupts
        CPSID   I               ; Set I bit (disable interrupts)
        BX      LR              ; Return

; OS_EnableInterrupts
; Enable interrupts (clear I bit in PRIMASK)
; Input:  none
; Output: none
OS_EnableInterrupts
        CPSIE   I               ; Clear I bit (enable interrupts)
        BX      LR              ; Return

; StartCritical
; Save current interrupt state and disable interrupts
; Input:  none
; Output: R0 = previous PRIMASK value (0 = enabled, 1 = disabled)
StartCritical
        MRS     R0, PRIMASK     ; Read PRIMASK into R0
        CPSID   I               ; Disable interrupts
        BX      LR              ; Return with old PRIMASK in R0

; EndCritical
; Restore previous interrupt state
; Input:  R0 = previous PRIMASK value
; Output: none
EndCritical
        MSR     PRIMASK, R0     ; Restore PRIMASK from R0
        BX      LR              ; Return

; =============================================================================
; CONTEXT SWITCH AND STARTUP
; =============================================================================

; SysTick_Handler
; Performs context switch between threads
; 1. Save context of current thread
; 2. Call scheduler to select next thread
; 3. Restore context of next thread
; Note: R0-R3, R12, LR, PC, PSR automatically saved by CPU
SysTick_Handler
        CPSID   I               ; Disable interrupts during switch
        PUSH    {R4-R11}        ; Save remaining registers (R4-R11)
        
        LDR     R0, =RunPt      ; R0 = address of RunPt
        LDR     R1, [R0]        ; R1 = RunPt (current TCB)
        STR     SP, [R1]        ; Save SP into current TCB
        
        PUSH    {LR, R0}        ; Save LR and R0 for scheduler call
        BL      Scheduler       ; Call C function to select next thread
        POP     {LR, R0}        ; Restore LR and R0
        
        LDR     R1, [R0]        ; R1 = RunPt (next TCB after scheduling)
        LDR     SP, [R1]        ; Load SP from next TCB
        
        POP     {R4-R11}        ; Restore R4-R11 for next thread
        CPSIE   I               ; Re-enable interrupts
        BX      LR              ; Return (will restore R0-R3, R12, LR, PC, PSR)

; StartOS
; Initialize stack pointer and start first thread
; This function is called once by OS_Launch and never returns
; Input:  RunPt must point to first thread's TCB
; Output: none (does not return)
StartOS
        LDR     R0, =RunPt      ; R0 = address of RunPt
        LDR     R2, [R0]        ; R2 = RunPt (first TCB)
        LDR     SP, [R2]        ; Initialize SP from first TCB
        
        POP     {R4-R11}        ; Restore R4-R11
        POP     {R0-R3}         ; Restore R0-R3
        POP     {R12}           ; Restore R12
        POP     {LR}            ; Discard initial LR
        POP     {LR}            ; Load PC into LR (thread start address)
        POP     {R1}            ; Discard PSR
        
        CPSIE   I               ; Enable interrupts
        BX      LR              ; Branch to first thread

        ALIGN
        END
