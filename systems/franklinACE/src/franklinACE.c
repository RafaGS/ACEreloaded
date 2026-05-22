// franklinACE.c
//
// ## zlib/libpng license
//
// Copyright (c) 2023 Veselin Sladkov
// Copyright (c) 2026 RafaGS
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the
// use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//     1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software in a
//     product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
//     2. Altered source versions must be plainly marked as such, and must not
//     be misrepresented as being the original software.
//     3. This notice may not be removed or altered from any source
//     distribution.

#define CHIPS_IMPL

#define RGBA8(r, g, b) (0xFF000000 | (r << 16) | (g << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"

#include "roms/franklinACE_roms.h"
#include "images/f.h"

static uint8_t* franklinACE_nib_images[] = { fdisk_nib_image };

#include "tusb.h"

#include "chips/chips_common.h"
#ifdef OLIMEX_NEO6502
#include "chips/wdc65C02cpu.h"
#else
#include "chips/mos6502cpu.h"
#endif
#include "chips/beeper.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "chips/clk.h"
#include "devices/franklinACE_lc.h"
#include "devices/disk2_fdd.h"
#include "devices/disk2_fdc.h"
#include "devices/franklinACE_fdc_rom.h"
//#include "devices/prodos_hdd_msc.h"
//#include "devices/prodos_hdc.h"
//#include "devices/prodos_hdc_rom.h"
#include "systems/franklinACE.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "hardware/uart.h"
#include "hardware/interp.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"

#include "tmds_encode.h"

#include "common_dvi_pin_configs.h"
#include "dvi.h"
#include "dvi_serialiser.h"

#include "audio.h"

#if PICO_RP2350
#define FRANKLIN_DEBUG_UART uart0
#define FRANKLIN_DEBUG_UART_TX_PIN 0
#define FRANKLIN_DEBUG_UART_RX_PIN 1
#define FRANKLIN_DEBUG_UART_INIT_MSG "UART0 inicializado a 115200 baudios (GPIO00 TX, GPIO01 RX)\n"
#define FRANKLIN_DEBUG_PLATFORM_BANNER "\n\n=== Franklin ACE RP2350 (UART debug) ===\n"
#else
#define FRANKLIN_DEBUG_UART uart1
#define FRANKLIN_DEBUG_UART_TX_PIN 8
#define FRANKLIN_DEBUG_UART_RX_PIN 9
#define FRANKLIN_DEBUG_UART_INIT_MSG "UART1 inicializado a 115200 baudios (GPIO08 TX, GPIO09 RX)\n"
#define FRANKLIN_DEBUG_PLATFORM_BANNER "\n\n=== Franklin ACE RP2040 (UART debug) ===\n"
#endif

// Safe boot knobs for isolating core1 reset causes.
#define FRANKLIN_DEBUG_DISABLE_AUDIO_INIT 1
#define FRANKLIN_DEBUG_DISABLE_BUS_PRIORITY_TWEAK 1
#define FRANKLIN_DEBUG_IDLE_MAIN_LOOP 0
#define FRANKLIN_DEBUG_DISABLE_ACTIVE_RENDER 0
#define FRANKLIN_DEBUG_UART_STATS 0
// Perfil normal: DVI activo y emulación completa.
#define FRANKLIN_DEBUG_DISABLE_DVI 0
// Modo diagnóstico USB-only desactivado.
#define FRANKLIN_DEBUG_USB_ONLY_LOOP 0
// Solo se usa si FRANKLIN_DEBUG_USB_ONLY_LOOP=1.
#define FRANKLIN_DEBUG_SKIP_TUH_TASK 0
// Estrategia alternativa estable: teclado por UART (desde terminal/PC).
// 1 = UART bridge activo (USB host desactivado en runtime)
// 0 = USB HID host activo
#if PICO_RP2350
#define FRANKLIN_INPUT_UART_BRIDGE 0
#else
#define FRANKLIN_INPUT_UART_BRIDGE 1
#endif

typedef struct {
    uint32_t version;
    franklinACE_t franklinACE;
} franklinACE_snapshot_t;

typedef struct {
    franklinACE_t franklinACE;
    uint32_t frame_time_us;
    uint32_t ticks;
    // double emu_time_ms;
} state_t;

static state_t __not_in_flash() state;

