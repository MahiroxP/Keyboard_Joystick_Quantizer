/* Copyright 2021 sekigon-gonnoc
 * Copyright 2026 MahiroxP
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "keyboard_quantizer.h"
#include "mini.h"
#include "report_descriptor_parser.h"
#include "report_parser.h"

#include <string.h>

#include "quantum.h"
#include "debug.h"
#include "process_combo.h"
#include "via.h"
#include "eeprom.h"

#include "host_os_eeconfig.h"
#include "use_layer_as_combo_config.h"

#include "pico_cdc.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/watchdog.h"
#include "hardware/exception.h"
#include "hardware/adc.h"
#include "pico/multicore.h"
#include "cdc_device.h"
#include "tusb.h"
#include "pio_usb.h"
#include "pointing_device.h"
#include "ws2812.h"

#include "RP2040.h"
#include "core_cm0plus.h"

#define LEN(x) (sizeof(x) / sizeof(x[0]))

extern uint8_t device_cnt;
extern uint8_t hid_info_cnt;

keyboard_config_t keyboard_config;

static volatile bool core1_active;
static volatile bool core1_stop_trigger;
static volatile bool core1_start_trigger;

void __not_in_flash_func(core1_main)(void) {
    core1_active = true;

    // Use tuh_configure() to pass pio configuration to the host stack
    // Note: tuh_configure() must be called before
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = 4;
    pio_cfg.extra_error_retry_count = 10;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
    // port1) on core1
    tuh_init(1);

    bool     core1_task_active = true;
    uint32_t interrupt         = 0;

    while (1) {
        if (core1_task_active) tuh_task();  // tinyusb host task

        if (core1_stop_trigger) {
            interrupt         = save_and_disable_interrupts();
            core1_task_active = false;
            core1_stop_trigger = false;
        } else if (core1_start_trigger) {
            restore_interrupts(interrupt);
            core1_task_active = true;
            core1_start_trigger = false;
        }
    }
}

void pico_before_flash_operation(void) {
    if (!core1_active) return;
    core1_stop_trigger = true;
    while (core1_stop_trigger) {
        continue;
    }
}

void pico_after_flash_operation(void) {
    if (!core1_active) return;
    core1_start_trigger = true;
    while (core1_start_trigger) {
        continue;
    }
}

void pico_cdc_receive_cb(uint8_t const *buf, uint32_t cnt) {
    if (cnt > 0) {
        printf("%c\n", buf[0]);

        switch (buf[0]) {
            case 'd':
                if (debug_enable) {
                    puts("Disable debug output\n");
                    debug_enable = false;
                } else {
                    puts("Enable debug output\n");
                    debug_enable = true;
                }
                break;
            case 'b':
                bootloader_jump();
                break;
        }
    }
}

void hardfault_handler(void) {
    bootloader_jump();
}

void keyboard_post_init_kb_rev(void) {
    debug_enable = false;

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hardfault_handler);
    watchdog_hw->scratch[0] = 0; // disable bootloader jump by watchdog

    if (keyboard_config.layer_to_combo) {
        convert_layer_to_combo();
    } else {
        combo_disable();
    }

    keyboard_post_init_user();
}

void dynamic_keymap_reset() {
    for (int idx = 0; idx < DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_COLS_DEFAULT *
                                MATRIX_ROWS_DEFAULT;
         idx++) {
        const uint8_t layer = idx / (MATRIX_COLS_DEFAULT * MATRIX_ROWS_DEFAULT);
        const uint8_t offset =
            idx % (MATRIX_COLS_DEFAULT * MATRIX_ROWS_DEFAULT);
        const uint8_t row = offset / MATRIX_COLS_DEFAULT;
        const uint8_t col = offset % MATRIX_COLS_DEFAULT;

        if (row < MATRIX_MODIFIER_ROW) {
            dynamic_keymap_set_keycode(
                layer, row, col,
                idx % (MATRIX_COLS_DEFAULT * MATRIX_ROWS_DEFAULT));
        } else if (row == MATRIX_MODIFIER_ROW) {
            dynamic_keymap_set_keycode(layer, row, col, KC_LCTRL + col);
        } else if (row == MATRIX_MSBTN_ROW) {
            dynamic_keymap_set_keycode(layer, row, col, KC_BTN1 + col);
        } else if (row == MATRIX_MSGES_ROW) {
            if (col < MATRIX_MSWHEEL_COL) {
                dynamic_keymap_set_keycode(layer, row, col, KC_NO);
            } else {
                dynamic_keymap_set_keycode(
                    layer, row, col, KC_MS_WH_UP + col - MATRIX_MSWHEEL_COL);
            }
        }
    }
}

uint8_t  encoder_modifier            = 0;
uint16_t encoder_modifier_pressed_ms = 0;
bool     is_encoder_action           = false;
int      reset_flag                  = 0;

#ifndef ENCODER_MODIFIER_TIMEOUT_MS
#    define ENCODER_MODIFIER_TIMEOUT_MS 500
#endif

void on_host_os_eeconfig_update(void) {
    keyboard_config.raw = eeconfig_read_kb();
    set_key_override(keyboard_config.override_mode);
}

void eeconfig_init_kb(void) {
    if (keyboard_config.os_eeconfig) {
        host_os_eeconfig_init();
    }

    keyboard_config.raw = 0;
    eeconfig_update_kb(keyboard_config.raw);

    eeconfig_init_user();
}

static void debug_led_flash(void);

#ifdef ANALOG_JOYSTICK_ENABLE
// Logical center for each axis, calibrated from the resting position at
// power-on (see analog_joystick_init) since the physical joystick's true
// center can drift from the nominal ANALOG_JOYSTICK_ADC_CENTER.
static int32_t joystick_center_x = ANALOG_JOYSTICK_ADC_CENTER;
static int32_t joystick_center_y = ANALOG_JOYSTICK_ADC_CENTER;

// RP2040's ADC is 12bit, but this joystick is specified against a 10bit ADC
// (center=512), so the raw reading is rescaled down to that 10bit range.
static int32_t analog_joystick_read_10bit(uint8_t pin) {
    adc_select_input(pin - 26);
    return (int32_t)(adc_read() >> 2);
}

static void analog_joystick_init(void) {
    adc_init();
    adc_gpio_init(ANALOG_JOYSTICK_X_PIN);
    adc_gpio_init(ANALOG_JOYSTICK_Y_PIN);

    joystick_center_x = analog_joystick_read_10bit(ANALOG_JOYSTICK_X_PIN);
    joystick_center_y = analog_joystick_read_10bit(ANALOG_JOYSTICK_Y_PIN);
}

// The quadratic term is scaled up by SUBPIXEL_SCALE and any sub-1 remainder
// is carried over to the next call (via *carry), so slow overall speeds
// don't get lost to integer truncation of the mid-range of stick travel.
#define ANALOG_JOYSTICK_SUBPIXEL_SCALE 256

static int32_t analog_joystick_read_raw(uint8_t pin, int32_t center) {
    return analog_joystick_read_10bit(pin) - center;
}

// Quadratic response (fine control near center, disproportionately faster
// the further the stick is tilted), further multiplied by accel_scale (the
// hold-time ramp computed in analog_joystick_task).
static int32_t analog_joystick_magnitude(int32_t deflection, float accel_scale) {
    int32_t max_value = ANALOG_JOYSTICK_ADC_CENTER - ANALOG_JOYSTICK_ADC_DEADZONE;
    if (deflection > max_value) {
        deflection = max_value;
    }

    int32_t subpixels = (deflection * deflection * ANALOG_JOYSTICK_SUBPIXEL_SCALE) / max_value;
    return (int32_t)(subpixels * accel_scale);
}

static int8_t analog_joystick_step(int32_t magnitude, int32_t sign, int32_t *carry) {
    *carry += sign * magnitude;

    int32_t unit = ANALOG_JOYSTICK_ADC_DIVISOR * ANALOG_JOYSTICK_SUBPIXEL_SCALE;
    int32_t step = *carry / unit;
    *carry -= step * unit;

    if (step > 127) {
        step = 127;
    } else if (step < -127) {
        step = -127;
    }

    return (int8_t)step;
}

// Rotates a raw (x, y) deflection by ANALOG_JOYSTICK_LAYOUT degrees
// (clockwise, multiple of 45) to compensate for the joystick's physical
// mounting angle. cos/sin are fixed-point (*256); 181/256 = 0.70703125
// approximates 1/sqrt(2), exact enough for integer ADC deflection.
static void analog_joystick_rotate(int32_t *x, int32_t *y) {
    static const int32_t cos256[8] = {256, 181, 0, -181, -256, -181, 0, 181};
    static const int32_t sin256[8] = {0, 181, 256, 181, 0, -181, -256, -181};

    uint8_t idx = (uint8_t)((ANALOG_JOYSTICK_LAYOUT / 45) % 8);
    int32_t c   = cos256[idx];
    int32_t s   = sin256[idx];
    int32_t rx  = *x;
    int32_t ry  = *y;

    *x = (rx * c - ry * s) >> 8;
    *y = (rx * s + ry * c) >> 8;
}

static void analog_joystick_task(void) {
    static int32_t  x_carry        = 0;
    static int32_t  y_carry        = 0;
    static uint16_t hold_start_ms  = 0;
    static bool     holding        = false;
    static int32_t  still_ref_x    = 0;
    static int32_t  still_ref_y    = 0;
    static uint16_t still_start_ms = 0;
    static bool     still_tracking = false;

    int32_t raw_x = analog_joystick_read_raw(ANALOG_JOYSTICK_X_PIN, joystick_center_x);
    int32_t raw_y = analog_joystick_read_raw(ANALOG_JOYSTICK_Y_PIN, joystick_center_y);

    // Re-center whenever the tilt has held steady (within tolerance) for
    // ANALOG_JOYSTICK_RECENTER_MS, treating that position as the new
    // "stopped" state.
    int32_t still_diff_x = raw_x - still_ref_x;
    int32_t still_diff_y = raw_y - still_ref_y;
    if (still_diff_x < 0) still_diff_x = -still_diff_x;
    if (still_diff_y < 0) still_diff_y = -still_diff_y;

    if (!still_tracking || still_diff_x > ANALOG_JOYSTICK_RECENTER_TOLERANCE ||
        still_diff_y > ANALOG_JOYSTICK_RECENTER_TOLERANCE) {
        still_tracking = true;
        still_ref_x    = raw_x;
        still_ref_y    = raw_y;
        still_start_ms = timer_read();
    } else if (timer_elapsed(still_start_ms) >= ANALOG_JOYSTICK_RECENTER_MS) {
        joystick_center_x += raw_x;
        joystick_center_y += raw_y;
        raw_x          = 0;
        raw_y          = 0;
        still_ref_x    = 0;
        still_ref_y    = 0;
        still_tracking = false;
    }

    // Recentering above must stay in the physical ADC coordinate space, so
    // the mounting-angle rotation is applied after it, only affecting the
    // direction reported for cursor movement below.
    analog_joystick_rotate(&raw_x, &raw_y);

    int32_t sign_x = raw_x < 0 ? -1 : 1;
    int32_t sign_y = raw_y < 0 ? -1 : 1;
    int32_t dev_x  = raw_x < 0 ? -raw_x : raw_x;
    int32_t dev_y  = raw_y < 0 ? -raw_y : raw_y;

    bool active = (dev_x >= ANALOG_JOYSTICK_ADC_DEADZONE) || (dev_y >= ANALOG_JOYSTICK_ADC_DEADZONE);

    if (active) {
        if (!holding) {
            holding       = true;
            hold_start_ms = timer_read();
        }
    } else {
        holding = false;
    }

    // Ramp up from a slow start (fine movements) the longer the stick stays
    // tilted, reaching the previously-tuned max speed only after being held
    // for ANALOG_JOYSTICK_ACCEL_RAMP_MS.
    const float min_scale = 1.0f / ANALOG_JOYSTICK_ACCEL_START_SCALE;
    float       accel_scale = min_scale;
    if (holding) {
        uint16_t held_ms = timer_elapsed(hold_start_ms);
        if (held_ms >= ANALOG_JOYSTICK_ACCEL_RAMP_MS) {
            accel_scale = 1.0f;
        } else {
            float progress = (float)held_ms / ANALOG_JOYSTICK_ACCEL_RAMP_MS;
            accel_scale     = min_scale + progress * (1.0f - min_scale);
        }
    }

    int8_t dx = 0;
    int8_t dy = 0;
    if (dev_x >= ANALOG_JOYSTICK_ADC_DEADZONE) {
        int32_t magnitude = analog_joystick_magnitude(dev_x - ANALOG_JOYSTICK_ADC_DEADZONE, accel_scale);
        dx                = analog_joystick_step(magnitude, sign_x, &x_carry);
    }
    if (dev_y >= ANALOG_JOYSTICK_ADC_DEADZONE) {
        int32_t magnitude = analog_joystick_magnitude(dev_y - ANALOG_JOYSTICK_ADC_DEADZONE, accel_scale);
        dy                = analog_joystick_step(magnitude, sign_y, &y_carry);
    }

    if (dx != 0 || dy != 0) {
        report_mouse_t mouse = pointing_device_get_report();
        mouse.x += dx;
        mouse.y += -dy;  // invert vertical axis
        pointing_device_set_report(mouse);
        pointing_device_send();

        debug_led_flash();
    }
}
#endif

// Onboard WS2812 LED flashed briefly on any keyboard input or joystick
// movement, for visually confirming that input is being registered. Each
// flash steps to the next hue so consecutive flashes cycle through the
// rainbow. Driven directly (not via the RGBLIGHT_ENABLE feature, so no
// dependency on quantum/color.c's hsv_to_rgb) since this is just a debug
// indicator, not a lighting effect.
#define DEBUG_LED_FLASH_MS 80
#define DEBUG_LED_VAL 32
#define DEBUG_LED_HUE_STEP 16  // 256 / 16 = 16 distinct hues per revolution

static uint16_t debug_led_timer  = 0;
static bool     debug_led_active = false;
static uint8_t  debug_led_hue    = 0;

// Minimal full-saturation HSV->RGB (region-based hue wheel), avoiding a
// dependency on quantum/color.c (which isn't otherwise built here).
static LED_TYPE debug_led_hue_to_rgb(uint8_t hue, uint8_t val) {
    uint8_t region    = hue / 43;        // 0..5 (256/6 ~= 43)
    uint8_t remainder  = (hue % 43) * 6;  // 0..255
    uint8_t q          = (uint8_t)(((uint16_t)val * (255 - remainder)) >> 8);
    uint8_t t          = (uint8_t)(((uint16_t)val * remainder) >> 8);

    switch (region) {
        case 0:
            return (LED_TYPE){.r = val, .g = t, .b = 0};
        case 1:
            return (LED_TYPE){.r = q, .g = val, .b = 0};
        case 2:
            return (LED_TYPE){.r = 0, .g = val, .b = t};
        case 3:
            return (LED_TYPE){.r = 0, .g = q, .b = val};
        case 4:
            return (LED_TYPE){.r = t, .g = 0, .b = val};
        default:
            return (LED_TYPE){.r = val, .g = 0, .b = q};
    }
}

static void debug_led_flash(void) {
    LED_TYPE led = debug_led_hue_to_rgb(debug_led_hue, DEBUG_LED_VAL);
    debug_led_hue += DEBUG_LED_HUE_STEP;

    ws2812_setleds(&led, 1);
    debug_led_timer  = timer_read();
    debug_led_active = true;
}

static void debug_led_task(void) {
    if (debug_led_active && timer_elapsed(debug_led_timer) >= DEBUG_LED_FLASH_MS) {
        LED_TYPE led = {0};
        ws2812_setleds(&led, 1);
        debug_led_active = false;
    }
}

void matrix_init_kb(void) {
    keyboard_config.raw = eeconfig_read_kb();
    set_key_override(keyboard_config.override_mode);

#ifdef ANALOG_JOYSTICK_ENABLE
    analog_joystick_init();
#endif

    matrix_init_user();
}

void matrix_scan_kb(void) {
    if (encoder_modifier != 0 && timer_elapsed(encoder_modifier_pressed_ms) >
                                     ENCODER_MODIFIER_TIMEOUT_MS) {
        unregister_mods(encoder_modifier);
        encoder_modifier = 0;
    }

    if (keyboard_config.os_eeconfig) {
        host_os_eeconfig_update(get_usb_host_os_type());
    }

    if (reset_flag > 0) {
        if (--reset_flag == 0) {
            // Reset USB to detect host os properly after reset
            reset_block(RESETS_RESET_USBCTRL_BITS);
            // Reset MCU
            __NVIC_SystemReset();
        }
    }

#ifdef ANALOG_JOYSTICK_ENABLE
    analog_joystick_task();
#endif

    debug_led_task();

    matrix_scan_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        debug_led_flash();
    }

    if (encoder_modifier != 0 && !is_encoder_action) {
        unregister_mods(encoder_modifier);
        encoder_modifier = 0;
    }

    switch (keycode) {
        case QK_MODS ... QK_MODS_MAX:
            if (is_encoder_action) {
                if (record->event.pressed) {
                    uint8_t current_mods        = keycode >> 8;
                    encoder_modifier_pressed_ms = timer_read();
                    if (current_mods != encoder_modifier) {
                        del_mods(encoder_modifier);
                        encoder_modifier = current_mods;
                        add_mods(encoder_modifier);
                    }
                    register_code(keycode & 0xff);
                } else {
                    unregister_code(keycode & 0xff);
                }
                return false;
            } else {
                return true;
            }
            break;
    }

    if (record->event.pressed) {
        switch (keycode) {
            case DISABLE_KEY_OVERRIDES:
            case KEY_OVERRIDE_OFF: {
                println("Disable key overrides");
                keyboard_config.override_mode = DISABLE_OVERRIDE;
                set_key_override(DISABLE_OVERRIDE);
                eeconfig_update_kb(keyboard_config.raw);
                return true;
            }
            case ENABLE_US_KEY_ON_JP_OS_OVERRIDE: {
                println(
                    "Perform as an US keyboard on the OS configured for JP");
                keyboard_config.override_mode = US_KEY_ON_JP_OS_OVERRIDE;
                set_key_override(US_KEY_ON_JP_OS_OVERRIDE);
                eeconfig_update_kb(keyboard_config.raw);
                return false;
            } break;
            case ENABLE_JP_KEY_ON_US_OS_OVERRIDE: {
                println("Perform as a JP keyboard on the OS configured for US");
                keyboard_config.override_mode = JP_KEY_ON_US_OS_OVERRIDE;
                set_key_override(JP_KEY_ON_US_OS_OVERRIDE);
                eeconfig_update_kb(keyboard_config.raw);
                return false;
            } break;
            case CMB_ON:
            case CMB_OFF:
            case CMB_TOG: {
                if (keyboard_config.layer_to_combo) {
                    convert_layer_to_combo();
                }
                return true;
            } break;
        }
    }

    return process_record_user(keycode, record);
}

enum via_keyboard_value_id_kb { id_keyboard_quantizer = 0x99 };

enum via_kq_value_id_kb {
    id_config_ver = 0x01,
    id_bootloader,
    id_reset,
    id_eeprom,   // read/write eeprom
    id_host_os,  // get os
};

static void raw_hid_get_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);

    switch (*command_id) {
        case id_config_ver: {
            command_data[0] = 0x01;
        } break;
        case id_eeprom: {
            uint32_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            size            = size < (length - 4) ? size : (length - 4);
            eeprom_read_block(&command_data[3], (const void *)offset, size);
        } break;
        case id_host_os: {
            command_data[0] = get_usb_host_os_type();
        }
    }
}

static void raw_hid_set_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);

    switch (*command_id) {
        case id_bootloader: {
            bootloader_jump();
        } break;
        case id_reset: {
            reset_flag = 100;
        } break;
        case id_eeprom: {
            uint32_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            size            = size < (length - 4) ? size : (length - 4);
            eeprom_update_block(&command_data[3], (void *)offset, size);
        } break;
    }
}

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);
    switch (*command_id) {
        case id_get_keyboard_value: {
            if (command_data[0] == id_keyboard_quantizer) {
                raw_hid_get_kb(data + 2, length - 2);
            }
        } break;
        case id_set_keyboard_value: {
            if (command_data[0] == id_keyboard_quantizer) {
                raw_hid_set_kb(data + 2, length - 2);
            }
        } break;
    }
}
