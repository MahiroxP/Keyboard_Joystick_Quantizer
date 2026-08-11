
#pragma once

// Number of dynamic keymap layers stored in EEPROM for VIA/Remap live
// remapping. Range: positive integer; each layer costs
// MATRIX_ROWS*MATRIX_COLS*2 bytes of EEPROM, so it is bounded by available
// EEPROM space (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR, set in tmk_core/pico.mk).
// VIA/Remapのライブ書き換え機能用に、EEPROMに保持するダイナミックキーマップ
// のレイヤー数。設定範囲: 正の整数。1レイヤーあたりMATRIX_ROWS*MATRIX_COLS*2
// バイトのEEPROMを消費するため、実際に使えるEEPROM容量
// (tmk_core/pico.mkのDYNAMIC_KEYMAP_EEPROM_MAX_ADDR)で上限が決まる。
#define DYNAMIC_KEYMAP_LAYER_COUNT 10

// EEPROM address of VIA's "magic number", used by VIA to detect whether
// the stored EEPROM layout is valid (resetting it when invalid). Range:
// must stay within its own dedicated range, disjoint from the EEPROM
// ranges used by other features (dynamic keymap storage, host_os_eeconfig,
// use_layer_as_combo_config, etc.).
// VIAが保存済みEEPROMレイアウトの有効性を判定するための「マジックナンバー」
// を置くEEPROMアドレス(無効と判定された場合はリセットされる)。設定範囲:
// 他機能(ダイナミックキーマップ本体、host_os_eeconfig、
// use_layer_as_combo_configなど)が使うEEPROM領域と重ならない専用範囲内。
#define VIA_EEPROM_MAGIC_ADDR (EECONFIG_SIZE * 5 + 1)

// USB Product ID reported to the host. Range: 0x0000-0xFFFF (16bit),
// overrides the value from the shared keyboard_quantizer.h.
// ホストに報告するUSBプロダクトID。設定範囲: 0x0000-0xFFFF(16bit)。
// 共通ヘッダkeyboard_quantizer.hで定義された値をこのボード用に上書きする。
#undef PRODUCT_ID
#define PRODUCT_ID 0x999b

// RP2040のシステムクロック(kHz単位)。USBフルスピードのビットクロックである
// 12MHzの整数倍(120MHz = 12MHz x 10)に設定することで、ソフトウェアでUSB
// ビット列を生成するPico-PIO-USBのタイミング精度を確保している。
// RP2040 system clock, in kHz. Set to an exact multiple of the 12MHz USB
// full-speed bit clock (120MHz = 12MHz x 10) to keep Pico-PIO-USB's
// software-bit-banged USB host timing accurate.
#define PICO_SYSTEM_CLOCK_KHZ 120000

// Pico-PIO-USBを有効化する設定。RP2040自身のPIO(プログラマブルI/O)
// ブロックでUSBホスト信号をソフトウェア生成することで、専用のUSBホスト
// コントローラICなしにRP2040ボード単体でUSBホストとして動作させる。
// Enables Pico-PIO-USB: the RP2040 generates USB host signaling in
// software via its PIO (programmable I/O) blocks, letting this board act
// as a USB host on its own, without a separate USB host controller chip.
#define PICO_PIO_USB_HOST_ENABLE

/* Analog joystick used as an additional pointing device (no button) */
/* ボタンなしのアナログジョイスティックを、追加のポインティングデバイスとして使用する */
// main.c内の、アナログジョイスティックをマウスカーソルに変換する独自コード
// (matrix_init_kb/matrix_scan_kbから呼ばれるanalog_joystick_init/task)を
// 有効化する、本プロジェクト独自の設定フラグ。
// Enables the custom analog-joystick-to-mouse-cursor code in main.c
// (matrix_init_kb/matrix_scan_kb call analog_joystick_init/task); a
// project-specific flag distinct from QMK's own ANALOG_JOYSTICK feature.
#define ANALOG_JOYSTICK_ENABLE