// HardFault handler: imprime info por el UART de debug antes del reset
void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile (
        "mov r0, sp\n"
        "bl hardfault_handler_c\n"
    );
}
void hardfault_handler_c(uint32_t *sp) {
    uart_puts(FRANKLIN_DEBUG_UART, "\n!!! HARDFAULT !!!\n");
    uint32_t pc = sp[6];
    uint32_t lr = sp[5];
    uint32_t cfsr = *(volatile uint32_t*)0xE000ED28;
    uart_puts(FRANKLIN_DEBUG_UART, "PC=0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (pc >> (i*4)) & 0xF;
        uart_putc_raw(FRANKLIN_DEBUG_UART, nibble < 10 ? '0'+nibble : 'A'+nibble-10);
    }
    uart_puts(FRANKLIN_DEBUG_UART, " LR=0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (lr >> (i*4)) & 0xF;
        uart_putc_raw(FRANKLIN_DEBUG_UART, nibble < 10 ? '0'+nibble : 'A'+nibble-10);
    }
    uart_puts(FRANKLIN_DEBUG_UART, " CFSR=0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (cfsr >> (i*4)) & 0xF;
        uart_putc_raw(FRANKLIN_DEBUG_UART, nibble < 10 ? '0'+nibble : 'A'+nibble-10);
    }
    uart_puts(FRANKLIN_DEBUG_UART, "\n");
    sleep_ms(100);
    while(1);
}

// Audio streaming callback
static void audio_callback(const uint8_t sample, void *user_data) {
    (void)user_data;
#if FRANKLIN_DEBUG_DISABLE_AUDIO_INIT
    (void)sample;
    return;
#else
    audio_push_sample(sample);
#endif
}

// Get franklinACE_desc_t struct based on joystick type
franklinACE_desc_t franklinACE_desc(void) {
    return (franklinACE_desc_t){
        .fdc_enabled = true,
        .audio =
            {
                .callback = {.func = audio_callback},
                .sample_rate = 22050,
            },
        .roms =
            {
                .rom = {.ptr = franklinACE_rom, .size = sizeof(franklinACE_rom)},
                .character_rom = {.ptr = franklinACE_character_rom, .size = sizeof(franklinACE_character_rom)},
                .fdc_rom = {.ptr = franklinACE_fdc_rom, .size = sizeof(franklinACE_fdc_rom)},
            },
    };
}

void app_init(void) {
    franklinACE_desc_t desc = franklinACE_desc();
    franklinACE_init(&state.franklinACE, &desc);
}

// Align with the Oric RP2040 profile to avoid receiver-side scaling artifacts.
#define FRAME_WIDTH  800
#define FRAME_HEIGHT 480
#define VREG_VSEL    VREG_VOLTAGE_1_20
#define DVI_TIMING   dvi_timing_800x480p_60hz

uint32_t __not_in_flash() tmds_palette[PALETTE_SIZE * 6];
uint32_t __not_in_flash() empty_tmdsbuf[3 * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD];
uint32_t __not_in_flash() line_tmdsbuf[3 * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD];

uint8_t __not_in_flash() scanbuf[FRAME_WIDTH];
uint8_t __not_in_flash() video_fb[2][APPLE2_FRAMEBUFFER_SIZE];
volatile uint8_t video_fb_front = 0;

struct dvi_inst dvi0;

void tmds_palette_init() { tmds_setup_palette24_symbols(franklinACE_palette, tmds_palette, PALETTE_SIZE); }

void kbd_raw_key_down(int code) {
    if (code == 0) {
        return;
    }

    if (isascii(code)) {
        code = toupper(code);
    }

    if (code == 0x14F) {
        // Arrow right
        code = 0x15;
    } else if (code == 0x150) {
        // Arrow left
        code = 0x08;
    }

    franklinACE_t *sys = &state.franklinACE;

    switch (code) {
        case 0x13A:  // F1
        case 0x13B:  // F2
        case 0x13C:  // F3
        case 0x13D:  // F4
        case 0x13E:  // F5
        case 0x13F:  // F6
        case 0x140:  // F7
        case 0x141:  // F8
        case 0x142:  // F9
        {
            if (sys->fdc.valid) {
                uint8_t index = code - 0x13A;
                if (CHIPS_ARRAY_SIZE(franklinACE_nib_images) > index) {
                    disk2_fdd_insert_disk(&sys->fdc.fdd[0], franklinACE_nib_images[index]);
                }
            }
            break;
        }

        case 0x145:  // F12
            franklinACE_reset(sys);
            break;

        default:
            if (code < 128) {
                sys->kbd_last_key = code | 0x80;
            }
            break;
    }
    // printf("Key down: %d\n", code);
}

