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

// 配列の要素数を求めるヘルパーマクロ。呼び出し箇所は現状0件。
// Array element count helper. Call count is currently zero.
#define LEN(x) (sizeof(x) / sizeof(x[0]))

// TinyUSBホストスタックが認識しているUSBデバイス数/HIDインターフェース数。
// TinyUSBホストスタック側で定義されている。
// Number of connected USB devices/HID interfaces the TinyUSB host stack is
// tracking. Defined by the TinyUSB host stack.
extern uint8_t device_cnt;
extern uint8_t hid_info_cnt;

// This board's persistent (EEPROM-backed) settings; see keyboard_config_t
// in mini.h for the individual fields.
// このボードの永続設定(EEPROM保存)。各項目の説明はmini.hの
// keyboard_config_t参照。
keyboard_config_t keyboard_config;

// Coordination flags between the main core (core0, running QMK) and core1
// (running the TinyUSB/PIO-USB host stack, see core1_main below): whether
// core1 has started, and requests to pause/resume its USB task loop during
// flash writes (see pico_before/after_flash_operation).
// メインコア(core0、QMK本体が動く側)と、core1(TinyUSB/PIO-USBホスト
// スタックが動く側、下記core1_main参照)間の連携用フラグ。core1が起動
// 済みかどうか、およびフラッシュ書き込み中にcore1のUSBタスクループを
// 一時停止/再開させるための要求(pico_before/after_flash_operation参照)。
static volatile bool core1_active;
static volatile bool core1_stop_trigger;
static volatile bool core1_start_trigger;

// - Function/関数名称 ： core1_main
// - Purpose/機能 ： USBホスト処理をRP2040の第2コアで専任実行し、メインコアで動くQMK本体から独立してキーボード接続を処理し続ける機能。フラッシュ書き込み中も動作を継続する。 / Runs USB host processing on the RP2040's second CPU core, independent of the main core's QMK processing, keeping keyboard connections serviced continuously -- including through flash write operations.
// - Usage/呼出方法 ： call core1_main( void )
// - Return/戻り値 ： void(無限ループのため実行が戻るのはリセット時のみ)
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

// - Function/関数名称 ： pico_before_flash_operation
// - Purpose/機能 ： フラッシュ書き込み中の安全性を確保する機能。書き込みが始まる前に、RAM実行でないcore1側の処理を確実に静止させる。 / Ensures flash writes are safe by bringing core1's non-RAM-resident processing to a confirmed stop before the write begins.
// - Usage/呼出方法 ： call pico_before_flash_operation( void )
// - Return/戻り値 ： void
void pico_before_flash_operation(void) {
    if (!core1_active) return;
    core1_stop_trigger = true;
    while (core1_stop_trigger) {
        continue;
    }
}

// - Function/関数名称 ： pico_after_flash_operation
// - Purpose/機能 ： フラッシュ書き込み完了後にUSBホスト処理を通常運転へ戻す機能(pico_before_flash_operationの対)。 / Restores normal USB host processing after a flash write completes (the counterpart to pico_before_flash_operation).
// - Usage/呼出方法 ： call pico_after_flash_operation( void )
// - Return/戻り値 ： void
void pico_after_flash_operation(void) {
    if (!core1_active) return;
    core1_start_trigger = true;
    while (core1_start_trigger) {
        continue;
    }
}

// - Function/関数名称 ： pico_cdc_receive_cb
// - Purpose/機能 ： USBシリアル経由の1文字コマンドで、デバッグ出力の切替とブートローダーへの切り替えを行える簡易デバッグコンソール機能。 / A simple debug console over USB serial, driven by single-character commands: toggling debug output and switching into the bootloader.
// - Usage/呼出方法 ： call pico_cdc_receive_cb( uint8_t const *buf, uint32_t cnt )
// - Return/戻り値 ： void
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