// RP2040 ADC-capable GPIO pin wired to the joystick's X and Y axis.
// Range: 26-29 (RP2040's four ADC-capable GPIOs).
// ジョイスティックのX軸・Y軸が接続されているRP2040のADC対応GPIOピン。
// 設定範囲: 26-29(RP2040でADCが使える4本のGPIOのいずれか)。
// GPIO 26 ： X軸に対応
// GPIO 27 ： Y軸に対応
#define ANALOG_JOYSTICK_X_PIN 26
#define ANALOG_JOYSTICK_Y_PIN 27

// Nominal center (rest position) reading on the 10bit-rescaled ADC scale
// used by this code (see analog_joystick_read_10bit in main.c). Only a
// fallback: the actual center is recalibrated from the real resting
// position at power-on and after periods of steady tilt (see
// ANALOG_JOYSTICK_RECENTER_MS below). Range: 0-1023.
// このコードが使う10bit換算ADCスケール(main.cのanalog_joystick_read_10bit
// 参照)上での、中央(静止)位置の想定値。あくまでフォールバックであり、
// 実際の中心は電源投入時と、傾きが一定時間安定した後(下記
// ANALOG_JOYSTICK_RECENTER_MS参照)に実測値へ再較正される。設定範囲: 0-1023。
#define ANALOG_JOYSTICK_ADC_CENTER 512

// Deflection (in ADC counts from center) below which the stick is treated
// as centered/idle, to ignore electrical noise and minor drift. Range:
// 0 to just under ANALOG_JOYSTICK_ADC_CENTER (must leave room for real
// deflection above the deadzone).
// 中心からのズレ(ADCカウント)がこの値未満なら、静止状態とみなして無視する
// (電気的ノイズや軽微なドリフトを無視するため)。設定範囲: 0以上、かつ
// ANALOG_JOYSTICK_ADC_CENTERを大きく下回る値(デッドゾーンを超えて実際に
// 傾きを検出できる余地を残す必要がある)。
#define ANALOG_JOYSTICK_ADC_DEADZONE 90

// Overall cursor speed divisor: larger values move the cursor slower for
// the same physical tilt. Range: positive integer, tuned by feel.
// カーソル移動速度全体の除数。値が大きいほど、同じ傾き量でもカーソルが
// ゆっくり動く。設定範囲: 正の整数。実際に動かして好みの速さになるよう調整する。
#define ANALOG_JOYSTICK_ADC_DIVISOR 640

// If the stick reports the same tilt continuously for this long, that tilt
// is treated as the new "stopped" position (re-centers to compensate for
// the physical resting point drifting over time). Range: positive integer,
// milliseconds.
// スティックが同じ傾きをこの時間継続して報告した場合、その位置を新たな
// 「静止」位置として再較正する(物理的な静止点のドリフトを補正するため)。
// 設定範囲: 正の整数(ミリ秒)。
#define ANALOG_JOYSTICK_RECENTER_MS 4000

// ADC counts of wiggle room when judging whether the tilt is "the same",
// to tolerate ADC noise. Range: small positive integer (0 or more).
// 傾きが「同じ」かどうかを判定する際に許容するADCカウントのブレ幅
// (ADCノイズを許容するため)。設定範囲: 0以上の小さな整数。
#define ANALOG_JOYSTICK_RECENTER_TOLERANCE 3

// How much slower than the tuned max speed movement starts at when the
// stick first leaves the deadzone (higher = slower start). Range: positive
// integer; effective starting speed is roughly max-speed / this value.
// スティックがデッドゾーンを抜けた直後の初速が、最大速度に対してどれだけ
// 遅いか(値が大きいほど、より遅い初速になる)。設定範囲: 正の整数。
// 実際の初速はおおよそ「最大速度 / この値」になる。
#define ANALOG_JOYSTICK_ACCEL_START_SCALE 240

// How long (ms) the stick needs to stay tilted before reaching the tuned
// max speed above. Range: positive integer, milliseconds (0 would mean
// full speed immediately).
// スティックを傾け続けてから、上記の最大速度に到達するまでの時間(ms)。
// 設定範囲: 正の整数(ミリ秒)。0にすると傾けた瞬間から最大速度になる。
#define ANALOG_JOYSTICK_ACCEL_RAMP_MS 4000

