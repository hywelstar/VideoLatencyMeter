#include "app_log.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static log_level_t current_level = LOG_INFO;
static const char * const level_name[] = { "ERROR", "WARN", "INFO", "DEBUG" };

#define LOG_TX_BUFFER_SIZE 2048U

static uint8_t tx_buffer[LOG_TX_BUFFER_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static volatile uint16_t tx_in_flight;
static volatile uint8_t tx_active;
static volatile uint32_t dropped_count;

static void tx_start_locked(void)
{
    uint16_t length;
    if (tx_active != 0U || tx_head == tx_tail) return;
    length = tx_head > tx_tail ? (uint16_t)(tx_head - tx_tail) :
                                  (uint16_t)(LOG_TX_BUFFER_SIZE - tx_tail);
    tx_in_flight = length;
    tx_active = 1U;
    if (HAL_UART_Transmit_DMA(&huart1, &tx_buffer[tx_tail], length) != HAL_OK) {
        /* Keep queued data for the next write to retry; never block measurement. */
        tx_active = 0U;
        tx_in_flight = 0U;
    }
}

static void tx_enqueue(const uint8_t *data, uint16_t length)
{
    uint32_t primask = __get_PRIMASK();
    uint16_t used;
    uint16_t free_space;
    uint16_t i;
    __disable_irq();
    used = tx_head >= tx_tail ? (uint16_t)(tx_head - tx_tail) :
                                 (uint16_t)(LOG_TX_BUFFER_SIZE - tx_tail + tx_head);
    free_space = (uint16_t)(LOG_TX_BUFFER_SIZE - used - 1U);
    if (length > free_space) {
        ++dropped_count;
    } else {
        for (i = 0U; i < length; ++i) {
            tx_buffer[tx_head] = data[i];
            tx_head = (uint16_t)((tx_head + 1U) % LOG_TX_BUFFER_SIZE);
        }
        tx_start_locked();
    }
    __set_PRIMASK(primask);
}

void app_log_init(void)
{
    tx_head = 0U;
    tx_tail = 0U;
    tx_in_flight = 0U;
    tx_active = 0U;
    dropped_count = 0U;
}
void app_log_set_level(log_level_t level) { current_level = level; }
log_level_t app_log_get_level(void) { return current_level; }
uint32_t app_log_dropped_count(void) { return dropped_count; }

void app_log_uart_tx_complete(void)
{
    tx_tail = (uint16_t)((tx_tail + tx_in_flight) % LOG_TX_BUFFER_SIZE);
    tx_in_flight = 0U;
    tx_active = 0U;
    tx_start_locked();
}

void app_log_raw(const char *text)
{
    char line[256];
    int written = snprintf(line, sizeof(line), "%s\r\n", text);
    if (written <= 0) return;
    if ((size_t)written >= sizeof(line)) {
        line[sizeof(line) - 3U] = '\r';
        line[sizeof(line) - 2U] = '\n';
        line[sizeof(line) - 1U] = '\0';
        written = (int)strlen(line);
    }
    tx_enqueue((const uint8_t *)line, (uint16_t)written);
}

void app_log_write(log_level_t level, const char *module, const char *format, ...)
{
    char text[192];
    char line[256];
    va_list args;
    if (level > current_level || level > LOG_DEBUG) return;
    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    (void)snprintf(line, sizeof(line), "[%08lu][%s][%s] %s\r\n",
                   HAL_GetTick(), level_name[level], module, text);
    tx_enqueue((const uint8_t *)line, (uint16_t)strlen(line));
}