// - Function/関数名称 ： hardfault_handler
// - Purpose/機能 ： 予期しない異常が発生した場合でも、ボードを復旧・再書き込み可能な状態に保つ機能。 / Keeps the board recoverable and reflashable even when an unexpected fault occurs.
// - Usage/呼出方法 ： call exception_set_exclusive_handler( HARDFAULT_EXCEPTION, hardfault_handler )
// - Return/戻り値 ： void(bootloader_jump()から戻らないため、実行が戻ることはない)
void hardfault_handler(void) {
    bootloader_jump();
}

// - Function/関数名称 ： keyboard_post_init_kb_rev
// - Purpose/機能 ： 起動時に必要な各種機能(USBホスト処理の開始、異常時の復旧手段の確保、ウォッチドッグリセットの正常化、レイヤー→コンボ変換設定の適用)をまとめて立ち上げる機能。 / Brings up the startup-time features this board needs: starting USB host processing, arming a fault-recovery path, normalizing watchdog-reset behavior, and applying the layer-to-combo setting.
// - Usage/呼出方法 ： call keyboard_post_init_kb_rev( void )
// - Return/戻り値 ： void
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

// - Function/関数名称 ： dynamic_keymap_reset
// - Purpose/機能 ： VIA/Remapで編集可能なキーマップに、そのまま使える初期配列(通常キー、モディファイア、マウスボタン、マウスホイール)を用意する機能。各行/列の役割はconfig.hのMATRIX_*定数で定義される。 / Populates the VIA/Remap-editable keymap with a ready-to-use initial layout (regular keys, modifiers, mouse buttons, mouse wheel). Each row/column's role is defined by the MATRIX_* constants in config.h.
// - Usage/呼出方法 ： call dynamic_keymap_reset( void )
// - Return/戻り値 ： void
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

// 「エンコーダー用アクション」修飾キー(process_record_kb内でis_encoder_action
// がtrueの間だけ処理されるQK_MODSキーコード)のための状態。現在保持中の
// 修飾キービット、最後に適用した時刻(下記の自動解除タイムアウト用)、および
// 現在のキーコードをエンコーダー用アクションとして扱うべきかどうか。
// is_encoder_actionはキーマップ側のコードが切り替える想定の外部インターフェース。
// State for "encoder action" modifier keys (QK_MODS keycodes processed
// specially while is_encoder_action is true, see process_record_kb below):
// which modifier bits are currently held for the encoder, when they were
// last applied (for the auto-release timeout below), and whether the
// current keycode should be treated as an encoder action at all.
// is_encoder_action is an external interface meant to be toggled by
// keymap-level code.
uint8_t  encoder_modifier            = 0;
uint16_t encoder_modifier_pressed_ms = 0;
bool     is_encoder_action           = false;
// Countdown (matrix_scan_kb ticks) until a pending soft reset (triggered
// via raw_hid, see id_reset below) actually fires, giving the HID response
// time to be sent before the USB controller resets.
// raw_hid経由(下記id_reset参照)で要求されたソフトリセットが実際に発火する
// までのカウントダウン(matrix_scan_kbが呼ばれるたびに減算)。USBコントローラ
// をリセットする前に、HID応答を送信する時間的猶予を持たせるため。
int      reset_flag                  = 0;

// How long (ms) an encoder-action modifier stays held before being
// automatically released if no matching key-up ever arrives. Range:
// positive integer, milliseconds.
// エンコーダー用アクションの修飾キーが、対応するキーアップが来ないまま
// 自動的に解除されるまでの保持時間(ms)。設定範囲: 正の整数(ミリ秒)。
#ifndef ENCODER_MODIFIER_TIMEOUT_MS
#    define ENCODER_MODIFIER_TIMEOUT_MS 500
#endif

// - Function/関数名称 ： on_host_os_eeconfig_update
// - Purpose/機能 ： 接続先OSの変化にJP/USキー上書き設定を追従させる機能。 / Keeps the JP/US key override setting in sync with the connected host OS.
// - Usage/呼出方法 ： call on_host_os_eeconfig_update( void )
// - Return/戻り値 ： void
void on_host_os_eeconfig_update(void) {
    keyboard_config.raw = eeconfig_read_kb();
    set_key_override(keyboard_config.override_mode);
}

