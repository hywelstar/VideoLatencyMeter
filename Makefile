PROJECT := VideoLatencyMeter
BUILD := build
TOOLCHAIN ?= arm-none-eabi-
CC := $(TOOLCHAIN)gcc
OBJCOPY := $(TOOLCHAIN)objcopy
SIZE := $(TOOLCHAIN)size

VENDOR := third_party/STM32CubeF4-min/Drivers
INCLUDES := -ICore/Inc -IApp/Inc -I$(VENDOR)/STM32F4xx_HAL_Driver/Inc -I$(VENDOR)/CMSIS/Include -I$(VENDOR)/CMSIS/Device/ST/STM32F4xx/Include
CFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -DSTM32F407xx -DUSE_HAL_DRIVER -DHSE_VALUE=8000000U $(INCLUDES) -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections -Og -g3
LDFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -TSTM32F407ZETx_FLASH.ld -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

SOURCES := $(wildcard Core/Src/*.c) $(wildcard App/Src/*.c) \
  $(VENDOR)/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c \
  $(VENDOR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/Core/Startup/startup_stm32f407xx.o

.PHONY: all clean size
all: $(BUILD)/$(PROJECT).elf $(BUILD)/$(PROJECT).hex $(BUILD)/$(PROJECT).bin size

$(BUILD)/$(PROJECT).elf: $(OBJECTS) STM32F407ZETx_FLASH.ld
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(OBJECTS) $(LDFLAGS) -Wl,-Map,$(BUILD)/$(PROJECT).map -o $@

$(BUILD)/%.o: %.c
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/Core/Startup/startup_stm32f407xx.o: Core/Startup/startup_stm32f407xx.s
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PROJECT).hex: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(PROJECT).elf
	$(SIZE) $<

clean:
	@if exist "$(BUILD)" rmdir /S /Q "$(BUILD)"

-include $(OBJECTS:.o=.d)
