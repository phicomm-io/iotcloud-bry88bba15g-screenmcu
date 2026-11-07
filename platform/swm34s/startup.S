/* Vector ordering from Synwit SWM341 GCC SDK 2025-08-07. */
.syntax unified
.cpu cortex-m33
.thumb
    .section .isr_vector,"a",%progbits
    .align 2
    .globl __isr_vector
__isr_vector:
    .long    __StackTop            
    .long    Reset_Handler         
    .long    NMI_Handler          
    .long    HardFault_Handler     
    .long    MemManage_Handler     
    .long    BusFault_Handler      
    .long    UsageFault_Handler   
    .long    0                    
    .long    0                    
    .long    0                    
    .long    0                     
    .long    SVC_Handler          
    .long    DebugMon_Handler     
    .long    0                     
    .long    PendSV_Handler           
    .long    SysTick_Handler         

    /* External interrupts */
    .long    UART0_Handler
    .long    UART1_Handler
    .long    TIMR0_Handler
    .long    TIMR1_Handler
    .long    DMA_Handler
    .long    SPI0_Handler
    .long    PWM0_Handler
    .long    WDT_Handler
    .long    UART2_Handler
    .long    PWM1_Handler
    .long    ADC0_Handler
    .long    BTIMR0_Handler
    .long    HALL0_Handler
    .long    PWM2_Handler
    .long    PWMBRK_Handler
    .long    I2C0_Handler
    .long    CAN0_Handler
    .long    SPI1_Handler
    .long    RTC_Handler
    .long    PWM3_Handler
    .long    TIMR2_Handler
    .long    UART3_Handler
    .long    TIMR3_Handler
    .long    ADC1_Handler
    .long    BOD_Handler
    .long    CORDIC_Handler
    .long    BTIMR1_Handler
    .long    PWM4_Handler
    .long    HALL3_Handler
    .long    BTIMR2_Handler
    .long    I2C1_Handler
    .long    BTIMR3_Handler
    .long    ACMP_Handler
    .long    XTALSTOP_Handler
    .long    FSPI_Handler
    .long    GPIOA_Handler
    .long    GPIOB_Handler
    .long    GPIOC_Handler
    .long    GPIOD_Handler
    .long    GPIOM_Handler
    .long    GPION_Handler
    .long    GPIOA0_Handler
    .long    GPIOA1_Handler
    .long    GPIOA5_Handler
    .long    GPIOA6_Handler
    .long    GPIOA10_Handler
    .long    GPIOA11_Handler
    .long    GPIOA12_Handler
    .long    GPIOA13_Handler
    .long    GPIOB0_Handler
    .long    GPIOB1_Handler
    .long    GPIOB2_Handler
    .long    GPIOC0_Handler
    .long    GPIOC1_Handler
    .long    GPIOC2_Handler
    .long    GPIOC3_Handler
    .long    GPIOC4_Handler
    .long    GPIOD3_Handler
    .long    GPIOD4_Handler
    .long    GPIOD5_Handler
    .long    GPIOD6_Handler
    .long    GPIOD7_Handler
    .long    GPIOD8_Handler
    .long    GPIOC9_Handler
    .long    GPIOC10_Handler
    .long    GPIOC11_Handler
    .long    GPIOC12_Handler
    .long    GPIOM0_Handler
    .long    GPIOM1_Handler
    .long    GPIOM2_Handler
    .long    GPIOM3_Handler
    .long    GPIOM4_Handler
    .long    DIV_Handler
    .long    LCD_Handler
    .long    GPIOE_Handler
    .long    JPEG_Handler
    .long    SDIO_Handler
    .long    USB_Handler
    .long    CAN1_Handler
    .long    TIMR4_Handler
    .long    BTIMR4_Handler
    .long    BTIMR5_Handler
    .long    BTIMR6_Handler
    .long    BTIMR7_Handler
    .long    BTIMR8_Handler
    .long    BTIMR9_Handler
    .long    BTIMR10_Handler
    .long    BTIMR11_Handler
    .long    DMA2D_Handler
    .long    QEI_Handler



.text
.thumb_func
.global Reset_Handler
Reset_Handler:
 cpsid i
 movs r0, #0
 msr control, r0
 msr basepri, r0
 isb
 ldr sp, =__StackTop
 bl screen_start
 b .