// - Function/関数名称 ： eeconfig_init_kb
// - Purpose/機能 ： このボードの設定を初期状態にリセットする機能。ホストOS自動判別が有効な場合はその設定も併せて初期化する。 / Resets this board's settings to their initial state, including host-OS auto-detection's own settings when that feature is enabled.
// - Usage/呼出方法 ： call eeconfig_init_kb( void )
// - Return/戻り値 ： void
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

// - Function/関数名称 ： analog_joystick_read_10bit
// - Purpose/機能 ： ジョイスティックの傾きを、この機構が前提とする10bitスケールで取得する機能。 / Reads the joystick's tilt on the 10bit scale this mechanism is designed around.
// - Usage/呼出方法 ： call analog_joystick_read_10bit( uint8_t pin )
// - Return/戻り値 ： int32_t adc_value
static int32_t analog_joystick_read_10bit(uint8_t pin) {
    adc_select_input(pin - 26);
    return (int32_t)(adc_read() >> 2);
}

// - Function/関数名称 ： analog_joystick_init
// - Purpose/機能 ： アナログジョイスティックを使用可能な状態にする初期化機能。個体差のあるセンター位置を起動時に実測して較正する。 / Prepares the analog joystick for use, measuring and calibrating its individual center position at startup.
// - Usage/呼出方法 ： call analog_joystick_init( void )
// - Return/戻り値 ： void
static void analog_joystick_init(void) {
    adc_init();
    adc_gpio_init(ANALOG_JOYSTICK_X_PIN);
    adc_gpio_init(ANALOG_JOYSTICK_Y_PIN);

    joystick_center_x = analog_joystick_read_10bit(ANALOG_JOYSTICK_X_PIN);
    joystick_center_y = analog_joystick_read_10bit(ANALOG_JOYSTICK_Y_PIN);
}

// 2乗計算した値をSUBPIXEL_SCALE倍に拡大し、端数(1未満の余り)は*carry経由で
// 次回呼び出しへ持ち越すことで、低速域での動きを整数演算下でも維持する。
// The quadratic term is scaled up by SUBPIXEL_SCALE, with any sub-1
// remainder carried over to the next call via *carry, preserving
// low-speed movement under integer arithmetic.
#define ANALOG_JOYSTICK_SUBPIXEL_SCALE 256

// - Function/関数名称 ： analog_joystick_read_raw
// - Purpose/機能 ： ジョイスティックの傾き量を、較正済みの中心位置を基準とした値として取得する機能。 / Reads the joystick's tilt as a value measured relative to its calibrated center position.
// - Usage/呼出方法 ： call analog_joystick_read_raw( uint8_t pin, int32_t center )
// - Return/戻り値 ： int32_t deflection
static int32_t analog_joystick_read_raw(uint8_t pin, int32_t center) {
    return analog_joystick_read_10bit(pin) - center;
}

// - Function/関数名称 ： analog_joystick_magnitude
// - Purpose/機能 ： ジョイスティックの傾き量を、中心付近は繊細に・大きく傾けるほど速く動く操作感のカーソル移動量に変換する機能。 / Converts joystick tilt into a cursor-movement magnitude with a feel that's fine near center and increasingly fast the further the stick is tilted.
// - Usage/呼出方法 ： call analog_joystick_magnitude( int32_t deflection, float accel_scale )
// - Return/戻り値 ： int32_t magnitude
static int32_t analog_joystick_magnitude(int32_t deflection, float accel_scale) {
    int32_t max_value = ANALOG_JOYSTICK_ADC_CENTER - ANALOG_JOYSTICK_ADC_DEADZONE;
    if (deflection > max_value) {
        deflection = max_value;
    }

    int32_t subpixels = (deflection * deflection * ANALOG_JOYSTICK_SUBPIXEL_SCALE) / max_value;
    return (int32_t)(subpixels * accel_scale);
}

