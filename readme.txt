========================================================================
keyboard_quantizer/mini (quantizer_mini branch)
========================================================================

[日本語]

■ 概要
このリポジトリは、RP2040(Waveshare RP2040-Zero等)を「keyboard quantizer」
として動作させるためのQMKファームウェアです。RP2040自身がPIO(プログラマ
ブルI/O)を使ってUSBホスト機能をソフトウェアで実装し、専用のUSBホスト
コントローラICなしに、接続したキーボードの入力をPCへ中継します。

■ このビルドの主な機能
・USBキーボードの中継(quantizer本体): 接続したキーボードのHID入力を
  解析し、PCへ通常のキーボードとして送信する。
・VIA/Remap対応: remap-keys.app等からキーマップをライブ編集できる。
・JP/USキー配列の上書き機能: 接続キーボードの物理配列とOS側の設定配列が
  一致しない場合に、キーコードの解釈を切り替えられる。
・アナログジョイスティックのポインティングデバイス化:
  - 起動(PC接続)時のジョイスティック位置をデッドゾーンの中心として較正
  - 傾きが4秒間同じであればノイズ/静止とみなし、その位置を新たな中心
    として自動的に再較正
  - 物理的な取り付け角度のズレを45度単位で補正(ANALOG_JOYSTICK_LAYOUT)
  - 中心付近は繊細に、大きく傾けるほど速く動く加速カーブ
・デバッグ用LED: オンボードのWS2812 LEDが、キー入力またはジョイスティック
  移動のたびに短く点灯し、点灯ごとに色が変わる(入力確認用)。

■ フォーク元
本リポジトリは以下の系譜のフォークです。
  1. QMK Firmware (本家)      : https://github.com/qmk/qmk_firmware
  2. sekigon-gonnoc/qmk_firmware (keyboard_quantizerプロジェクト、
     quantizer_miniブランチ) : https://github.com/sekigon-gonnoc/qmk_firmware
  3. 本リポジトリ(MahiroxP/qmk_firmware, quantizer_miniブランチ)
                               : https://github.com/MahiroxP/qmk_firmware/tree/quantizer_mini

■ 著作権表示
・QMK Firmware本体、および keyboard_quantizer の基盤実装:
  Copyright sekigon-gonnoc および QMK/keyboard_quantizerの各コントリ
  ビューター(各ファイル冒頭のライセンス表記を参照)
・VIAプロトコル実装: Copyright 2019 Jason Williams (Wilba)
・本フォーク(quantizer_miniブランチ)で加えられた変更(コード・コメントを
  含む一切): Copyright 2026 MahiroxP

■ 著者に関する重要事項
本フォークで加えられたコード変更およびコメントは、その一切が
Claude Code(Anthropic製のAIコーディングエージェント)によって作成された
ものであり、リポジトリ所有者(MahiroxP)本人が直接記述したものでは
ありません。上記著作権表示はリポジトリ所有者としての表示であり、実際の
記述作業はAIエージェントが行いました。

------------------------------------------------------------------------

[English]

Overview
This repository is a QMK firmware build that turns an RP2040 (e.g. a
Waveshare RP2040-Zero) into a "keyboard quantizer." The RP2040 itself
implements USB host functionality in software via its PIO (Programmable
I/O) blocks, relaying input from a connected keyboard to the PC without
a dedicated USB host controller chip.

Main features of this build
- USB keyboard relay (the core quantizer function): decodes HID input
  from a connected keyboard and sends it to the PC as a normal keyboard.
- VIA/Remap support: the dynamic keymap can be live-edited from
  remap-keys.app or the VIA app.
- JP/US key layout override: lets keycode interpretation be switched
  when the connected keyboard's physical layout doesn't match the OS's
  configured layout.
- Analog joystick as a pointing device:
  - Calibrates the joystick's resting position as the deadzone center
    at startup (i.e. when connected to the PC).
  - Treats 4 seconds of steady tilt as noise/rest and automatically
    re-calibrates the center to that position.
  - Compensates for the joystick's physical mounting angle in 45-degree
    steps (ANALOG_JOYSTICK_LAYOUT).
  - Uses an acceleration curve: fine control near center, faster
    movement the further the stick is tilted.
- Debug LED: the onboard WS2812 LED flashes briefly, changing color each
  time, on any keyboard input or joystick movement, to visually confirm
  that input is being registered.

Fork lineage
This repository forks, in order:
  1. QMK Firmware (upstream)  : https://github.com/qmk/qmk_firmware
  2. sekigon-gonnoc/qmk_firmware (the keyboard_quantizer project,
     quantizer_mini branch)   : https://github.com/sekigon-gonnoc/qmk_firmware
  3. This repository (MahiroxP/qmk_firmware, quantizer_mini branch)
                               : https://github.com/MahiroxP/qmk_firmware/tree/quantizer_mini

Copyright notices
- QMK Firmware core and the keyboard_quantizer base implementation:
  Copyright sekigon-gonnoc and the QMK/keyboard_quantizer contributors
  (see the license header in each individual file).
- VIA protocol implementation: Copyright 2019 Jason Williams (Wilba).
- Changes made on this fork's quantizer_mini branch (all code and
  comments): Copyright 2026 MahiroxP.

A note on authorship
All code changes and comments added on this fork were written by Claude
Code (an AI coding agent by Anthropic), not directly by the repository
owner (MahiroxP). The copyright notice above reflects ownership of the
repository; the actual writing was carried out by the AI agent.
