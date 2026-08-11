// このファイルはQMKのカスタムマトリクス機能(rules.mkのCUSTOM_MATRIX = lite
// 参照)を実装している。「keyboard quantizer」はTinyUSBホストとして動作し、
// 接続されたキーボードからHIDレポートを受信、解析結果を仮想的なQMK
// マトリクスに書き込むことで、QMKの他の部分(キーマップ、VIA等)から見て
// 通常のキーボードのように扱えるようにしている。
// This file implements QMK's custom matrix (CUSTOM_MATRIX = lite, see
// rules.mk) for the "keyboard quantizer": it acts as a TinyUSB host,
// receives HID reports from a connected keyboard, and writes the parsed
// key state into a virtual QMK matrix so the rest of QMK (keymap, VIA,
// etc.) can treat it like a normal keyboard.

#include QMK_KEYBOARD_H
#include "mini.h"

#include <string.h>

#include "report_descriptor_parser.h"
#include "report_parser.h"

#include "quantum.h"

#include "tusb.h"
#include "hardware/sync.h"

// The virtual matrix buffer that report_parser() writes decoded keyboard
// input into. Points at QMK's own matrix state, defined elsewhere.
// report_parser()がデコードしたキー入力を書き込む仮想マトリクスバッファ。
// QMK本体側のマトリクス状態を指している(他所で定義)。
extern matrix_row_t* matrix_dest;
// Points at the row (see MATRIX_MSBTN_ROW in config.h) used for mouse
// button state, so mouse-button-handling code elsewhere can find it.
// マウスボタンの状態を格納する行(config.hのMATRIX_MSBTN_ROW参照)を指す。
// 他所のマウスボタン処理コードから参照できるようにするため。
matrix_row_t* matrix_mouse_dest;

// ステータスLED(KQ_PIN_LED)が現在の点滅を開始した時刻(timer_read()の値)。
// 値-1はアイドル状態(点灯継続中)を表す。
// timer_read() timestamp when the status LED (KQ_PIN_LED) started its
// current blink. A value of -1 represents the idle state (steady on).
static int32_t led_count = -1;
// Duration of each half of the status LED's blink (off then on). Range:
// positive integer, milliseconds.
// ステータスLED点滅の各半周期(消灯→点灯)の長さ。設定範囲: 正の整数(ミリ秒)。
#define LED_BLINK_TIME_MS 50

// 接続中のキーボードのUSBデバイスアドレス/HIDインスタンス番号
// (LEDレポートを送り返す際に使う)。値0は未接続状態を表す。
// USB device address/HID instance of the connected keyboard (used for
// sending LED reports back to it). A value of 0 represents the
// unconnected state.
static uint8_t kbd_addr;
static uint8_t kbd_instance;
// Buffer/size for the most recently received HID report, and which HID
// instance it came from. hid_report_size doubles as a "report pending"
// flag consumed by matrix_scan_custom().
// 直近に受信したHIDレポートのバッファ・サイズと、どのHIDインスタンスから
// 届いたか。hid_report_sizeは「未処理レポートあり」フラグも兼ねており、
// matrix_scan_custom()が消費する。
static uint8_t hid_report_buffer[64];
static volatile uint8_t hid_report_size;
static uint8_t hid_instance;
// Set when the connected device disconnects, so matrix_scan_custom() knows
// to clear the matrix on its next scan.
// 接続デバイスが切断された際にセットされ、matrix_scan_custom()が次回の
// スキャンでマトリクスをクリアすべきと判断するために使う。
static bool    hid_disconnect_flag;
// Previous frame's raw keyboard report, used by the "fixed" report parser
// (parser_type == 1) to diff against for key up/down edge detection.
// 直前フレームの生のキーボードレポート。「fixed」パーサー(parser_type == 1)
// が、キーの押下/離上のエッジ検出のために前回との差分を取るために使う。
static uint8_t pre_keyreport[8];

bool report_parser(uint8_t instance, uint8_t const* buf, uint16_t len,
                   matrix_row_t* current_matrix);
bool send_led_report(uint8_t* leds);

