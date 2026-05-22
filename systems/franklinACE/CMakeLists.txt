# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(franklinACE
	${CMAKE_CURRENT_SOURCE_DIR}/src/franklinACE.c
	${CMAKE_CURRENT_SOURCE_DIR}/src/utils.S
	${CMAKE_CURRENT_SOURCE_DIR}/../../src/hid_app.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio.c
)

target_compile_options(franklinACE PRIVATE -Wall)

if(PICO_RP2350)
	target_compile_definitions(franklinACE PRIVATE
		PICO_RP2350=1
		PICO_RP2040=0
		DVI_DEFAULT_SERIAL_CONFIG=olimex_rp2350pc_cfg
		PICO_DEFAULT_UART=0
		PICO_DEFAULT_UART_TX_PIN=0
		PICO_DEFAULT_UART_RX_PIN=1
	)
else()
	target_compile_definitions(franklinACE PRIVATE
		PICO_RP2040=1
		PICO_RP2350=0
		# DVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg
		# DVI_DEFAULT_SERIAL_CONFIG=olimex_neo6502_cfg
		DVI_DEFAULT_SERIAL_CONFIG=olimex_rp2040pc_cfg
		# DVI_DEFAULT_SERIAL_CONFIG=pico_zero_cfg
	)
endif()

target_link_libraries(franklinACE
	pico_stdlib
	pico_multicore
	pico_util
	libdvi
	tinyusb_host
)

# create map/bin/hex file etc.
pico_add_extra_outputs(franklinACE)

pico_enable_stdio_usb(franklinACE 0)
pico_enable_stdio_uart(franklinACE 1)
