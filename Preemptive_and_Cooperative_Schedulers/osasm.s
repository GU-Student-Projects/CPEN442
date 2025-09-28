;/*****************************************************************************/
; OSasm.s: low-level OS commands, written in assembly                       */
; Runs on LM4F120/TM4C123
; A very simple real time operating system with minimal features.
; Daniel Valvano
; January 29, 2015
;
; This example accompanies the book
;  "Embedded Systems: Real Time Interfacing to ARM Cortex M Microcontrollers",
;  ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2015
;
;  Programs 4.4 through 4.12, section 4.2
;
;Copyright 2015 by Jonathan W. Valvano, valvano@mail.utexas.edu
;    You may use, edit, run or distribute this file
;    as long as the above copyright notice remains
; THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
; OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
; MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
; VALVANO SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL,
; OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
; For more information about my classes, my research, and my books, see
; http://users.ece.utexas.edu/~valvano/
; */
        AREA |.text|, CODE, READONLY, ALIGN=2
        THUMB
        REQUIRE8
        PRESERVE8

        EXTERN  RunPt            ; currently running thread
        EXTERN  Scheduler        ; C function to select next thread
        EXPORT  OS_DisableInterrupts
        EXPORT  OS_EnableInterrupts
        EXPORT  StartOS
        EXPORT  SysTick_Handler
        EXPORT  PendSV_Handler

OS_DisableInterrupts
        CPSID   I
        BX      LR

OS_EnableInterrupts
        CPSIE   I
        BX      LR

StartOS
        LDR     R0, =RunPt         ; currently running thread
        LDR     R1, [R0]           ; R1 = value of RunPt
        LDR     SP, [R1]           ; new thread SP; SP = RunPt->sp;
        POP     {R4-R11}           ; restore regs R4-11
        POP     {R0-R3}            ; restore regs R0-3
        POP     {R12}
        ADD     SP, SP, #4         ; discard LR from initial stack
        POP     {LR}               ; start location
        ADD     SP, SP, #4         ; discard PSR
        CPSIE   I                  ; Enable interrupts at processor level
        BX      LR                 ; start first thread

; SysTick_Handler - Called every time slice
; Saves context, calls scheduler, restores context
SysTick_Handler
        CPSID   I                  ; Prevent interrupt during switch
        PUSH    {R4-R11}           ; Save remaining regs r4-11
        LDR     R0, =RunPt         ; R0 = pointer to RunPt, old thread
        LDR     R1, [R0]           ; R1 = RunPt
        STR     SP, [R1]           ; Save SP into TCB
        
        ; Call Scheduler to get next thread
        PUSH    {R0, LR}
        BL      Scheduler          ; Returns next thread in R0
        POP     {R1, LR}
        STR     R0, [R1]           ; RunPt = R0 (next thread)
        
        LDR     R1, [R1]           ; R1 = RunPt, new thread
        LDR     SP, [R1]           ; new thread SP; SP = RunPt->sp;
        POP     {R4-R11}           ; restore regs r4-11
        CPSIE   I                  ; interrupts enabled
        BX      LR                 ; restore R0-R3,R12,LR,PC,PSR

; PendSV_Handler - Used for context switch in OS_Suspend
; Lower priority than SysTick
PendSV_Handler
        CPSID   I                  ; Prevent interrupt during switch
        PUSH    {R4-R11}           ; Save remaining regs r4-11
        LDR     R0, =RunPt         ; R0 = pointer to RunPt, old thread
        LDR     R1, [R0]           ; R1 = RunPt
        STR     SP, [R1]           ; Save SP into TCB
        
        ; Call Scheduler to get next thread
        PUSH    {R0, LR}
        BL      Scheduler          ; Returns next thread in R0
        POP     {R1, LR}
        STR     R0, [R1]           ; RunPt = R0 (next thread)
        
        LDR     R1, [R1]           ; R1 = RunPt, new thread
        LDR     SP, [R1]           ; new thread SP; SP = RunPt->sp;
        POP     {R4-R11}           ; restore regs r4-11
        CPSIE   I                  ; interrupts enabled
        BX      LR                 ; restore R0-R3,R12,LR,PC,PSR

        ALIGN
        END