void kbd_raw_key_up(int code) {
    if (code == 0) {
        return;
    }

    if (isascii(code)) {
        code = toupper(code);
    }
    if (code < 128) {
        // Clear key latch to avoid stuck-repeat behavior.
        state.franklinACE.kbd_last_key = 0;
    }
}

void gamepad_state_update(uint8_t index, uint8_t hat_state, uint32_t button_state) {
    franklinACE_t *sys = &state.franklinACE;

    sys->paddl0 = 0x80;
    sys->paddl1 = 0x80;
    sys->paddl2 = 0x80;
    sys->paddl3 = 0x80;

    switch (hat_state) {
        case GAMEPAD_HAT_CENTERED:
            break;

        case GAMEPAD_HAT_UP:
            if (index == 0) {
                sys->paddl1 = 0x00;
            } else {
                sys->paddl3 = 0x00;
            }
            break;

        case GAMEPAD_HAT_UP_RIGHT:
            if (index == 0) {
                sys->paddl0 = 0xFF;
                sys->paddl1 = 0x00;
            } else {
                sys->paddl2 = 0xFF;
                sys->paddl3 = 0x00;
            }
            break;

        case GAMEPAD_HAT_RIGHT:
            if (index == 0) {
                sys->paddl0 = 0xFF;
            } else {
                sys->paddl2 = 0xFF;
            }
            break;

        case GAMEPAD_HAT_DOWN_RIGHT:
            if (index == 0) {
                sys->paddl0 = 0xFF;
                sys->paddl1 = 0xFF;
            } else {
                sys->paddl2 = 0xFF;
                sys->paddl3 = 0xFF;
            }
            break;

        case GAMEPAD_HAT_DOWN:
            if (index == 0) {
                sys->paddl1 = 0xFF;
            } else {
                sys->paddl3 = 0xFF;
            }
            break;

        case GAMEPAD_HAT_DOWN_LEFT:
            if (index == 0) {
                sys->paddl0 = 0x00;
                sys->paddl1 = 0xFF;
            } else {
                sys->paddl2 = 0x00;
                sys->paddl3 = 0xFF;
            }
            break;

        case GAMEPAD_HAT_LEFT:
            if (index == 0) {
                sys->paddl0 = 0x00;
            } else {
                sys->paddl2 = 0x00;
            }
            break;

        case GAMEPAD_HAT_UP_LEFT:
            if (index == 0) {
                sys->paddl0 = 0x00;
                sys->paddl1 = 0x00;
            } else {
                sys->paddl2 = 0x00;
                sys->paddl3 = 0x00;
            }
            break;

        default:
            break;
    }

    sys->butn0 = false;
    sys->butn1 = false;
    sys->butn2 = false;

    if (button_state & GAMEPAD_BUTTON_A) {
        if (index == 0) {
            sys->butn0 = true;
        } else {
            sys->butn2 = true;
        }
    }
    if (button_state & GAMEPAD_BUTTON_B) {
        if (index == 0) {
            sys->butn1 = true;
        }
    }
    // printf("Gamepad state update: %d %d %d\n", index, hat_state, button_state);
}

extern void franklinACE_render_scanline(const uint32_t *pixbuf, uint32_t *scanbuf, size_t n_pix);
extern void copy_tmdsbuf(uint32_t *dest, const uint32_t *src);

static inline void __not_in_flash_func(render_scanline)(const uint32_t *pixbuf, uint32_t *scanbuf, size_t n_pix) {
    interp_config c;

    c = interp_default_config();
    interp_config_set_cross_result(&c, true);
    interp_config_set_shift(&c, 0);
    interp_config_set_mask(&c, 0, 3);
    interp_config_set_signed(&c, false);
    interp_set_config(interp0, 0, &c);

    c = interp_default_config();
    interp_config_set_cross_result(&c, false);
    interp_config_set_shift(&c, 4);
    interp_config_set_mask(&c, 0, 31);
    interp_config_set_signed(&c, false);
    interp_set_config(interp0, 1, &c);

    franklinACE_render_scanline(pixbuf, scanbuf, n_pix);
}

