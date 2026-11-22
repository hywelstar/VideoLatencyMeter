#include "button.h"
#include "app_log.h"
#include "main.h"

#define BUTTON_DEBOUNCE_MS 30U
#define BUTTON_LONG_PRESS_MS 2000U
#define BUTTON_ACTIVE_LOW 1U

static uint8_t stable_pressed;
static uint8_t sampled_pressed;
static uint8_t long_reported;
static uint32_t sample_changed_ms;
static uint32_t pressed_ms;

static uint8_t button_is_pressed(void)
{
    GPIO_PinState level = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3);
#if BUTTON_ACTIVE_LOW
    return level == GPIO_PIN_RESET;
#else
    return level == GPIO_PIN_SET;
#endif
}

void button_init(void)
{
    sampled_pressed = button_is_pressed();
    stable_pressed = sampled_pressed;
    sample_changed_ms = HAL_GetTick();
    pressed_ms = HAL_GetTick();
    long_reported = 0U;
}

button_event_t button_poll(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t pressed = button_is_pressed();

    if (pressed != sampled_pressed) {
        sampled_pressed = pressed;
        sample_changed_ms = now;
    }
    if (sampled_pressed != stable_pressed &&
        (uint32_t)(now - sample_changed_ms) >= BUTTON_DEBOUNCE_MS) {
        stable_pressed = sampled_pressed;
        if (stable_pressed != 0U) {
            pressed_ms = now;
            long_reported = 0U;
            LOGI("KEY", "K1 pressed (PE3=LOW)");
        } else if (long_reported == 0U) {
            LOGI("KEY", "K1 released: short press");
            return BUTTON_EVENT_SHORT_PRESS;
        } else {
            LOGI("KEY", "K1 released after long press");
        }
    }
    if (stable_pressed != 0U && long_reported == 0U &&
        (uint32_t)(now - pressed_ms) >= BUTTON_LONG_PRESS_MS) {
        long_reported = 1U;
        LOGI("KEY", "K1 held for 2s: long press");
        return BUTTON_EVENT_LONG_PRESS;
    }
    return BUTTON_EVENT_NONE;
}
