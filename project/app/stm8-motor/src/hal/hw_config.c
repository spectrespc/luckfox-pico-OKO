/**
  ******************************************************************************
  * @file    hw_config.c
  * @author  MCD Application Team
  * @version V2.3.0
  * @date    16-January-2014
  * @brief   Hardware configuration file
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm8s_conf.h"
#include "hw_config.h"
#include "main.h"

/* Private typedef ---------------------------------------------------------*/
/* Private define ----------------------------------------------------------*/
/* AWU time base: interval = (APR + 1) << (TBR - 1) LSI periods.
 * APR = 24, TBR = 12 -> 25 * 2048 = 51200 LSI periods.
 * At the nominal 128 kHz LSI this is a 400 ms slice, i.e. 80 % of the IWDG
 * window (250 * 256 = 64000 periods).  Both dividers run from the same LSI,
 * so the ratio holds across the part-to-part LSI tolerance. */
#define AWU_APR_VALUE      24
#define AWU_TBR_VALUE      12

/* Private macro -------------------------------------------------------------*/
/* Private variables -----------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
/* Private functions -----------------------------------------------------------*/

/**
  * @brief  Initialize system clock
  * @param  None
  * @retval None
  */
void hw_clock_init(void)
{
    /* External resonator is NOT used. All clocking is from the internal HSI. */
    CLK_DeInit();
    CLK_HSICmd(ENABLE);
    while (CLK_GetFlagStatus(CLK_FLAG_HSIRDY) == RESET);
    CLK_ClockSwitchConfig(CLK_SWITCHMODE_AUTO, CLK_SOURCE_HSI, DISABLE, CLK_CURRENTCLOCKSTATE_DISABLE);

    /* Remove BOTH dividers: HSIDIV = /1 and CPUDIV = /1  ->  fMASTER = fHSI = 16 MHz.
       NOTE: CLK_SYSCLKConfig() only controls CPUDIV and leaves HSIDIV at its
       reset value (0b11 = HSI/8 = 2 MHz), which is far too slow. The spec
       requires writing CLK_CKDIVR = 0x00, so do exactly that. */
    CLK->CKDIVR = 0x00;
}

/**
  * @brief  Initialize GPIOs for the two DRV8833 H-bridge drivers.
  *
  * Each DRV8833 stepper channel needs four coil inputs (AIN1/AIN2 for coil A,
  * BIN1/BIN2 for coil B) plus nSLEEP (output) and nFAULT (input). There is no
  * STEP/DIR interface: the MCU sequences the coils itself (see stepper.c).
  *
  * TSSOP20 note: PB0-PB3 are NOT bonded out on the STM8S003F3P6 /
  * STM8S103F3P6 TSSOP20 (only PB4/PB5 exist), so all coil/sleep/fault
  * signals use port A/C/D pins.
  * @param  None
  * @retval None
  */