.weak NMI_Handler
.thumb_set NMI_Handler, board_fault_reset
.weak HardFault_Handler
.thumb_set HardFault_Handler, board_fault_reset
.weak MemManage_Handler
.thumb_set MemManage_Handler, board_fault_reset
.weak BusFault_Handler
.thumb_set BusFault_Handler, board_fault_reset
.weak UsageFault_Handler
.thumb_set UsageFault_Handler, board_fault_reset
.weak SVC_Handler
.thumb_set SVC_Handler, board_fault_reset
.weak DebugMon_Handler
.thumb_set DebugMon_Handler, board_fault_reset
.weak PendSV_Handler
.thumb_set PendSV_Handler, board_fault_reset
.weak UART1_Handler
.thumb_set UART1_Handler, board_fault_reset
.weak TIMR0_Handler
.thumb_set TIMR0_Handler, board_fault_reset
.weak TIMR1_Handler
.thumb_set TIMR1_Handler, board_fault_reset
.weak DMA_Handler
.thumb_set DMA_Handler, board_fault_reset
.weak SPI0_Handler
.thumb_set SPI0_Handler, board_fault_reset
.weak PWM0_Handler
.thumb_set PWM0_Handler, board_fault_reset
.weak WDT_Handler
.thumb_set WDT_Handler, board_fault_reset
.weak UART2_Handler
.thumb_set UART2_Handler, board_fault_reset
.weak PWM1_Handler
.thumb_set PWM1_Handler, board_fault_reset
.weak ADC0_Handler
.thumb_set ADC0_Handler, board_fault_reset
.weak BTIMR0_Handler
.thumb_set BTIMR0_Handler, board_fault_reset
.weak HALL0_Handler
.thumb_set HALL0_Handler, board_fault_reset
.weak PWM2_Handler
.thumb_set PWM2_Handler, board_fault_reset
.weak PWMBRK_Handler
.thumb_set PWMBRK_Handler, board_fault_reset
.weak I2C0_Handler
.thumb_set I2C0_Handler, board_fault_reset
.weak CAN0_Handler
.thumb_set CAN0_Handler, board_fault_reset
.weak SPI1_Handler
.thumb_set SPI1_Handler, board_fault_reset
.weak RTC_Handler
.thumb_set RTC_Handler, board_fault_reset
.weak PWM3_Handler
.thumb_set PWM3_Handler, board_fault_reset
.weak TIMR2_Handler
.thumb_set TIMR2_Handler, board_fault_reset
.weak UART3_Handler
.thumb_set UART3_Handler, board_fault_reset
.weak TIMR3_Handler
.thumb_set TIMR3_Handler, board_fault_reset
.weak ADC1_Handler
.thumb_set ADC1_Handler, board_fault_reset
.weak BOD_Handler
.thumb_set BOD_Handler, board_fault_reset
.weak CORDIC_Handler
.thumb_set CORDIC_Handler, board_fault_reset
.weak BTIMR1_Handler
.thumb_set BTIMR1_Handler, board_fault_reset
.weak PWM4_Handler
.thumb_set PWM4_Handler, board_fault_reset
.weak HALL3_Handler
.thumb_set HALL3_Handler, board_fault_reset
.weak BTIMR2_Handler
.thumb_set BTIMR2_Handler, board_fault_reset
.weak I2C1_Handler
.thumb_set I2C1_Handler, board_fault_reset
.weak BTIMR3_Handler
.thumb_set BTIMR3_Handler, board_fault_reset
.weak ACMP_Handler
.thumb_set ACMP_Handler, board_fault_reset
.weak XTALSTOP_Handler
.thumb_set XTALSTOP_Handler, board_fault_reset
.weak FSPI_Handler
.thumb_set FSPI_Handler, board_fault_reset
.weak GPIOA_Handler
.thumb_set GPIOA_Handler, board_fault_reset
.weak GPIOB_Handler
.thumb_set GPIOB_Handler, board_fault_reset
.weak GPIOC_Handler
.thumb_set GPIOC_Handler, board_fault_reset
.weak GPIOD_Handler
.thumb_set GPIOD_Handler, board_fault_reset
.weak GPIOM_Handler
.thumb_set GPIOM_Handler, board_fault_reset
.weak GPION_Handler
.thumb_set GPION_Handler, board_fault_reset
.weak GPIOA0_Handler
.thumb_set GPIOA0_Handler, board_fault_reset
.weak GPIOA1_Handler
.thumb_set GPIOA1_Handler, board_fault_reset
.weak GPIOA5_Handler
.thumb_set GPIOA5_Handler, board_fault_reset
.weak GPIOA6_Handler
.thumb_set GPIOA6_Handler, board_fault_reset
.weak GPIOA10_Handler
.thumb_set GPIOA10_Handler, board_fault_reset
.weak GPIOA11_Handler
.thumb_set GPIOA11_Handler, board_fault_reset
.weak GPIOA12_Handler
.thumb_set GPIOA12_Handler, board_fault_reset
.weak GPIOA13_Handler
.thumb_set GPIOA13_Handler, board_fault_reset
.weak GPIOB0_Handler
.thumb_set GPIOB0_Handler, board_fault_reset
.weak GPIOB1_Handler
.thumb_set GPIOB1_Handler, board_fault_reset
.weak GPIOB2_Handler
.thumb_set GPIOB2_Handler, board_fault_reset
.weak GPIOC0_Handler
.thumb_set GPIOC0_Handler, board_fault_reset
.weak GPIOC1_Handler
.thumb_set GPIOC1_Handler, board_fault_reset
.weak GPIOC2_Handler
.thumb_set GPIOC2_Handler, board_fault_reset
.weak GPIOC3_Handler
.thumb_set GPIOC3_Handler, board_fault_reset
.weak GPIOC4_Handler
.thumb_set GPIOC4_Handler, board_fault_reset
.weak GPIOD3_Handler
.thumb_set GPIOD3_Handler, board_fault_reset
.weak GPIOD4_Handler
.thumb_set GPIOD4_Handler, board_fault_reset
.weak GPIOD5_Handler
.thumb_set GPIOD5_Handler, board_fault_reset
.weak GPIOD6_Handler
.thumb_set GPIOD6_Handler, board_fault_reset
.weak GPIOD7_Handler
.thumb_set GPIOD7_Handler, board_fault_reset
.weak GPIOD8_Handler
.thumb_set GPIOD8_Handler, board_fault_reset
.weak GPIOC9_Handler
.thumb_set GPIOC9_Handler, board_fault_reset
.weak GPIOC10_Handler
.thumb_set GPIOC10_Handler, board_fault_reset
.weak GPIOC11_Handler
.thumb_set GPIOC11_Handler, board_fault_reset
.weak GPIOC12_Handler
.thumb_set GPIOC12_Handler, board_fault_reset
.weak GPIOM0_Handler
.thumb_set GPIOM0_Handler, board_fault_reset
.weak GPIOM1_Handler
.thumb_set GPIOM1_Handler, board_fault_reset
.weak GPIOM2_Handler
.thumb_set GPIOM2_Handler, board_fault_reset
.weak GPIOM3_Handler
.thumb_set GPIOM3_Handler, board_fault_reset
.weak GPIOM4_Handler
.thumb_set GPIOM4_Handler, board_fault_reset
.weak DIV_Handler
.thumb_set DIV_Handler, board_fault_reset
.weak LCD_Handler
.thumb_set LCD_Handler, board_fault_reset
.weak GPIOE_Handler
.thumb_set GPIOE_Handler, board_fault_reset
.weak JPEG_Handler
.thumb_set JPEG_Handler, board_fault_reset
.weak SDIO_Handler
.thumb_set SDIO_Handler, board_fault_reset
.weak USB_Handler
.thumb_set USB_Handler, board_fault_reset
.weak CAN1_Handler
.thumb_set CAN1_Handler, board_fault_reset
.weak TIMR4_Handler
.thumb_set TIMR4_Handler, board_fault_reset
.weak BTIMR4_Handler
.thumb_set BTIMR4_Handler, board_fault_reset
.weak BTIMR5_Handler
.thumb_set BTIMR5_Handler, board_fault_reset
.weak BTIMR6_Handler
.thumb_set BTIMR6_Handler, board_fault_reset
.weak BTIMR7_Handler
.thumb_set BTIMR7_Handler, board_fault_reset
.weak BTIMR8_Handler
.thumb_set BTIMR8_Handler, board_fault_reset
.weak BTIMR9_Handler
.thumb_set BTIMR9_Handler, board_fault_reset
.weak BTIMR10_Handler
.thumb_set BTIMR10_Handler, board_fault_reset
.weak BTIMR11_Handler
.thumb_set BTIMR11_Handler, board_fault_reset
.weak DMA2D_Handler
.thumb_set DMA2D_Handler, board_fault_reset
.weak QEI_Handler
.thumb_set QEI_Handler, board_fault_reset

