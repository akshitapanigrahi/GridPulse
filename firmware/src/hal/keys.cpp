#include "keys.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#include "../../include/board_map.h"

namespace gridpulse {

void KeysInit() {
  for (std::size_t cell = 0; cell < kCellCount; ++cell) {
    const std::uint8_t gpio = kCellToGpio[cell];
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);
  }

  // The pull-ups take a moment to charge the pin capacitance. Reading before they
  // settle would show every key as pressed and the boot-time stuck-key detection
  // would condemn the whole board.
  //
  // GP26 and GP27 are ADC-capable pins used here as plain digital inputs. Nothing in
  // this firmware enables the ADC or selects an analogue function on them, so the
  // digital input buffer and the pull-up behave exactly as on any other pin.
  busy_wait_us(1000);
}

std::uint32_t KeysReadRaw() {
  // One 32-bit load reads all 25 switches simultaneously, which also means every key
  // in a scan shares a single timestamp - there is no skew between the first and
  // last key of a chord.
  return gpio_get_all();
}

}  // namespace gridpulse
