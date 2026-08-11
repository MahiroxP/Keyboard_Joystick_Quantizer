/* Copyright 2021 sekigon-gonnoc
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

#pragma once

#include <stdint.h>
#include "keycode.h"

// GPIO pin for the onboard status LED (see matrix.c), lit by default and
// briefly blinked on USB host events: a device mounting (tuh_mount_cb) or
// an HID report being received (tuh_hid_report_received_cb).
// オンボードのステータスLED用GPIOピン(matrix.c参照)。通常は点灯しており、
// USBホスト側のイベント(デバイス接続時のtuh_mount_cb、HIDレポート受信時の
// tuh_hid_report_received_cb)で短く点滅する。
#define KQ_PIN_LED 7

// 複数ボードバリアント間で共有されるkeyboard_quantizer.hのAPI一覧に含まれる
// 宣言。本ボードでの呼び出し回数は0件。
// Declarations belonging to the shared keyboard_quantizer.h API surface
// across board variants. Call count on this board is zero.

// - Function/関数名称 ： uart_recv_callback
// - Purpose/機能 ： UARTで受信した1バイトを処理する受信フック。 / A receive hook that processes one byte received over UART.
// - Usage/呼出方法 ： call uart_recv_callback( uint8_t dat )
// - Return/戻り値 ： void
void uart_recv_callback(uint8_t dat);

// - Function/関数名称 ： uart_buf_init
// - Purpose/機能 ： UART受信バッファを初期化する。 / Initializes the UART receive buffer.
// - Usage/呼出方法 ： call uart_buf_init( void )
// - Return/戻り値 ： void
void uart_buf_init(void);

// - Function/関数名称 ： send_reset_cmd
// - Purpose/機能 ： UART経由で相手チップへリセットコマンドを送信する。 / Sends a reset command to the companion chip over UART.
// - Usage/呼出方法 ： call send_reset_cmd( void )
// - Return/戻り値 ： int status
int  send_reset_cmd(void);

// - Function/関数名称 ： send_led_cmd
// - Purpose/機能 ： UART経由で相手チップへLED状態コマンドを送信する。 / Sends an LED state command to the companion chip over UART.
// - Usage/呼出方法 ： call send_led_cmd( uint8_t led )
// - Return/戻り値 ： int status
int  send_led_cmd(uint8_t led);

// Persistent (EEPROM-backed) per-keyboard configuration, packed into a
// single 32-bit word so it can be read/written as one eeconfig value.
// EEPROMに保存される、このキーボード固有の設定。1回のeeconfig読み書きで
// 済むよう、複数の設定項目を32bitに詰め込んだビットフィールド。
typedef union {
    uint32_t raw;
    struct {
        uint8_t os_eeconfig : 1;        // Auto-detect & remember host OS (Win/Mac/etc.) for key overrides below. / 接続先OS(Win/Mac等)を自動判別し記憶するか(下記キー上書き機能で使用)。
        uint8_t override_mode : 2;      // Which JP/US key-override behavior is active (see kb_keycodes below). / 下記kb_keycodesのどのJP/US上書き挙動が有効かを示す。
        uint8_t layer_to_combo : 1;     // Convert layer-switch keys to combo keys (see convert_layer_to_combo). / レイヤー切替キーをコンボキーに変換するか(convert_layer_to_combo参照)。
        uint8_t tapping_term_20ms : 4;  // Tapping term in 20ms steps: term = 20*x+60ms (x>1). / タッピング判定時間を20ms単位で指定: term = 20*x+60ms (x>1)。設定範囲: 0-15。
        uint8_t parser_type : 1;        // Which connected-device report parser variant to use. / 接続機器のレポート解析方式の種別。
    };
} keyboard_config_t;
extern keyboard_config_t keyboard_config;

// Custom keycodes for switching between JP/US key-override behaviors, used
// when the connected keyboard's physical layout doesn't match the OS's
// configured keyboard layout (e.g. a US-layout keyboard on a JP-configured
// OS, or vice versa).
// 接続したキーボードの物理レイアウトとOS側の設定レイアウトが一致しない場合
// (例: JP設定のOSにUS配列キーボードを接続、またはその逆)に、JP/US間の
// キー上書き挙動を切り替えるための独自キーコード。
enum kb_keycodes {
    DISABLE_KEY_OVERRIDES = KC_FN0,    // Turn off all JP/US key overrides. / JP/US間のキー上書きを全て無効化する。
    ENABLE_US_KEY_ON_JP_OS_OVERRIDE,   // Treat the connected keyboard as US layout while the OS is set to JP. / OSがJP設定の状態で、接続キーボードをUS配列として扱う。
    ENABLE_JP_KEY_ON_US_OS_OVERRIDE,   // Treat the connected keyboard as JP layout while the OS is set to US. / OSがUS設定の状態で、接続キーボードをJP配列として扱う。
};