.global board_fault_reset
.type board_fault_reset,%function
.thumb_func
board_fault_reset:
 cpsid i
 ldr r0, =fault_record
 mrs r1, ipsr
 str r1, [r0, #4]
 ldr r2, =0xe000ed28
 ldr r1, [r2]
 str r1, [r0, #8]
 ldr r1, [r2, #4]
 str r1, [r0, #12]
 ldr r1, [r2, #12]
 str r1, [r0, #16]
 ldr r1, [r2, #16]
 str r1, [r0, #20]
 str lr, [r0, #24]
 mrs r1, msp
 str r1, [r0, #28]
 /* Optional core frame: only the internal application MSP stack, with no
  * stacking/lazy-state fault. Recovery must also work with an unusable SP. */
 movs r3, #0
 str r3, [r0, #32]
 str r3, [r0, #36]
 str r3, [r0, #40]
 ldr r3, [r0, #8]
 ldr r2, =0x00003838
 tst r3, r2
 bne 2f
 tst lr, #4
 bne 2f
 tst r1, #7
 bne 2f
 ldr r2, =__StackTop-0x4000
 cmp r1, r2
 blo 2f
 tst lr, #16
 it eq
 addeq r1, r1, #72
 ldr r2, =__StackTop-32
 cmp r1, r2
 bhi 2f
 ldr r2, [r1, #28]
 tst r2, #0x01000000
 beq 2f
 str r2, [r0, #40]
 ldr r2, [r1, #24]
 str r2, [r0, #32]
 ldr r2, [r1, #20]
 str r2, [r0, #36]
2:
 ldr r0, =fault_latch_check
 ldr r1, =0xadbabcA9
 str r1, [r0]
 ldr r0, =fault_latch
 ldr r1, =0x52454356
 str r1, [r0]
 ldr r0, =0xe000ed0c
 ldr r1, =0x05fa0004
 str r1, [r0]
 dsb
1: b 1b
