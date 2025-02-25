#include "flash.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/pm/device.h>
#include <string.h>

#define BUTTONS_NODE DT_PATH(buttons)

#define GPIO_SPEC_AND_COMMA(button) GPIO_DT_SPEC_GET(button, gpios),

static const struct gpio_dt_spec da_buttons[] =
{
#if DT_NODE_EXISTS(BUTTONS_NODE)
    DT_FOREACH_CHILD(BUTTONS_NODE, GPIO_SPEC_AND_COMMA)
#endif
};

static struct gpio_callback da_gpio_cb_info;

enum commands_t
{
    DA_CMD_DO_NOTHING = 0,
    DA_CMD_DO_WORK,
    DA_CMD_SYSTEM_OFF_SLEEP
};

static int da_command = DA_CMD_DO_NOTHING;
static struct k_sem da_command_sem;

static struct gpio_dt_spec r_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});
static struct gpio_dt_spec g_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});
static struct gpio_dt_spec b_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0});

static bool da_work_LED_state = 0;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (pins & BIT(da_buttons[0].pin))
    {
        da_command = DA_CMD_DO_WORK;
        k_sem_give(&da_command_sem);
    }
    else if (pins & BIT(da_buttons[1].pin))
    {
        da_command = DA_CMD_SYSTEM_OFF_SLEEP;
        k_sem_give(&da_command_sem);
    }
}

/* Prevent deep sleep (system off) from being entered on long timeouts
 * or `K_FOREVER` due to the default residency policy.
 *
 * This has to be done before anything tries to sleep, which means
 * before the threading system starts up between PRE_KERNEL_2 and
 * POST_KERNEL.  Do it at the start of PRE_KERNEL_2.
 */
static int disable_ds_1(const struct device *dev)
{
    ARG_UNUSED(dev);

    pm_policy_state_lock_get(PM_STATE_SOFT_OFF, PM_ALL_SUBSTATES);
    return 0;
}

SYS_INIT(disable_ds_1, PRE_KERNEL_2, 0);

int main(void)
{
    int err;

    /* Initialize buttons */

    for (size_t ii = 0; ii < ARRAY_SIZE(da_buttons); ii++)
    {
        if (!device_is_ready(da_buttons[ii].port))
        {
            printk("Buttons not ready\n");
            return 1;
        }
    }

    for (size_t ii = 0; ii < ARRAY_SIZE(da_buttons); ii++)
    {
        gpio_flags_t flags =
            da_buttons[ii].dt_flags & GPIO_ACTIVE_LOW ?
            GPIO_PULL_UP : GPIO_PULL_DOWN;
        err = gpio_pin_configure_dt(&da_buttons[ii], GPIO_INPUT | flags);

        if (err)
        {
            printk("Cannot configure button gpio (err %d)\n", err);
            return 1;
        }
    }

    uint32_t pin_mask = 0;

    for (size_t ii = 0; ii < ARRAY_SIZE(da_buttons); ii++)
    {

        err = gpio_pin_interrupt_configure_dt(&da_buttons[ii],
                              GPIO_INT_EDGE_TO_ACTIVE);
        if (err)
        {
            printk("Cannot disable callbacks (err %d)\n", err);
            return 1;
        }

        pin_mask |= BIT(da_buttons[ii].pin);
    }

    gpio_init_callback(&da_gpio_cb_info, button_pressed, pin_mask);

    for (size_t ii = 0; ii < ARRAY_SIZE(da_buttons); ii++)
    {
        err = gpio_add_callback(da_buttons[ii].port, &da_gpio_cb_info);
        if (err)
        {
            printk("Cannot add callback (err %d)\n", err);
            return 1;
        }
    }

    /* Red LED initialization */
    if (r_led.port && !device_is_ready(r_led.port)) {
        printk("LED device %s is not ready; ignoring it\n", r_led.port->name);
        r_led.port = NULL;
    }
    if (r_led.port) {
        err = gpio_pin_configure_dt(&r_led, GPIO_OUTPUT_INACTIVE);
        if (err) {
            printk("Error %d: failed to configure LED device %s pin %d\n",
                   err, r_led.port->name, r_led.pin);
            r_led.port = NULL;
        } else {
            printk("Set up LED at %s pin %d\n", r_led.port->name, r_led.pin);
        }
    }

    /* Green LED initialization */
    if (g_led.port && !device_is_ready(g_led.port)) {
        printk("LED device %s is not ready; ignoring it\n", g_led.port->name);
        g_led.port = NULL;
    }
    if (g_led.port) {
        err = gpio_pin_configure_dt(&g_led, GPIO_OUTPUT_INACTIVE);
        if (err) {
            printk("Error %d: failed to configure LED device %s pin %d\n",
                   err, g_led.port->name, g_led.pin);
            g_led.port = NULL;
        } else {
            printk("Set up LED at %s pin %d\n", g_led.port->name, g_led.pin);
        }
    }

    /* Blue LED initialization */
    if (b_led.port && !device_is_ready(b_led.port)) {
        printk("LED device %s is not ready; ignoring it\n", b_led.port->name);
        b_led.port = NULL;
    }
    if (b_led.port) {
        err = gpio_pin_configure_dt(&b_led, GPIO_OUTPUT_INACTIVE);
        if (err) {
            printk("Error %d: failed to configure LED device %s pin %d\n",
                   err, b_led.port->name, b_led.pin);
            b_led.port = NULL;
        } else {
            printk("Set up LED at %s pin %d\n", b_led.port->name, b_led.pin);
        }
    }

    /* Put the flash into deep power down mode
     * Note. The init will fail if the system is reset without power loss.
     * I believe the init fails because the chip is still in dpd mode. I also
     * think this is why Zephyr is having issues.
     * The chip seems to stay in dpd and is OK after another reset or full power
     * cycle.
     * Since the errors are ignored I removed the checks.
     */
    da_flash_init();
    da_flash_command(0xB9);
    da_flash_uninit();

    /* Flash green to see poweron, reset, and wake from system_off */
    gpio_pin_set_dt(&g_led, 1);
    k_sleep(K_MSEC(1000));
    gpio_pin_set_dt(&g_led, 0);

    k_sem_init(&da_command_sem, 0, 1);
    da_command = DA_CMD_DO_NOTHING;

    /* Loop forever processing commands */
    while (true)
    {
        printk("Waiting for a command\n");
        k_sem_take(&da_command_sem, K_FOREVER);

        if (da_command == DA_CMD_DO_WORK)
        {
            printk("Doing work\n");

            da_work_LED_state = !da_work_LED_state;
            gpio_pin_set_dt(&b_led, da_work_LED_state);
        }
        else if (da_command == DA_CMD_SYSTEM_OFF_SLEEP)
        {
            printk("Going to deep sleep\n");

            /* Flash red to know deep sleep starting */
            gpio_pin_set_dt(&r_led, 1);
            k_sleep(K_MSEC(1000));
            gpio_pin_set_dt(&r_led, 0);

            /* Force entry to deep sleep on any delay. */
            // pm_state_force(0u, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
            // pm_system_suspend(PM_STATE_SOFT_OFF);
            // struct pm_state_info state = { .state = PM_STATE_SOFT_OFF };
            // pm_device_action_run(NULL, PM_DEVICE_ACTION_SUSPEND);
            // pm_state_set(&state);  // Put system into deep sleep
            pm_policy_state_lock_get(PM_STATE_SOFT_OFF, PM_ALL_SUBSTATES);

            k_sleep(K_SECONDS(2U)); /* Sleep time doesn't matter */

            /* Execution should never reach here */
        }
    }
    return 0;
}