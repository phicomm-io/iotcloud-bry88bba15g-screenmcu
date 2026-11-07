/* Minimal SDK boundary for executing the actual board RTC bus on the host. */
#pragma once
#include <stdint.h>
typedef struct {uint32_t unused;} GPIO_TypeDef;
typedef struct {uint32_t CLKEN0;} SYS_TypeDef;
extern GPIO_TypeDef test_c, test_d;
extern SYS_TypeDef test_sys;
#define GPIOC (&test_c)
#define GPIOD (&test_d)
#define SYS (&test_sys)
#define SYS_CLKEN0_GPIOC_Msk 4u
#define SYS_CLKEN0_GPIOD_Msk 8u
#define GPIO_OUTPUT_OpenDrain_PullUp 11u
extern uint32_t CyclesPerUs;
void test_nop(void);
#define __NOP() test_nop()
void GPIO_Init(GPIO_TypeDef *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define GPIO_INIT(p, n, mode) GPIO_Init(p, n, !!((mode)&1), !!((mode)&2), \
                                       !!((mode)&4), !!((mode)&8))
void GPIO_SetBit(GPIO_TypeDef *, uint32_t);
void GPIO_ClrBit(GPIO_TypeDef *, uint32_t);
uint32_t GPIO_GetBit(GPIO_TypeDef *, uint32_t);