// - Function/関数名称 ： analog_joystick_step
// - Purpose/機能 ： カーソル移動量を実際のマウス移動ステップへ変換する機能。低速なゆっくりとした操作でも動きが失われないようにする。 / Converts a cursor-movement magnitude into an actual mouse-movement step, preserving slow, gentle movement rather than losing it.
// - Usage/呼出方法 ： call analog_joystick_step( int32_t magnitude, int32_t sign, int32_t *carry )
// - Return/戻り値 ： int8_t step
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

// - Function/関数名称 ： analog_joystick_rotate
// - Purpose/機能 ： ジョイスティックの物理的な取り付け角度のズレを補正する機能。 / Compensates for the joystick's physical mounting angle.
// - Usage/呼出方法 ： call analog_joystick_rotate( int32_t *x, int32_t *y )
// - Return/戻り値 ： void(*xと*yが回転後の値で上書きされる)
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

// - Function/関数名称 ： analog_joystick_task
// - Purpose/機能 ： アナログジョイスティックの傾きをマウスカーソル移動に変換する機能。センター位置のドリフトと物理的な取り付け角度のズレを自動補正する。 / Converts analog joystick tilt into mouse cursor movement, automatically compensating for center-position drift and physical mounting-angle offset.
// - Usage/呼出方法 ： call analog_joystick_task( void )
// - Return/戻り値 ： void
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
    // 傾きが許容誤差内でANALOG_JOYSTICK_RECENTER_MSの間安定していたら、
    // その位置を新たな「静止」状態として再センタリングする。
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
    // 上記の再センタリングは物理ADC座標系のまま行う必要があるため、取り付け
    // 角度の回転はその後に適用し、以下のカーソル移動方向だけに影響させる。
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
    // スティックを傾け続けている時間が長いほど、ゆっくりした初速(繊細な
    // 操作向け)から徐々に加速し、ANALOG_JOYSTICK_ACCEL_RAMP_MSの間
    // 保持して初めて調整済みの最大速度に達する。
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
        mouse.y += -dy;  // invert vertical axis / 上下方向を反転させる
        pointing_device_set_report(mouse);
        pointing_device_send();

        debug_led_flash();
    }
}
#endif

// オンボードのWS2812 LEDを、キーボード入力またはジョイスティック移動の
// たびに短く点灯させる、入力確認用のデバッグインジケータ。点灯のたびに
// 色相を進めるため、連続して点灯すると虹色に変化する。自前のHSV→RGB変換
// (下記debug_led_hue_to_rgb)でws2812_setledsを直接呼び出す方式で実装している。
// Onboard WS2812 LED flashed briefly on any keyboard input or joystick
// movement, serving as a debug indicator that input is being registered.
// Each flash steps to the next hue, so consecutive flashes cycle through
// the rainbow. Implemented via a self-contained HSV->RGB conversion (see
// debug_led_hue_to_rgb below) that calls ws2812_setleds directly.

// How long (ms) each flash stays lit before turning off. Range: positive
// integer, milliseconds.
// 各点灯が消灯するまでの時間(ms)。設定範囲: 正の整数(ミリ秒)。
#define DEBUG_LED_FLASH_MS 80
// Brightness (V in HSV) of each flash. Range: 0-255.
// 各点灯の明度(HSVのV値)。設定範囲: 0-255。
#define DEBUG_LED_VAL 32
// Hue advance per flash. Range: 1-255; 256/step = number of distinct hues
// per full revolution before the cycle repeats (16 here).
// 点灯ごとに進める色相の量。設定範囲: 1-255。256をこの値で割った数が
// 1周期あたりの色数になる(この値では16色)。
#define DEBUG_LED_HUE_STEP 16  // 256 / 16 = 16 distinct hues per revolution

// Timestamp of the most recent flash (for the auto-off timeout below),
// whether the LED is currently lit from a flash, and the current position
// in the hue cycle (advances by DEBUG_LED_HUE_STEP each flash).
// 直近の点灯開始時刻(下記の自動消灯タイムアウト用)、LEDが現在点灯中かどうか、
// 現在の色相サイクル上の位置(点灯のたびにDEBUG_LED_HUE_STEPずつ進む)。
static uint16_t debug_led_timer  = 0;
static bool     debug_led_active = false;
static uint8_t  debug_led_hue    = 0;

