// GRID PULSE - TinyUSB configuration.
//
// A single CDC-ACM interface, device mode only. The FIFO sizes are chosen against
// the actual traffic: a protocol line is at most 320 bytes and the busiest moment of
// a run produces a HIT and a TARGET back to back plus a 4 Hz tick, so a 512-byte
// transmit FIFO holds well over a second of output if the host stalls momentarily.

#ifndef GRIDPULSE_TUSB_CONFIG_H_
#define GRIDPULSE_TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU OPT_MCU_RP2040
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

// Enabled classes. CDC only: this device is a serial port, nothing more.
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

// Transmit is the busy direction; receive only ever carries short commands.
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif  // GRIDPULSE_TUSB_CONFIG_H_
