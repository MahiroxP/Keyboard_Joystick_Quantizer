SRC += matrix.c
SRC += drivers/pico/ws2812.c
# drivers/pico/ws2812.c includes "atomic_util.h" by bare filename; this
# repo has both a generic tmk_core/common/atomic_util.h and an RP2040-
# specific tmk_core/common/pico/atomic_util.h (defines __interrupt_disable__
# / __interrupt_enable__ via hardware/sync.h), and only the latter works on
# this MCU, so force it ahead of the generic one in the include search path.
EXTRAINCDIRS += tmk_core/common/pico
# MCU name
MCU_FAMILY = PICO
MCU_SERIES = RP2040
MCU = cortex-m0plus

PICO_FLASH_SPI_CLKDIV = 8

CUSTOM_MATRIX = lite
VIA_ENABLE = yes
POINTING_DEVICE_ENABLE = yes

include users/sekigon/host_os_eeconfig/rules.mk
include users/sekigon/use_layer_as_combo_config/rules.mk
