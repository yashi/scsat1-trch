/*
 * Space Cubics OBC TRCH Software
 *  FPGA Control Utility
 *
 * (C) Copyright 2021-2022
 *         Space Cubics, LLC
 *
 */

#include "fpga.h"

#include <pic.h>
#include <stdbool.h>
#include "trch.h"
#include "timer.h"

#define FPGA_WATCHDOG_TIMEOUT 3

static void fpga_wdt_init(struct fpga_management_data *fmd) {
#ifdef CONFIG_ENABLE_WDT_RESET
        fmd->wdt_value = 0;
        fmd->wdt_last_tick = timer_get_ticks();
#endif
}

static void fpga_wdt(struct fpga_management_data *fmd, bool wdt_value, uint32_t tick) {
#ifdef CONFIG_ENABLE_WDT_RESET
        if (fmd->wdt_value != wdt_value) {
                fmd->wdt_value = !fmd->wdt_value;
                fmd->wdt_last_tick = tick;
        }

        if (fmd->wdt_last_tick + FPGA_WATCHDOG_TIMEOUT < tick)
                fmd->config_ok = 0;
#endif
}

enum FpgaState fpga_init(struct fpga_management_data *fmd)
{
        fmd->state = FPGA_STATE_POWER_OFF;
        fmd->config_ok = 0;
        fmd->mem_select = 0;
        fmd->boot_mode = FPGA_BOOT_48MHZ;
        fmd->time = 0;

        return fmd->state;
}

bool fpga_is_i2c_accessible (enum FpgaState state) {
        return state == FPGA_STATE_POWER_OFF || state == FPGA_STATE_READY;
}

/*
 * POWER_OFF state
 *
 * pre-condition
 *  - state: POWER_OFF
 *  - FPGA_PWR_EN: LOW
 * post-condition
 *  - state: READY
 *    - config_ok: 1
 *    - FPGA_PWR_EN: HIGH
 *  - state: POWER_OFF
 *    - config_ok: 0
 *    - FPGA_PWR_EN: LOW
 *
 * When the user ask to actiavte the FPGA by setting config_ok to 1,
 * set FPGA_PWR_EN HIGH to start the power sequence, and transition to
 * READY state.
 *
 * Keep the FPGA power off, otherwise.
 */
static void f_power_off (struct fpga_management_data *fmd)
{
        /* check user request */
        if (fmd->config_ok) {
                FPGA_PWR_EN = 1;
                fmd->state = FPGA_STATE_READY;
        }
}

/*
 * READY state
 *
 * pre-condition
 *  - state: READY
 *  - FPGA_PWR_EN: HIGH
 * post-condtion
 *  - state: POWER_OFF
 *    - config_ok: 0
 *    - FPGA_PWR_EN: LOW
 *  - state: CONFIG
 *    - config_ok: 1
 *    - FPGA_PWR_EN: HIGH
 *    - VDD_3V3: 1
 *  - state: READY
 *    - config_ok: 1
 *    - FPGA_PWR_EN: HIGH
 *    - VDD_3V3: HIGH
 *
 * Wait for FPGA power to be stable by monitoring VDD_3V3 become high.
 *
 * Go back to POWER_OFF when config_ok become 0.
 *
 * Stay in READY otherwise.
 */
static void f_fpga_ready (struct fpga_management_data *fmd)
{
        /* check user request */
        if (!fmd->config_ok) {
                FPGA_PWR_EN = 0;
                fmd->state = FPGA_STATE_POWER_OFF;
                return;
        }

        /* wait for VDD_3V3 */
        if (VDD_3V3) {
                TRCH_CFG_MEM_SEL = (char)fmd->mem_select;
                FPGA_BOOT0 = 0b01 & fmd->boot_mode;
                FPGA_BOOT1 = 0b01 & (fmd->boot_mode >> 1);
                fpga_wdt_init(fmd);
                fmd->state = FPGA_STATE_CONFIG;
        }
}

/*
 * CONFIG state
 *
 * pre-condition
 *  - state: CONFIG
 *  - FPGA_PWR_EN: HIGH
 *  - VDD_3V3: HIGH
 * post-condtion
 *  - state: POWER_OFF
 *    - config_ok: 0
 *    - FPGA_PWR_EN: LOW
 *  - state: CONFIG
 *    - config_ok: 1
 *    - FPGA_PWR_EN: HIGH
 *    - VDD_3V3: HIGH
 *    - FPGA_INIT_B: NOT LOW
 *
 * Wait for the completion of the FPGA configuration sequence.  The
 * FPGA must drive FPGA_WATCHDOG HIGH once the configuration is done.
 *
 * Go back to POWER_OFF when config_ok become 0.
 *
 * Stay in CONFIG state, otherwise.
 *
 * Note that fpga_wdt() counts ticks from the last wdt kick and sets
 * config_ok to 0 if the count exceeds FPGA_WATCHDOG_TIMEOUT.
 */
static void f_fpga_config (struct fpga_management_data *fmd)
{
        fpga_wdt(fmd, FPGA_WATCHDOG, timer_get_ticks());

        /* check user request */
        if (!fmd->config_ok) {
                FPGA_PWR_EN = 0;
                fmd->mem_select = !fmd->mem_select;
                fmd->state = FPGA_STATE_POWER_OFF;
                return;
        }

        /* wait for watchdog pulse from the fpga */
        if (FPGA_WATCHDOG) {
                fmd->state = FPGA_STATE_ACTIVE;
        }
}

/*
 * ACTIVE state
 *
 * pre-condition
 *  - state: ACTIVE
 *  - FPGA_PWR_EN: HIGH
 *  - VDD_3V3: 1
 *  - FPGA_INIT_B: NOT LOW
 * post-condtion
 *  - state: POWER_OFF
 *    - config_ok: 0
 *    - FPGA_PWR_EN: LOW
 *  - state: ACTIVE
 *    - config_ok: 1
 *    - FPGA_PWR_EN: HIGH
 *    - VDD_3V3: HIGH
 *    - FPGA_INIT_B: NOT LOW
 *
 * Monitor watchdog kicks from the FPGA.  Shutdown the FPGA if no kick
 * is observed in FPGA_WATCHDOG_TIMEOUT ticks.
 *
 * Stay in ACTIVE state, otherwise.
 *
 * Note that fpga_wdt() counts ticks from the last wdt kick and sets
 * config_ok to 0 if the count exceeds FPGA_WATCHDOG_TIMEOUT.
 */
static void f_fpga_active (struct fpga_management_data *fmd)
{
        fpga_wdt(fmd, FPGA_WATCHDOG, timer_get_ticks());

        /* check user request */
        if (!fmd->config_ok) {
                FPGA_PWR_EN = 0;
                fmd->mem_select = !fmd->mem_select;
                fmd->state = FPGA_STATE_POWER_OFF;
                return;
        }

        TRCH_CFG_MEM_SEL = FPGA_CFG_MEM_SEL;
}

typedef void (*STATEFUNC)(struct fpga_management_data *fmd);

static STATEFUNC fpgafunc[] = {
        f_power_off,
        f_fpga_ready,
        f_fpga_config,
        f_fpga_active };

void fpga_state_control(struct fpga_management_data *fmd)
{
       fpgafunc[fmd->state](fmd);
}
