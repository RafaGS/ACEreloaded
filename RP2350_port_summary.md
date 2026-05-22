# RP2350 Port Summary

## Cambios realizados respecto a la versión original

### 1. Soporte RP2350
- Añadido el build target RP2350 en `CMakeLists.txt` y `build_rp2350.sh`.
- En `systems/franklinACE/CMakeLists.txt` se activan las macros:
  - `PICO_RP2350=1`
  - `PICO_RP2040=0`
  - `DVI_DEFAULT_SERIAL_CONFIG=olimex_rp2350pc_cfg`
  - `PICO_DEFAULT_UART=0`, `PICO_DEFAULT_UART_TX_PIN=0`, `PICO_DEFAULT_UART_RX_PIN=1`
- Se habilita el perfil USB HID host en runtime para RP2350 (`FRANKLIN_INPUT_UART_BRIDGE=0`).

### 2. Vídeo DVI / RP2350
- Añadida configuración de pines `olimex_rp2350pc_cfg` en `lib/PicoDVI/software/include/common_dvi_pin_configs.h`.
- Ajustado `invert_diffpairs` para RP2350 a `false`.
- Introducida doble-buffer de framebuffer en `systems/franklinACE/src/franklinACE.c` para evitar artefactos verdes y carreras de lectura/escritura.

### 3. Teclado USB / TinyUSB host
- En `src/hid_app.c` se implementa:
  - montaje HID con `tuh_hid_set_protocol(..., HID_PROTOCOL_BOOT)`
  - recepción de reportes de teclas con soporte a 8 bytes boot report y 9 bytes con report-id
  - estado `prev_report` por instancia HID para evitar rebotes falsos en `keydown/keyup`
  - filtrado de código `0` para que no se inyecten NULs en el emulador
- Se corrigió la lectura de teclado en `src/systems/franklinACE.h` para devolver explícitamente `0x00` cuando no hay tecla en `0xC000`.

### 4. Depuración y limpieza
- Añadidas trazas temporales (`trace: ...`, `video hb`, `HID kbd ...`, `kbd dn/up`) durante la depuración.
- Luego se eliminaron esas trazas manteniendo los fixes funcionales.
- Eliminada la autopulsación de F1 al arranque en `systems/franklinACE/src/franklinACE.c`.

### 5. Otros fixes
- Corregido callback de audio cuando `FRANKLIN_DEBUG_DISABLE_AUDIO_INIT=1`.
- Ajustada la ruta de `kbd_raw_key_down/up` para F12 = reset y F1..F9 montan discos sólo si hay imagen válida.

## Qué se reutilizó de `Pico2MSX`

### 1. Soporte PicoDVI / RP2350
- La forma de definir configuraciones de pines TMDS y `DVI_DEFAULT_SERIAL_CONFIG`.
- La estructura de inicialización de `dvi0.ser_cfg` y el uso de `pio_set_gpio_base(...)` para RP2350.

### 2. TinyUSB HID host
- El patrón de callbacks `tuh_hid_mount_cb`, `tuh_hid_report_received_cb` y `tuh_hid_receive_report(...)` viene de la referencia.
- La idea de forzar protocolo BOOT cuando el dispositivo es un teclado HID.

### 3. Arquitectura de build / board
- La separación de perfiles RP2040 y RP2350, y el uso de las macros PICO/board config siguen el modelo de `Pico2MSX`.

## Qué es propio de Franklin

- El núcleo del emulador `franklinACE` y toda la lógica de emulación de Apple II.
- La integración del framebuffer y render pipeline con `franklinACE_render_scanline(...)`.
- El manejo de teclado interno `_franklinACE_mem_c000_c0ff_rw` y la lógica de entrada.
- El soporte de imágenes nibble para `disk2_fdd_insert_disk(...)`.

## Archivos clave modificados

- `systems/franklinACE/src/franklinACE.c`
- `systems/franklinACE/CMakeLists.txt`
- `src/hid_app.c`
- `src/systems/franklinACE.h`
- `lib/PicoDVI/software/include/common_dvi_pin_configs.h`
- `CMakeLists.txt`
- `build_rp2350.sh`
