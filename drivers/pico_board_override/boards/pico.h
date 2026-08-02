#pragma once

// The stock Raspberry Pi Pico board header hardcodes boot_stage2 for the
// W25Q080 flash chip. This target actually runs on RP2040 Zero clone
// boards, whose flash chip isn't guaranteed to speak W25Q080's fast-read
// sequence: the ROM bootloader's own UF2 write still succeeds, but the
// mismatched boot_stage2 can fail to bring up QSPI on the next reset, so
// the board never comes back to life after flashing. Fall back to
// boot2_generic_03h, which uses the universally-supported 0x03 read
// command instead.
#include_next "boards/pico.h"

#undef PICO_BOOT_STAGE2_CHOOSE_W25Q080
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 0
