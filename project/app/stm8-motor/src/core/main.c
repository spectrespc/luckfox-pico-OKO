/**
  ******************************************************************************
  * @file    main.c
  * @author  MCD Application Team
  * @version V2.3.0
  * @date    16-January-2014
  * @brief   Main program body
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
#include "stm8s_it.h"    /* SDCC patch: REQUIRED so SDCC emits the interrupt
                            vector table in this module (the one with main()).
                            Without it only the reset vector is generated and
                            no ISR (I2C/TIM4) ever fires. */
#include "main.h"
#include "sleep_logic.h"

/* Private variables ---------------------------------------------------------*/
/* Volatile: written from ISRs (TIM4/I2C) and read in the main loop. */
volatile uint32_t system_ticks = 0;
volatile uint32_t i2c_last_activity = 0;
volatile uint8_t i2c_need_reset = 0;
volatile uint8_t i2c_cmd_ready = 0;
uint32_t eeprom_last_save_time = 0;
volatile uint8_t eeprom_dirty = 0;

/* Sleep mode runtime state.  The compile-time default depends on the board:
 * the production build (BOARD=oko) is OFF and the host enables it explicitly,
 * the test build (BOARD=test, -DSLEEP_DEFAULT_ON=1) is ON from reset. */
volatile uint8_t  sleep_enabled = SLEEP_DEFAULT_ON;
volatile uint16_t sleep_idle_ms = SLEEP_IDLE_MS;
volatile uint8_t  g_sleep_state = SLEEP_STATE_AWAKE;
volatile uint16_t g_sleep_entries = 0;
volatile uint8_t  g_wake_by_i2c = 0;
volatile uint8_t  g_wake_by_timer = 0;

/**
  * @brief  Snapshot the live state for the pure sleep decision logic.
  *         Called with interrupts in whatever state the caller needs: the
  *         pre-Halt re-check runs under sim(), so the snapshot is atomic.
  */
static void sleep_read_conditions(sleep_conditions *sc)
{
    sc->last_activity = i2c_last_frame;
    sc->now = system_ticks;
    sc->idle_ms = sleep_idle_ms;
    sc->sleep_enabled = sleep_enabled;
    sc->motor_running = (uint8_t)((steppers[0].state == STATUS_RUNNING) ||
                                  (steppers[1].state == STATUS_RUNNING));
    sc->eeprom_dirty = eeprom_dirty;
    sc->i2c_busy = i2c_is_busy();
}

/* Private function prototypes ---------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Close the current sleep episode when a frame was served since the
  *         last look, and count that frame once as an I2C wake.
  *
  * Called at the top of every loop pass, so a frame that arrives while the loop
  * is running between two WFI calls (the address-match ISR runs immediately)
  * is still accounted for.  Counting only around WFI would leave the episode
  * open and under-report entries/wake_i2c in real traffic.
  * @param  seen          Last frame timestamp already accounted for.
  * @param  episode_open  Set to 0 when a frame closes the current episode.
  * @retval None
  */
static void sleep_note_frames(uint32_t *seen, uint8_t *episode_open)
{
    uint32_t now;

    sim();
    now = i2c_last_frame;
    rim();

    if (now == *seen)
        return;

    *seen = now;
    if (!*episode_open)
        return;

    *episode_open = 0;
    if (g_wake_by_i2c != 0xFF)
        g_wake_by_i2c++;
}


/**
  * @brief  Main program.
  * @param  None
  * @retval None
  */