// Compensates for the joystick's physical mounting angle by rotating its
// output. Degrees, clockwise, must be a multiple of 45 in [0, 315].
// ジョイスティックの物理的な取り付け角度のズレを補正するため、出力方向を
// 回転させる。単位は度・時計回り。設定範囲: 0から315までの45の倍数
// (0/45/90/135/180/225/270/315のいずれか)。
#define ANALOG_JOYSTICK_LAYOUT 0

/* key matrix size */
/* 仮想キーマトリクスのサイズ(接続キーボードのレポートをこのマトリクスへ変換して扱う) */
// Total rows in the virtual matrix: enough rows for the largest supported
// physical layout, plus the extra rows below (modifiers/mouse buttons/
// mouse gestures). Range: positive integer, must be >= the row indices
// used below (MATRIX_MSGES_ROW + 1 at minimum).
// 仮想マトリクスの総行数: 対応する物理レイアウトの最大サイズに、下記の
// 追加行(モディファイア/マウスボタン/マウスジェスチャー)を加えた数。
// 設定範囲: 正の整数。少なくとも下記で使う行番号(MATRIX_MSGES_ROW + 1)以上が必要。
#define MATRIX_ROWS 24

// Total columns in the virtual matrix. Range: positive integer, must be >=
// the column indices used below (MATRIX_MSWHEEL_COL + wheel key count).
// 仮想マトリクスの総列数。設定範囲: 正の整数。少なくとも下記で使う列番号
// (MATRIX_MSWHEEL_COL + ホイールキー数)以上が必要。
#define MATRIX_COLS 8
#define MATRIX_ROWS_DEFAULT MATRIX_ROWS
#define MATRIX_COLS_DEFAULT MATRIX_COLS

// Row holding modifier keycodes (KC_LCTRL+col) in the default dynamic
// keymap generated by dynamic_keymap_reset() in main.c. Range: 0 to
// MATRIX_ROWS-1, must be less than MATRIX_MSBTN_ROW.
// main.cのdynamic_keymap_reset()が生成するデフォルトキーマップで、
// モディファイアキー(KC_LCTRL+col)を割り当てる行。設定範囲: 0から
// MATRIX_ROWS-1まで。MATRIX_MSBTN_ROWより小さい値である必要がある。
#define MATRIX_MODIFIER_ROW 21

// Row holding mouse button keycodes (KC_BTN1+col). Range: 0 to
// MATRIX_ROWS-1, must be less than MATRIX_MSGES_ROW.
// マウスボタンキー(KC_BTN1+col)を割り当てる行。設定範囲: 0からMATRIX_ROWS-1
// まで。MATRIX_MSGES_ROWより小さい値である必要がある。
#define MATRIX_MSBTN_ROW 22

// Row holding mouse gesture (wheel) keycodes. Range: 0 to MATRIX_ROWS-1.
// マウスジェスチャー(ホイール)キーを割り当てる行。設定範囲: 0から
// MATRIX_ROWS-1まで。
#define MATRIX_MSGES_ROW 23
#define MATRIX_MSGES_ROW 23
#define MATRIX_MSWHEEL_ROW 23

// Column within MATRIX_MSGES_ROW where wheel keycodes (KC_MS_WH_UP+...)
// start. Range: 0 to MATRIX_COLS-1.
// MATRIX_MSGES_ROW内で、ホイールキー(KC_MS_WH_UP+...)の割り当てが始まる列。
// 設定範囲: 0からMATRIX_COLS-1まで。
#define MATRIX_MSWHEEL_COL 4

// QMK標準の分割キーボード用「左右どちらか」を示すフラグ。本ボードは
// 分割キーボードではなく単体構成のため、参照箇所は現状0件(名残の設定)。
// Standard QMK split-keyboard handedness flag. This board is a
// single-unit design (not split), so its reference count is currently
// zero (a holdover setting).
#define IS_LEFT_HAND true

// keymap.cのLAYOUT()キーコードから、ダイナミックキーマップ(VIA/Remapで
// 書き換え可能な領域)を直接参照できるようにする。
// Lets keymap.c's LAYOUT() keycodes reference dynamic-keymap-controlled
// slots (the VIA/Remap-editable area) directly.
#define OVERRIDE_KEYMAP_KEY_TO_KEYCODE