// - Function/関数名称 ： debug_led_hue_to_rgb
// - Purpose/機能 ： 色相と明度の指定から、LEDに送るRGB発光色を求める機能。 / Determines the RGB color to send the LED from a given hue and brightness.
// - Usage/呼出方法 ： call debug_led_hue_to_rgb( uint8_t hue, uint8_t val )
// - Return/戻り値 ： LED_TYPE color
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

// - Function/関数名称 ： debug_led_flash
// - Purpose/機能 ： 入力があったことをLEDの点灯で視覚的に知らせる機能。点灯するたびに色を変える。 / Visually signals that input occurred by lighting the LED, changing color with each flash.
// - Usage/呼出方法 ： call debug_led_flash( void )
// - Return/戻り値 ： void
static void debug_led_flash(void) {
    LED_TYPE led = debug_led_hue_to_rgb(debug_led_hue, DEBUG_LED_VAL);
    debug_led_hue += DEBUG_LED_HUE_STEP;

    ws2812_setleds(&led, 1);
    debug_led_timer  = timer_read();
    debug_led_active = true;
}

// - Function/関数名称 ： debug_led_task
// - Purpose/機能 ： 入力通知の点灯を一定時間で消灯させ、次の点灯に備える機能。 / Turns off the input-notification flash after a set duration, readying it for the next flash.
// - Usage/呼出方法 ： call debug_led_task( void )
// - Return/戻り値 ： void
static void debug_led_task(void) {
    if (debug_led_active && timer_elapsed(debug_led_timer) >= DEBUG_LED_FLASH_MS) {
        LED_TYPE led = {0};
        ws2812_setleds(&led, 1);
        debug_led_active = false;
    }
}

// - Function/関数名称 ： matrix_init_kb
// - Purpose/機能 ： 起動時にこのボード固有の設定(JP/USキー上書きモード、アナログジョイスティック)を有効化する機能。 / Activates this board's own settings at startup: the JP/US key override mode and the analog joystick.
// - Usage/呼出方法 ： call matrix_init_kb( void )
// - Return/戻り値 ： void
void matrix_init_kb(void) {
    keyboard_config.raw = eeconfig_read_kb();
    set_key_override(keyboard_config.override_mode);

#ifdef ANALOG_JOYSTICK_ENABLE
    analog_joystick_init();
#endif

    matrix_init_user();
}

// - Function/関数名称 ： matrix_scan_kb
// - Purpose/機能 ： このボードの動作中に継続して必要な処理(エンコーダー用修飾キーの張り付き防止、接続先OSへの追従、リモート要求によるソフトリセットの実行、ジョイスティック/デバッグLEDの更新)をまとめて行う機能。 / Handles this board's ongoing per-cycle needs: preventing stuck encoder-action modifiers, tracking the connected host OS, carrying out remotely requested soft resets, and updating the joystick and debug LED.
// - Usage/呼出方法 ： call matrix_scan_kb( void )
// - Return/戻り値 ： void
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
            // USBをリセットし、再起動後にホストOSを正しく検出できるようにする
            reset_block(RESETS_RESET_USBCTRL_BITS);
            // Reset MCU
            // MCU自体をリセットする
            __NVIC_SystemReset();
        }
    }

#ifdef ANALOG_JOYSTICK_ENABLE
    analog_joystick_task();
#endif

    debug_led_task();

    matrix_scan_user();
}