#define APPLE2_EMPTY_LINES   ((FRAME_HEIGHT - APPLE2_SCREEN_HEIGHT * 2) / 4)
#define APPLE2_EMPTY_COLUMNS ((FRAME_WIDTH - APPLE2_SCREEN_WIDTH) / 2)

static inline void __not_in_flash_func(render_empty_scanlines)() {
    for (int y = 0; y < APPLE2_EMPTY_LINES; y += 2) {
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        copy_tmdsbuf(tmdsbuf, empty_tmdsbuf);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);

        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        copy_tmdsbuf(tmdsbuf, empty_tmdsbuf);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
}

static inline void __not_in_flash_func(render_empty_frame)() {
    for (int y = 0; y < FRAME_HEIGHT; y++) {
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        copy_tmdsbuf(tmdsbuf, empty_tmdsbuf);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
}
static inline void __not_in_flash_func(render_frame)() {
    uint8_t front = video_fb_front;
    const uint8_t *fb = video_fb[front];
    // Match Oric pipeline: emit one TMDS line per source line.
    for (int y = 0; y < APPLE2_SCREEN_HEIGHT; y += 2) {
        uint32_t *tmdsbuf;

        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        memset(scanbuf, 0, sizeof(scanbuf));
        render_scanline((const uint32_t *)(&fb[y * 280]),
                        (uint32_t *)(&scanbuf[APPLE2_EMPTY_COLUMNS]), 280);
        tmds_encode_palette_data((const uint32_t *)scanbuf, tmds_palette, tmdsbuf, FRAME_WIDTH, PALETTE_BITS);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);

        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        memset(scanbuf, 0, sizeof(scanbuf));
        render_scanline((const uint32_t *)(&fb[(y + 1) * 280]),
                        (uint32_t *)(&scanbuf[APPLE2_EMPTY_COLUMNS]), 280);
        tmds_encode_palette_data((const uint32_t *)scanbuf, tmds_palette, tmdsbuf, FRAME_WIDTH, PALETTE_BITS);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
}

void __not_in_flash_func(core1_main()) {
#if !FRANKLIN_DEBUG_DISABLE_AUDIO_INIT
    audio_init(_AUDIO_PIN, 22050);
#endif

    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);

    uint32_t render_count = 0;
    while (1) {
#if !FRANKLIN_DEBUG_DISABLE_ACTIVE_RENDER
        render_empty_scanlines();
        render_frame();
        render_empty_scanlines();
#else
        render_empty_frame();
#endif
    render_count++;
    }

    __builtin_unreachable();
}

