// GRID PULSE - USB descriptors for the CDC-ACM interface.
//
// PURPOSE
//   Declares a single-interface USB CDC device. The host bridge opens it as a serial
//   port and speaks the line protocol in docs/PROTOCOL.md.
//
// VID:PID
//   2E8A:000A is Raspberry Pi's vendor ID with the Pico SDK's stock CDC product ID.
//   Keeping the stock pair means the device enumerates with the same in-box drivers
//   as any other Pico on macOS, Linux and Windows, and it is what run.sh and
//   host/gridpulse/serial_port.py auto-detect on.
//
//   The serial number is derived from the RP2040's unique flash ID, so two boards
//   plugged into the same machine are distinguishable.
//
// C rather than C++ because TinyUSB's callbacks are declared with C linkage.

#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#define USB_VID 0x2E8A
#define USB_PID 0x000A
#define USB_BCD 0x0200

// --- device descriptor ---------------------------------------------------------

static const tusb_desc_device_t kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,

    // Interface Association Descriptor: required so a CDC device with a control and
    // a data interface is recognised as one function.
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

const uint8_t* tud_descriptor_device_cb(void) {
  return (const uint8_t*)&kDeviceDescriptor;
}

// --- configuration descriptor ----------------------------------------------------

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t kConfigurationDescriptor[] = {
    // Configuration: 1 function, bus powered, 200 mA.
    //
    // At full output the one lit WS2812B may draw about 60 mA and the RP2040 about
    // 35 mA. Requesting 200 mA leaves margin for the strip's quiescent current while
    // remaining well below a USB 2.0 port's 500 mA configured-device budget. See
    // docs/HARDWARE.md for the full current budget.
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 200),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT,
                       EPNUM_CDC_IN, 64),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return kConfigurationDescriptor;
}

// --- string descriptors ------------------------------------------------------------

static char s_serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char* kStringDescriptors[] = {
    (const char[]){0x09, 0x04},  // 0: English (US)
    "Grid Pulse",                // 1: manufacturer
    "GRID PULSE 5x5 Keypad",     // 2: product
    s_serial,                    // 3: serial, filled in at first request
    "GRID PULSE CDC",            // 4: CDC interface
};

static uint16_t s_string_buffer[32];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;

  uint8_t char_count;

  if (index == 0) {
    memcpy(&s_string_buffer[1], kStringDescriptors[0], 2);
    char_count = 1;
  } else {
    if (index >= sizeof(kStringDescriptors) / sizeof(kStringDescriptors[0])) {
      return NULL;
    }

    if (index == 3 && s_serial[0] == '\0') {
      // Derived from the flash chip's unique ID, so two boards on one machine do not
      // collide.
      pico_get_unique_board_id_string(s_serial, sizeof(s_serial));
    }

    const char* text = kStringDescriptors[index];
    size_t length = strlen(text);
    // Leave room for the two-byte header within the fixed buffer.
    const size_t max_chars = (sizeof(s_string_buffer) / sizeof(uint16_t)) - 1;
    if (length > max_chars) {
      length = max_chars;
    }

    for (size_t i = 0; i < length; i++) {
      s_string_buffer[1 + i] = (uint16_t)text[i];
    }
    char_count = (uint8_t)length;
  }

  // First word is the length in bytes and the descriptor type.
  s_string_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * char_count + 2));
  return s_string_buffer;
}