// - Function/関数名称 ： process_record_kb
// - Purpose/機能 ： このボード固有のキー操作(エンコーダー用修飾キー、JP/USキー上書きの切替、コンボのon/off/切替)を処理し、それ以外の通常のキー入力は標準の処理へ引き継ぐ機能。押下ごとにデバッグ用LEDで通知する。 / Handles this board's own key operations (encoder-action modifiers, JP/US key override toggles, combo on/off/toggle) and passes ordinary key input through to standard processing, notifying each press via the debug LED.
// - Usage/呼出方法 ： call process_record_kb( uint16_t keycode, keyrecord_t *record )
// - Return/戻り値 ： bool continue_processing
bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        debug_led_flash();
    }

    if (encoder_modifier != 0 && !is_encoder_action) {
        unregister_mods(encoder_modifier);
        encoder_modifier = 0;
    }

    // is_encoder_actionが(キーマップ側のコードによって)立っている間、
    // QK_MODSキーコードは専用経路で処理する: 修飾キーを直接適用/保持し、
    // その解除はencoder_modifier自体のタイムアウト(matrix_scan_kb参照)で
    // 行う。この方式は、押下型のイベントのみを生成するエンコーダーの
    // 特性に合わせた設計。
    // While is_encoder_action is set (by keymap-level code), QK_MODS
    // keycodes go through a dedicated path: the modifier is applied/held
    // directly, and its release is driven by encoder_modifier's own
    // timeout (see matrix_scan_kb). This matches encoders' behavior of
    // reporting only press-style events.
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

// VIA's generic "keyboard value" ID space is shared across many keyboards;
// this reserves 0x99 to mean "this is a keyboard_quantizer-specific
// command", so raw_hid_receive_kb (below) can distinguish this board's own
// protocol extension from other VIA traffic.
// VIAの汎用的な「キーボード値」ID空間は多くのキーボードで共有されている。
// 0x99を「keyboard_quantizer独自のコマンドである」という意味で予約する
// ことで、下記raw_hid_receive_kbがこのボード独自のプロトコル拡張と、
// その他のVIA通信とを区別できるようにしている。
enum via_keyboard_value_id_kb { id_keyboard_quantizer = 0x99 };

// Sub-commands within the keyboard_quantizer-specific protocol (see
// id_keyboard_quantizer above), dispatched by raw_hid_get_kb/set_kb below.
// 上記id_keyboard_quantizer配下でやり取りされる、keyboard_quantizer独自
// プロトコルのサブコマンド。下記raw_hid_get_kb/set_kbで振り分けられる。
enum via_kq_value_id_kb {
    id_config_ver = 0x01,  // Protocol/config version query. / プロトコル・設定バージョンの問い合わせ。
    id_bootloader,         // Jump to the USB bootloader. / USBブートローダーへジャンプする。
    id_reset,              // Request a soft reset (see reset_flag). / ソフトリセットを要求する(reset_flag参照)。
    id_eeprom,   // read/write eeprom / EEPROMの読み書き
    id_host_os,  // get os / 接続先OSの種別を取得する
};

// - Function/関数名称 ： raw_hid_get_kb
// - Purpose/機能 ： VIA経由でこのボード固有の情報(設定バージョン、EEPROMの内容、接続先OS)を取得できるようにする機能。 / Lets VIA retrieve this board's own information: config version, EEPROM contents, and connected host OS.
// - Usage/呼出方法 ： call raw_hid_get_kb( uint8_t *data, uint8_t length )
// - Return/戻り値 ： void
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

// - Function/関数名称 ： raw_hid_set_kb
// - Purpose/機能 ： VIA経由でこのボードを操作(ブートローダーへの切替、リセット、EEPROMへの書き込み)できるようにする機能。 / Lets VIA operate this board: switching into the bootloader, triggering a reset, or writing EEPROM contents.
// - Usage/呼出方法 ： call raw_hid_set_kb( uint8_t *data, uint8_t length )
// - Return/戻り値 ： void
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

// - Function/関数名称 ： raw_hid_receive_kb
// - Purpose/機能 ： VIAプロトコル上で、このボード独自のコマンドを受け付ける窓口機能。 / Serves as the entry point for this board's own commands over the VIA protocol.
// - Usage/呼出方法 ： call raw_hid_receive_kb( uint8_t *data, uint8_t length )
// - Return/戻り値 ： void
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