void app_main_loop(void) {
    uint32_t frame_count = 0;
    while (1) {
#if FRANKLIN_DEBUG_USB_ONLY_LOOP
        frame_count++;
#if FRANKLIN_DEBUG_SKIP_TUH_TASK
        if ((frame_count % 500u) == 0u) {
            uart_puts(FRANKLIN_DEBUG_UART, "hb: usb init ok, tuh_task omitido\n");
        }
        sleep_us(2000);
        continue;
#else
#if !FRANKLIN_INPUT_UART_BRIDGE
        if ((frame_count & 3u) == 0u) {
            tuh_task();
        }
        if ((frame_count % 500u) == 0u) {
            uart_puts(FRANKLIN_DEBUG_UART, "hb: tuh_task activo\n");
        }
#endif
        sleep_us(2000);
        continue;
    #endif
#endif

        uint32_t start_time_in_micros = time_us_32();

        uint32_t num_ticks = 19968;
        for (uint32_t ticks = 0; ticks < num_ticks; ticks++) {
            franklinACE_tick(&state.franklinACE);
        }

        frame_count++;

#if !FRANKLIN_DEBUG_DISABLE_DVI
        franklinACE_screen_update(&state.franklinACE);
        uint8_t back = video_fb_front ^ 1u;
        memcpy(video_fb[back], state.franklinACE.fb, APPLE2_FRAMEBUFFER_SIZE);
        video_fb_front = back;
#endif
        kbd_update(&state.franklinACE.kbd, 19968);
        // Reduce USB host pressure when DVI is active.
    #if !FRANKLIN_INPUT_UART_BRIDGE
    #if !FRANKLIN_DEBUG_DISABLE_DVI
        if ((frame_count & 7u) == 0u) {
    #else
        if ((frame_count & 3u) == 0u) {
    #endif
            tuh_task();
        }
    #endif

        uint32_t end_time_in_micros = time_us_32();
        uint32_t execution_time = end_time_in_micros - start_time_in_micros;
        int sleep_time = 19968 - (int) execution_time;
        if (sleep_time > 0) {
            sleep_us((uint32_t) sleep_time);
        }
    }
}

int main() {
    // Inicialización mínima del UART de debug
    uart_init(FRANKLIN_DEBUG_UART, 115200);
    gpio_set_function(FRANKLIN_DEBUG_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(FRANKLIN_DEBUG_UART_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(FRANKLIN_DEBUG_UART_RX_PIN);
    uart_puts(FRANKLIN_DEBUG_UART, FRANKLIN_DEBUG_PLATFORM_BANNER);
    uart_puts(FRANKLIN_DEBUG_UART, FRANKLIN_DEBUG_UART_INIT_MSG);
    if (watchdog_caused_reboot()) {
        uart_puts(FRANKLIN_DEBUG_UART, "reset: watchdog\n");
    } else {
        uart_puts(FRANKLIN_DEBUG_UART, "reset: power-on\n");
    }

    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    sleep_ms(100);
    // Reconfigura UART tras cambio de clock
    gpio_set_function(FRANKLIN_DEBUG_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(FRANKLIN_DEBUG_UART_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(FRANKLIN_DEBUG_UART_RX_PIN);
    uart_set_baudrate(FRANKLIN_DEBUG_UART, 115200);
    uart_puts(FRANKLIN_DEBUG_UART, "System clock changed to DVI timing\n");

    stdio_init_all();
    uart_puts(FRANKLIN_DEBUG_UART, "stdio_init_all listo\n");
#if !FRANKLIN_INPUT_UART_BRIDGE
    // Keep USB host IRQ below video DMA path when DVI is enabled.
    irq_set_priority(USBCTRL_IRQ, 0xC0);
    tusb_init();
    uart_puts(FRANKLIN_DEBUG_UART, "tusb_init done\n");
    sleep_ms(500);
    uart_puts(FRANKLIN_DEBUG_UART, "post-tusb delay done\n");
#else
    uart_puts(FRANKLIN_DEBUG_UART, "input mode: UART bridge (USB host off)\n");
#endif

#if !FRANKLIN_DEBUG_DISABLE_DVI
    uart_puts(FRANKLIN_DEBUG_UART, "configurando DVI...\n");
    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    uart_puts(FRANKLIN_DEBUG_UART, "dvi_init done\n");

    tmds_palette_init();
    memset(scanbuf, 0, sizeof(scanbuf));
    tmds_encode_palette_data((const uint32_t *)scanbuf, tmds_palette, empty_tmdsbuf, FRAME_WIDTH, PALETTE_BITS);
    uart_puts(FRANKLIN_DEBUG_UART, "tmds palette inicializada\n");

    uart_puts(FRANKLIN_DEBUG_UART, "about to launch core1\n");
#if !FRANKLIN_DEBUG_DISABLE_BUS_PRIORITY_TWEAK
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
#endif
    multicore_launch_core1(core1_main);
    uart_puts(FRANKLIN_DEBUG_UART, "core1 lanzado\n");
#else
    uart_puts(FRANKLIN_DEBUG_UART, "DVI deshabilitado (modo debug USB)\n");
#endif

    app_init();
    memset(video_fb[0], 0, APPLE2_FRAMEBUFFER_SIZE);
    memset(video_fb[1], 0, APPLE2_FRAMEBUFFER_SIZE);
    memcpy(video_fb[video_fb_front], state.franklinACE.fb, APPLE2_FRAMEBUFFER_SIZE);
    uart_puts(FRANKLIN_DEBUG_UART, "app_init done\n");

    uart_puts(FRANKLIN_DEBUG_UART, "entrando en bucle principal\n");
    app_main_loop();

    __builtin_unreachable();
}