void hw_gpio_init(void)
{
    /* Configure all GPIOs */
    GPIO_DeInit(GPIOA);
    GPIO_DeInit(GPIOB);
    GPIO_DeInit(GPIOC);
    GPIO_DeInit(GPIOD);
    
    /* PB4/PB5 - I2C SCL/SDA (open-drain) */
    GPIO_Init(GPIOB, GPIO_PIN_4 | GPIO_PIN_5, GPIO_MODE_OUT_OD_HIZ_FAST);

#if defined(BOARD_TEST)
    /* --- Test bench wiring (STM8S003F3P6) ---
       Focus DRV8833: PD5=AIN1, PC7=AIN2, PD3=BIN1, PD2=BIN2,
       PA1=nSLEEP (LOW = sleep), PA2=nFAULT (pull-up input).
       Zoom DRV8833:  PC4=AIN1, PC3=AIN2, PC6=BIN1, PC5=BIN2,
       PA3=nSLEEP, PD6=nFAULT (pull-up input). */
    GPIO_Init(GPIOC, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
                      GPIO_PIN_7,
              GPIO_MODE_OUT_PP_LOW_FAST);
    GPIO_Init(GPIOD, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5,
              GPIO_MODE_OUT_PP_LOW_FAST);
    GPIO_Init(GPIOA, GPIO_PIN_1 | GPIO_PIN_3, GPIO_MODE_OUT_PP_LOW_FAST);
    /* nFAULT is open-drain; the internal pull-up is enough (1 uA leakage). */
    GPIO_Init(GPIOA, GPIO_PIN_2, GPIO_MODE_IN_PU_NO_IT);
    GPIO_Init(GPIOD, GPIO_PIN_6, GPIO_MODE_IN_PU_NO_IT);
#else
    /* --- Focus DRV8833 ---
       PC3=AIN1, PC4=AIN2, PC5=BIN1, PC6=BIN2 (coil outputs, start coasting) */
    GPIO_Init(GPIOC, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6,
              GPIO_MODE_OUT_PP_LOW_FAST);
    /* PD3 - nSLEEP (LOW = driver in sleep) */
    GPIO_Init(GPIOD, GPIO_PIN_3, GPIO_MODE_OUT_PP_LOW_FAST);
    /* PD2 - nFAULT (open-drain, LOW on fault -> pull-up, read RESET means fault) */
    GPIO_Init(GPIOD, GPIO_PIN_2, GPIO_MODE_IN_PU_NO_IT);
    
    /* --- Zoom DRV8833 ---
       PD5=AIN1, PD6=AIN2, PA3=BIN1, PC7=BIN2 (coil outputs) */
    GPIO_Init(GPIOD, GPIO_PIN_5 | GPIO_PIN_6, GPIO_MODE_OUT_PP_LOW_FAST);
    GPIO_Init(GPIOA, GPIO_PIN_3, GPIO_MODE_OUT_PP_LOW_FAST);
    GPIO_Init(GPIOC, GPIO_PIN_7, GPIO_MODE_OUT_PP_LOW_FAST);
    /* PA1 - nSLEEP */
    GPIO_Init(GPIOA, GPIO_PIN_1, GPIO_MODE_OUT_PP_LOW_FAST);
    /* PA2 - nFAULT (pull-up) */
    GPIO_Init(GPIOA, GPIO_PIN_2, GPIO_MODE_IN_PU_NO_IT);
#endif
    
    /* PD4 - LED_STATUS (PP low) */
    GPIO_Init(GPIOD, GPIO_PIN_4, GPIO_MODE_OUT_PP_LOW_FAST);

    /* PD1 is intentionally left in its reset state (input floating): it is the
       SWIM data pin and must stay free for programming. */
}

/**
  * @brief  Initialize IWDG
  * @param  None
  * @retval None
  */
void hw_iwdg_init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_256);
    IWDG_SetReload(250); // ~500ms
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/**
  * @brief  Initialize TIM4
  * @param  None
  * @retval None
  */
void hw_tim4_init(void)
{
    /* Enable TIM4 clock */
    CLK_PeripheralClockConfig(CLK_PERIPHERAL_TIMER4, ENABLE);
    
    /* Configure TIM4 time base for 1ms tick */
    TIM4_TimeBaseInit(TIM4_PRESCALER_128, 124);
    
    /* Enable update interrupt */
    TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
    
    /* Enable TIM4 */
    TIM4_Cmd(ENABLE);
}

/**
  * @brief  Initialize the Auto Wake-Up unit (Active-halt timer).
  *
  * The AWU is configured once and kept disarmed; hw_sleep_enter() arms it only
  * right before HALT, exactly like the STM32C011 reference arms its wake-up
  * only around the Stop entry.  The LSI is shared with the IWDG.
  * @param  None
  * @retval None
  */
void hw_awu_init(void)
{
    /* IWDG already needs the LSI, but the AWU register block only works once
       the oscillator is up; wait for it explicitly. */
    CLK->ICKR |= CLK_ICKR_LSIEN;
    while (!(CLK->ICKR & CLK_ICKR_LSIRDY))
        ;

    AWU->APR = (uint8_t)((AWU->APR & (uint8_t)~AWU_APR_APR) | AWU_APR_VALUE);
    AWU->TBR = (uint8_t)((AWU->TBR & (uint8_t)~AWU_TBR_AWUTB) | AWU_TBR_VALUE);

    AWU->CSR &= (uint8_t)~AWU_CSR_AWUEN;
    AWU->CSR &= (uint8_t)~AWU_CSR_AWUF;
}

