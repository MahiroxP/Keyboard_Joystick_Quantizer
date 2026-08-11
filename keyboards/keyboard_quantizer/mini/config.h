
#pragma once

#define DYNAMIC_KEYMAP_LAYER_COUNT 10
#define VIA_EEPROM_MAGIC_ADDR (EECONFIG_SIZE * 5 + 1)

#undef PRODUCT_ID
#define PRODUCT_ID 0x999b

#define PICO_SYSTEM_CLOCK_KHZ 120000
#define PICO_PIO_USB_HOST_ENABLE

/* Analog joystick used as an additional pointing device (no button) */
#define ANALOG_JOYSTICK_ENABLE
#define ANALOG_JOYSTICK_X_PIN 26
#define ANALOG_JOYSTICK_Y_PIN 27
#define ANALOG_JOYSTICK_ADC_CENTER 512
#define ANALOG_JOYSTICK_ADC_DEADZONE 90
#define ANALOG_JOYSTICK_ADC_DIVISOR 640
// If the stick reports the same tilt continuously for this long, that tilt
// is treated as the new "stopped" position (re-centers to compensate for
// the physical resting point drifting over time).
#define ANALOG_JOYSTICK_RECENTER_MS 4000
// ADC counts of wiggle room when judging whether the tilt is "the same",
// to tolerate ADC noise.
#define ANALOG_JOYSTICK_RECENTER_TOLERANCE 3
// How much slower than the tuned max speed movement starts at when the
// stick first leaves the deadzone (higher = slower start).
#define ANALOG_JOYSTICK_ACCEL_START_SCALE 240
// How long (ms) the stick needs to stay tilted before reaching the tuned
// max speed above.
#define ANALOG_JOYSTICK_ACCEL_RAMP_MS 4000
// Compensates for the joystick's physical mounting angle by rotating its
// output. Degrees, clockwise, must be a multiple of 45 in [0, 315].
#define ANALOG_JOYSTICK_LAYOUT 0

/* key matrix size */
#define MATRIX_ROWS 24
#define MATRIX_COLS 8
#define MATRIX_ROWS_DEFAULT MATRIX_ROWS
#define MATRIX_COLS_DEFAULT MATRIX_COLS
#define MATRIX_MODIFIER_ROW 21
#define MATRIX_MSBTN_ROW 22
#define MATRIX_MSGES_ROW 23
#define MATRIX_MSGES_ROW 23
#define MATRIX_MSWHEEL_ROW 23
#define MATRIX_MSWHEEL_COL 4
#define IS_LEFT_HAND true

#define OVERRIDE_KEYMAP_KEY_TO_KEYCODE
#define QUANTIZER_REPORT_PARSER REPORT_PARSER_DEFAULT

#define RGBLIGHT_SPLIT
#define RGB_DI_PIN 16  // onboard WS2812 LED (Waveshare RP2040-Zero), driven
                       // directly via ws2812_setleds() for debug input
                       // indication -- RGBLIGHT_ENABLE stays off, so the
                       // animation settings below are unused scaffolding
#ifdef RGB_DI_PIN
#    define RGBLED_NUM_DEFAULT 128
#    define RGBLIGHT_HUE_STEP 8
#    define RGBLIGHT_SAT_STEP 8
#    define RGBLIGHT_VAL_STEP 8
#    define RGBLIGHT_LIMIT_VAL 255 /* The maximum brightness level */
#    define RGBLIGHT_SLEEP /* If defined, the RGB lighting will be switched \
                              off when the host goes to sleep */
                           /*== all animations enable ==*/
#    define RGBLIGHT_ANIMATIONS
/*== or choose animations ==*/
#    define RGBLIGHT_EFFECT_BREATHING
#    define RGBLIGHT_EFFECT_RAINBOW_MOOD
#    define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#    define RGBLIGHT_EFFECT_SNAKE
#    define RGBLIGHT_EFFECT_KNIGHT
#    define RGBLIGHT_EFFECT_CHRISTMAS
#    define RGBLIGHT_EFFECT_STATIC_GRADIENT
#    define RGBLIGHT_EFFECT_RGB_TEST
#    define RGBLIGHT_EFFECT_ALTERNATING
/*== customize breathing effect ==*/
/*==== (DEFAULT) use fixed table instead of exp() and sin() ====*/
#    define RGBLIGHT_BREATHE_TABLE_SIZE 256  // 256(default) or 128 or 64
/*==== use exp() and sin() ====*/
#    define RGBLIGHT_EFFECT_BREATHE_CENTER 1.85  // 1 to 2.7
#    define RGBLIGHT_EFFECT_BREATHE_MAX 255      // 0 to 255
#endif