// - Function/関数名称 ： matrix_init_custom
// - Purpose/機能 ： 起動時にステータスLEDを点灯状態にする機能。 / Lights the status LED at startup.
// - Usage/呼出方法 ： call matrix_init_custom( void )
// - Return/戻り値 ： void
void matrix_init_custom(void) {
    setPinOutput(KQ_PIN_LED);
    writePinHigh(KQ_PIN_LED);
}

// - Function/関数名称 ： matrix_scan_custom
// - Purpose/機能 ： 接続キーボードからの入力を仮想マトリクスへ反映し続ける、本ボードのマトリクススキャン機能。ホストのLEDインジケータ状態を接続キーボードへ同期し、ステータスLEDでの接続状況表示も担う。 / The board's matrix-scanning feature: continuously reflects input from the connected keyboard into the virtual matrix, keeps the host's LED indicator state synced to the connected keyboard, and shows connection activity via the status LED.
// - Usage/呼出方法 ： call matrix_scan_custom( matrix_row_t current_matrix[] )
// - Return/戻り値 ： bool changed
bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_dest = current_matrix;

    static uint8_t keyboard_led;
    if (keyboard_led != host_keyboard_leds()) {
        uint8_t led_backup = keyboard_led;
        keyboard_led       = host_keyboard_leds();
        if (!send_led_report(&keyboard_led)) {
            keyboard_led = led_backup;
        }
    }

    if (led_count >= 0) {
        if (timer_elapsed(led_count) < LED_BLINK_TIME_MS) {
            writePinLow(KQ_PIN_LED);
        } else if (timer_elapsed(led_count) < 2 * LED_BLINK_TIME_MS) {
            writePinHigh(KQ_PIN_LED);
        } else {
            led_count = -1;
        }
    }

    matrix_mouse_dest = &current_matrix[MATRIX_MSBTN_ROW];

    if (hid_disconnect_flag) {
        bool matrix_change = false;
        for (uint8_t rowIdx = 0; rowIdx < MATRIX_ROWS; rowIdx++) {
            if (current_matrix[rowIdx] != 0) {
                matrix_change          = true;
                current_matrix[rowIdx] = 0;
            }
        }

        hid_disconnect_flag = false;

        return matrix_change;
    }

    if (hid_report_size > 0) {
        bool matrix_change = report_parser(hid_instance, hid_report_buffer,
                                           hid_report_size, current_matrix);
        hid_report_size    = 0;
        return matrix_change;
    } else {
        return false;
    }
}

// - Function/関数名称 ： tuh_mount_cb
// - Purpose/機能 ： USB機器の接続をステータスLEDの点滅で知らせ、接続機器がキーボードであれば以後のLEDレポート送信(send_led_report)に使う宛先情報を記録する機能。 / Signals a newly connected USB device via the status LED, and for keyboard devices, records the addressing information later used to send LED reports (send_led_report).
// - Usage/呼出方法 ： call tuh_mount_cb( uint8_t dev_addr )
// - Return/戻り値 ： void
void tuh_mount_cb(uint8_t dev_addr) {
    if (led_count < 0) {
        led_count = timer_read();
    }

    for (int instance = 0; instance < tuh_hid_instance_count(dev_addr);
         instance++) {
        uint8_t const itf_protocol =
            tuh_hid_interface_protocol(dev_addr, instance);
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            kbd_addr     = dev_addr;
            kbd_instance = instance;
        }
    }
}

// - Function/関数名称 ： tuh_hid_mount_cb
// - Purpose/機能 ： 新しく接続されたHID機器のレポート形式を把握し、その機器からのHID入力受信を開始する機能。 / Learns the report layout of a newly connected HID device and starts receiving its HID input.
// - Usage/呼出方法 ： call tuh_hid_mount_cb( uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len )
// - Return/戻り値 ： void
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    report_descriptor_parser_user(instance, desc_report, desc_len);
    tuh_hid_receive_report(dev_addr, instance);
}

// - Function/関数名称 ： tuh_hid_umount_cb
// - Purpose/機能 ： HID機器の切断を検知し、解析状態と仮想マトリクスの表示を切断前の状態から切り離してクリーンな状態に戻す機能。 / Detects a HID device disconnect and returns parser state and the virtual matrix to a clean state, independent of what was connected before.
// - Usage/呼出方法 ： call tuh_hid_umount_cb( uint8_t dev_addr, uint8_t instance )
// - Return/戻り値 ： void
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    on_disconnect_device_user(instance);
    hid_disconnect_flag = true;
    kbd_addr = 0;
    kbd_instance = 0;
}

