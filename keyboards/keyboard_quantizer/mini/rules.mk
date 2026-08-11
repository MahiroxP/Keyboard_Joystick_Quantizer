# Overrides QMK's default output filename (KEYBOARD_FILESAFE_KEYMAP, i.e.
# keyboard_quantizer_mini_default) with this project's name.
# QMKのデフォルト出力ファイル名(KEYBOARD_FILESAFE_KEYMAP、つまり
# keyboard_quantizer_mini_default)をこのプロジェクトの名前で上書きする。
TARGET = Keyboard_Joystick_Quantizer

# main.c is no longer auto-detected by QMK's build system, since that
# detection matches files named after the keyboard folder ("mini.c"); add
# it explicitly. Also the custom-matrix source (see matrix.c: TinyUSB host
# callbacks + report parsing dispatch) and the debug status LED driver
# (see main.c).
# main.cはQMKのビルドシステムによる自動検出(キーボードのフォルダ名と同名の
# ファイル="mini.c"にマッチする仕組み)が効かなくなったため、明示的に追加。
# また、カスタムマトリクスのソース(matrix.c: TinyUSBホストのコールバックと
# レポート解析の振り分け)と、デバッグ用ステータスLEDのドライバ(main.c参照)。
SRC += main.c
SRC += matrix.c
SRC += drivers/pico/ws2812.c
# drivers/pico/ws2812.c includes "atomic_util.h" by bare filename; this
# repo has both a generic tmk_core/common/atomic_util.h and an RP2040-
# specific tmk_core/common/pico/atomic_util.h (defines __interrupt_disable__
# / __interrupt_enable__ via hardware/sync.h), and only the latter works on
# this MCU, so force it ahead of the generic one in the include search path.
# drivers/pico/ws2812.cはベアなファイル名で"atomic_util.h"をincludeして
# いるが、このリポジトリには汎用版(tmk_core/common/atomic_util.h)とRP2040
# 専用版(tmk_core/common/pico/atomic_util.h、hardware/sync.h経由で
# __interrupt_disable__/__interrupt_enable__を定義)の2つが存在し、この
# MCUで動作するのは専用版のみ。汎用版より優先して見つかるよう、インクルード
# 検索パスに明示的に追加している。
EXTRAINCDIRS += tmk_core/common/pico
# MCU name
# MCU種別の指定。この3つの組み合わせにより、tmk_core/pico.mkと
# tmk_core/protocol/pico.mk(RP2040/Pico向けビルド設定一式)が読み込まれる。
MCU_FAMILY = PICO
MCU_SERIES = RP2040
MCU = cortex-m0plus

# フラッシュメモリのSPIクロック分周比(値が大きいほど、より低速・安全側の
# クロックになる)。RP2040-Zeroクローン基板は多様なフラッシュチップを搭載
# しうるため、pico-sdk既定値(4)より高い値にして互換性の幅を広げている。
# 設定範囲: 正の整数。
# Flash SPI clock divider (higher = slower/more conservative flash clock).
# Set above the pico-sdk default (4) to widen compatibility with the
# variety of flash chips found on RP2040-Zero clone boards. Range:
# positive integer.
PICO_FLASH_SPI_CLKDIV = 8

# matrix.c内のカスタムマトリクス実装(本プロジェクトのTinyUSBホストベースの
# マトリクスの「lite」版)を使用する設定。
# Selects the custom matrix implementation in matrix.c (the "lite" variant
# of this project's TinyUSB-host-based matrix).
CUSTOM_MATRIX = lite
# Enables the VIA protocol, so remap-keys.app (or the VIA app) can query
# and live-edit the dynamic keymap over USB.
# VIAプロトコルを有効化し、remap-keys.app(またはVIAアプリ)からUSB経由で
# ダイナミックキーマップを照会・ライブ編集できるようにする。
VIA_ENABLE = yes
# Enables QMK's pointing-device (mouse) report subsystem, used both to
# forward reports from a connected USB mouse and to send the analog
# joystick's synthesized mouse movement (see main.c).
# QMKのポインティングデバイス(マウス)レポート機構を有効化する。接続された
# USBマウスのレポート転送と、アナログジョイスティックが生成するマウス移動
# (main.c参照)の両方で使われる。
POINTING_DEVICE_ENABLE = yes

# Shared host-OS auto-detection (used for the JP/US key override feature)
# and layer-to-combo conversion features, from the shared users/sekigon/
# codebase (used across multiple keyboard_quantizer board variants).
# 複数のkeyboard_quantizerボードバリアント間で共有されているusers/sekigon/
# 配下のコードから、接続先OSの自動判別機能(JP/USキー上書き機能で使用)と、
# レイヤー切替キーをコンボキーへ変換する機能を取り込む。
include users/sekigon/host_os_eeconfig/rules.mk
include users/sekigon/use_layer_as_combo_config/rules.mk