void main(void)
{
    /* Initialize hardware */
    hw_reset();

    /* Initialise the application modules. These MUST be called: stepper_init()
       fills in the per-motor coil/nSLEEP/nFAULT pin fields, and eeprom_init()
       restores the saved positions. */
    stepper_init();
    eeprom_init();
    
    /* Enable interrupts */
    __asm__("rim");
    
    uint8_t led_state = 0;
    uint32_t last_led_tick = 0;
    uint8_t sleep_state = SLEEP_STATE_AWAKE;
    uint8_t sleep_episode_open = 0;
    uint32_t sleep_seen_frame = i2c_last_frame;

    /* Infinite loop */
    while (1)
    {
        IWDG_ReloadCounter();
        
        if (i2c_need_reset)
        {
            i2c_slave_reset();
            i2c_need_reset = 0;
        }
        
        if (i2c_check_timeout())
            i2c_slave_reset();
        
        /* A frame served since the last pass ends the current sleep episode
           and counts as an I2C wake, whether it landed during a WFI or while
           this loop was running. */
        sleep_note_frames(&sleep_seen_frame, &sleep_episode_open);

        if (i2c_cmd_ready)
        {
            __asm__("sim");
            i2c_process_command();
            i2c_cmd_ready = 0;
            __asm__("rim");
        }
        
        eeprom_periodic_save();

        /* I2C-idle sleep: after SLEEP_IDLE_MS of bus silence (and with no
           motion, transfer or pending frame) the core enters Wait mode for one
           TIM4 tick at a time (see hw_sleep_enter()).  The tick handler keeps
           the IWDG reloaded and the I2C slave keeps answering, so the sleep
           never blocks a host frame. */
        {
            sleep_conditions sc;
            uint8_t action;

            sleep_read_conditions(&sc);
            action = sleep_next_action(sleep_state, &sc);

            if (action == SLEEP_ACTION_SAVE)
            {
                /* Persist the positions before the tick stops; respects the
                   "no Flash programming while stepping" rule. */
                eeprom_force_save();
                sleep_state = SLEEP_STATE_FORCE_SAVE;
            }
            else if (action == SLEEP_ACTION_HALT)
            {
                /* Re-check with interrupts masked, then execute one WFI.  WFI
                   returns on the next interrupt: the 1 ms TIM4 tick, or an
                   I2C frame.  Interrupts must be enabled for WFI to return,
                   so the mask is released before entering. */
                uint32_t frame_before;

                sim();
                sleep_read_conditions(&sc);
                if (sleep_can_enter(&sc))
                {
                    frame_before = i2c_last_frame;
                    rim();

                    /* One episode per quiet period: it starts at the first
                       WFI and ends when a frame is served. */
                    if (!sleep_episode_open)
                    {
                        sleep_episode_open = 1;
                        if (g_sleep_entries != 0xFFFF)
                            g_sleep_entries++;
                    }

                    g_sleep_state = SLEEP_STATE_ASLEEP;
                    GPIO_WriteLow(GPIOD, GPIO_PIN_4);   /* LED off */
                    hw_sleep_enter();

                    /* A frame that arrived during WFI is counted by
                       sleep_note_frames() on the next pass, which also closes
                       the episode; only a pure timer wake is counted here. */
                    if (i2c_last_frame == frame_before)
                    {
                        if (g_wake_by_timer != 0xFF)
                            g_wake_by_timer++;
                    }

                    g_sleep_state = SLEEP_STATE_AWAKE;
                    sleep_state = SLEEP_STATE_AWAKE;
                    /* Keep the LED phase continuous across the idle window. */
                    last_led_tick = system_ticks;
                }
                else
                {
                    rim();
                }
            }
            else if (action == SLEEP_ACTION_WAKE)
            {
                g_sleep_state = SLEEP_STATE_AWAKE;
                sleep_state = SLEEP_STATE_AWAKE;
            }
            else
            {
                sleep_state = SLEEP_STATE_AWAKE;
            }
        }
        
        /* LED blink every 500ms */
        if (system_ticks - last_led_tick >= 500)
        {
            last_led_tick = system_ticks;
            led_state = !led_state;
            if (led_state)
                GPIO_WriteHigh(GPIOD, GPIO_PIN_4);
            else
                GPIO_WriteLow(GPIOD, GPIO_PIN_4);
        }
    }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
    /* User can add his own implementation to report the file name and line number */
    while (1)
    {
    }
}
#endif