// - Function/関数名称 ： tuh_hid_report_received_cb
// - Purpose/機能 ： 接続機器からのHID入力を継続的に取り込む機能。受信データを取り込んだ後、次の入力の受信を再度待ち受ける。 / Continuously ingests HID input from the connected device, re-arming the wait for the next input after each one is captured.
// - Usage/呼出方法 ： call tuh_hid_report_received_cb( uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len )
// - Return/戻り値 ： void
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    if (led_count < 0) {
        led_count = timer_read();
    }

    if (len > 0) {
        int cnt = 0;
        while (hid_report_size > 0 && cnt++ < 50000) {
            if (cnt == 1) {
                dprintf("report stacked(%d)...", hid_report_size);
            }
            busy_wait_us(1);
            continue;
        }

        if (cnt > 0) {
            dprintf("(%d us)\n", cnt);
        }

        hid_instance = instance;
        memcpy(hid_report_buffer, report, len);
        __compiler_memory_barrier();
        // hid_report_size is used as trigger of report parser
        hid_report_size = len;
    }

    tuh_hid_receive_report(dev_addr, instance);
}

// - Function/関数名称 ： report_descriptor_parser_user
// - Purpose/機能 ： 接続機器のHIDレポート形式を、設定(keyboard_config.parser_type)に応じた方式で把握する機能。固定形式を前提とする設定では解析を省略し、それ以外では実際のレポート形式を解析して把握する。 / Determines a connected device's HID report layout using the method selected by keyboard_config.parser_type: a fixed-layout setting skips analysis, other settings parse the descriptor to learn the device's actual layout.
// - Usage/呼出方法 ： call report_descriptor_parser_user( uint8_t dev_num, uint8_t const* buf, uint16_t len )
// - Return/戻り値 ： void
void report_descriptor_parser_user(uint8_t dev_num, uint8_t const* buf,
                                   uint16_t len) {
    if (keyboard_config.parser_type == 1) {
        // no descriptor parser
    } else {
        parse_report_descriptor(dev_num, buf, len);
    }
}

// - Function/関数名称 ： report_parser
// - Purpose/機能 ： 接続機器からのキー入力データを解析し、仮想マトリクスのキー状態を更新する機能。 / Decodes key input data from the connected device and updates the virtual matrix's key state.
// - Usage/呼出方法 ： call report_parser( uint8_t instance, uint8_t const* buf, uint16_t len, matrix_row_t* current_matrix )
// - Return/戻り値 ： bool changed
bool report_parser(uint8_t instance, uint8_t const* buf, uint16_t len,
                   matrix_row_t* current_matrix) {
    if (keyboard_config.parser_type == 1) {
        return report_parser_fixed(buf, len, pre_keyreport, current_matrix);
    } else {
        return parse_report(instance, buf, len);
    }
}

// - Function/関数名称 ： on_disconnect_device_user
// - Purpose/機能 ： 接続機器の切り替え時に、キー解析の内部状態を初期状態へ戻す機能。 / Resets key-decoding internal state to its initial condition when the connected device changes.
// - Usage/呼出方法 ： call on_disconnect_device_user( uint8_t device )
// - Return/戻り値 ： void
void on_disconnect_device_user(uint8_t device) {
    memset(pre_keyreport, 0, sizeof(pre_keyreport));
}

// - Function/関数名称 ： send_led_report
// - Purpose/機能 ： 接続中の物理キーボードのLED表示(Caps/Num/Scroll Lock)を、ホスト側の状態と同期させる機能。 / Keeps the connected physical keyboard's LED indicators (caps/num/scroll lock) synced with the host's state.
// - Usage/呼出方法 ： call send_led_report( uint8_t* leds )
// - Return/戻り値 ： bool sent
bool send_led_report(uint8_t* leds) {
    if (kbd_addr != 0) {
        return tuh_hid_set_report(kbd_addr, kbd_instance, 0,
                                  HID_REPORT_TYPE_OUTPUT, leds, sizeof(*leds));
    }

    return false;
}
