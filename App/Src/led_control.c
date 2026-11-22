#include "led_control.h"
#include "main.h"

static bool led_is_on;

void led_control_init(void) { led_control_set(false); }
void led_control_set(bool on)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    led_is_on = on;
}
void led_control_toggle(void) { led_control_set(!led_is_on); }
bool led_control_is_on(void) { return led_is_on; }