// Selects which HID report parser this board uses (see the
// REPORT_PARSER_* constants in keyboard_quantizer.h). Range: one of
// REPORT_PARSER_DEFAULT(1)/REPORT_PARSER_FIXED(2)/REPORT_PARSER_USER(3)/
// REPORT_PARSER_BMP(4).
// このボードが使うHIDレポート解析方式を選ぶ(keyboard_quantizer.h内の
// REPORT_PARSER_*定数を参照)。設定範囲: REPORT_PARSER_DEFAULT(1)/
// REPORT_PARSER_FIXED(2)/REPORT_PARSER_USER(3)/REPORT_PARSER_BMP(4)の
// いずれか。
#define QUANTIZER_REPORT_PARSER REPORT_PARSER_DEFAULT

#define RGBLIGHT_SPLIT

// オンボードWS2812 LED(Waveshare RP2040-Zero)のGPIOピン番号。main.c内で
// ws2812_setleds()から直接制御し、デバッグ用の入力インジケータとして使う。
// 設定範囲: 0-29(RP2040の任意のGPIO)。
// GPIO pin of the onboard WS2812 LED (Waveshare RP2040-Zero), driven
// directly via ws2812_setleds() in main.c for debug input indication.
// Range: 0-29 (any RP2040 GPIO).
#define RGB_DI_PIN 16

// 以下、#endifまでの設定はQMK標準のRGBLIGHT_ENABLEアニメーション機能用。
// この機能自体はkeyboard_quantizer/rules.mk(RGBLIGHT_ENABLE = no)で無効
// 化されており、実際に動いているのは上記のデバッグ用LED(直接制御方式)のみ。
// Everything below this line, until #endif, belongs to QMK's
// RGBLIGHT_ENABLE animation feature. That feature is turned off by
// keyboard_quantizer/rules.mk (RGBLIGHT_ENABLE = no); the debug LED above
// (driven directly) is what actually runs on this board.
#ifdef RGB_DI_PIN
#    define RGBLED_NUM_DEFAULT 128  // LED count. Range: positive integer. / LED数。設定範囲: 正の整数。
#    define RGBLIGHT_HUE_STEP 8     // Hue change per keypress step. Range: 0-255. / キー操作1回あたりの色相変化量。設定範囲: 0-255。
#    define RGBLIGHT_SAT_STEP 8     // Saturation change per step. Range: 0-255. / 彩度の変化量。設定範囲: 0-255。
#    define RGBLIGHT_VAL_STEP 8     // Brightness change per step. Range: 0-255. / 明度の変化量。設定範囲: 0-255。
#    define RGBLIGHT_LIMIT_VAL 255 /* The maximum brightness level */ /* 最大輝度レベル。設定範囲: 0-255。*/
#    define RGBLIGHT_SLEEP /* If defined, the RGB lighting will be switched \
                              off when the host goes to sleep */
                           /* 定義するとホストのスリープ時にRGBを消灯する */
                           /*== all animations enable ==*/
                           /*== 全アニメーション有効化 ==*/
#    define RGBLIGHT_ANIMATIONS
/*== or choose animations ==*/
/*== または個別に選択 ==*/
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
/*== 呼吸(breathing)エフェクトの調整 ==*/
/*==== (DEFAULT) use fixed table instead of exp() and sin() ====*/
/*==== (デフォルト) exp()/sin()の代わりに固定テーブルを使う ====*/
#    define RGBLIGHT_BREATHE_TABLE_SIZE 256  // 256(default) or 128 or 64 / 256(既定)、128、64のいずれか
/*==== use exp() and sin() ====*/
/*==== exp()とsin()を使う場合 ====*/
#    define RGBLIGHT_EFFECT_BREATHE_CENTER 1.85  // 1 to 2.7 / 設定範囲: 1.0-2.7
#    define RGBLIGHT_EFFECT_BREATHE_MAX 255      // 0 to 255 / 設定範囲: 0-255
#endif