/**
  * @brief  Enter Active-halt for one AWU slice.
  *
  * Preconditions: interrupts are masked by the caller (sim()), and the sleep
  * gate has just been re-evaluated; see the halt sequence in main.c.
  *
  * STM8S003F3 datasheet, Table 11: the I2C address match wakes the core from
  * both Halt and Active-halt, EXTI does too, the AWU wakes from Active-halt
  * only, and TIM4 wakes it from neither.  TIM4 must therefore never be used as
  * the wake source; the AWU is the timed wake-up.
  * @param  None
  * @retval None
  */
void hw_sleep_enter(void)
{
    /* Fast wake-up: the application runs at 16 MHz and must resume without
       the full HSI start-up delay. */
    CLK->ICKR |= CLK_ICKR_FHWU;

    /* Stop the 1 ms tick and clear the pending update flag.  Leaving UIF
       pending would kick the core out of HALT immediately (the same trap was
       found on the STM32C011 port).  CEN/UIE are restored on wake. */
    TIM4->CR1 &= (uint8_t)~0x01;      /* CEN = 0 */
    TIM4->IER &= (uint8_t)~0x01;      /* UIE = 0 */
    TIM4->SR1 = (uint8_t)~0x01;       /* clear UIF (rc_w0) */

    IWDG_ReloadCounter();

    /* Arm the AWU (single timed wake-up source) and drop a stale flag. */
    AWU->CSR &= (uint8_t)~AWU_CSR_AWUF;
    AWU->CSR |= AWU_CSR_AWUEN;

    __asm__("halt");
}

/**
  * @brief  Leave Active-halt: disarm the AWU and restart the 1 ms tick.
  * @param  None
  * @retval None
  */
void hw_sleep_exit(void)
{
    AWU->CSR &= (uint8_t)~AWU_CSR_AWUEN;
    AWU->CSR &= (uint8_t)~AWU_CSR_AWUF;

    /* Release SCL if the core was woken by an I2C address match: the I2C
       stretches the clock until WUFH is cleared (RM0016, I2C low power modes).
       The ISR clears it again as a safety net. */
    if (I2C->SR2 & I2C_SR2_WUFH)
        I2C->SR2 = (uint8_t)~I2C_SR2_WUFH;

    /* With FHWU the HSI is already running, but never return to code that
       assumes 16 MHz without checking. */
    while (!(CLK->ICKR & CLK_ICKR_HSIRDY))
        ;

    TIM4->CR1 &= (uint8_t)~0x01;
    TIM4->SR1 = (uint8_t)~0x01;
    TIM4->IER |= 0x01;
    TIM4->CR1 |= 0x01;
}

/**
  * @brief  Wake-cause check, valid immediately after hw_sleep_enter() returns.
  * @retval 1 if the AWU slice elapsed, 0 if something else woke the core
  *         (I2C address match or a stray interrupt).
  */
uint8_t hw_sleep_wake_awu(void)
{
    return (AWU->CSR & AWU_CSR_AWUF) ? 1 : 0;
}

/**
  * @brief  Initialize I2C as slave
  * @param  None
  * @retval None
  */
void hw_i2c_init(void)
{
    /* Enable I2C clock */
    CLK_PeripheralClockConfig(CLK_PERIPHERAL_I2C, ENABLE);

    /* Single I2C configuration path: reuse the runtime re-init routine so the
       boot configuration and the error/timeout recovery can never diverge. */
    i2c_slave_reset();
}

/**
  * @brief  Reset system
  * @param  None
  * @retval None
  */
void hw_reset(void)
{
    hw_clock_init();
    hw_gpio_init();
    hw_iwdg_init();
    hw_tim4_init();
    hw_i2c_init();
    hw_awu_init();
}