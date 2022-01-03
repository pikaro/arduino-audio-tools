// -------------------------------------------------- //
// This file is autogenerated by pioasm; do not edit! //
// -------------------------------------------------- //

#if !PICO_NO_HARDWARE
#include "hardware/pio.h"
#endif

// -------------------- //
// audio_i2s_master_out //
// -------------------- //

#define audio_i2s_master_out_wrap_target 0
#define audio_i2s_master_out_wrap 7

#define audio_i2s_master_out_offset_entry_point 7u

static const uint16_t audio_i2s_master_out_program_instructions[] = {
            //     .wrap_target
    0x7001, //  0: out    pins, 1         side 2     
    0x1840, //  1: jmp    x--, 0          side 3     
    0x6001, //  2: out    pins, 1         side 0     
    0xa822, //  3: mov    x, y            side 1     
    0x6001, //  4: out    pins, 1         side 0     
    0x0844, //  5: jmp    x--, 4          side 1     
    0x7001, //  6: out    pins, 1         side 2     
    0xb822, //  7: mov    x, y            side 3     
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program audio_i2s_master_out_program = {
    .instructions = audio_i2s_master_out_program_instructions,
    .length = 8,
    .origin = -1,
};

static inline pio_sm_config audio_i2s_master_out_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + audio_i2s_master_out_wrap_target, offset + audio_i2s_master_out_wrap);
    sm_config_set_sideset(&c, 2, false, false);
    return c;
}
#endif

