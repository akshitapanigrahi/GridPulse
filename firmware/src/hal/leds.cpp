#include "leds.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "ws2812.pio.h"

#include "../pure/config.h"

namespace gridpulse {
namespace {

// Not constexpr: pio0 expands to a reinterpret_cast from a fixed address, which is
// not a constant expression. A file-scope const is equivalent here and costs nothing.
PIO const kPio = pio0;
constexpr float kWs2812BitRateHz = 800000.0f;

// Bits per pixel on the wire, and the reset latch the strip needs after a frame.
constexpr std::uint32_t kBitsPerPixel = 24;
constexpr std::uint32_t kResetLatchUs = 300;

// Time for one full frame to clock out, plus the latch. 25 pixels * 24 bits at
// 800 kHz is 750 us; the latch adds 300. Computed rather than hardcoded so a change
// to the pixel count cannot silently break the timing.
constexpr std::uint64_t kFrameDurationUs =
    (kCellCount * kBitsPerPixel * 1000000ull) /
        static_cast<std::uint64_t>(kWs2812BitRateHz) +
    kResetLatchUs;

std::uint8_t ApplyCap(std::uint8_t channel, std::uint8_t percent) {
  // Two stages: the compile-time safety cap, then the runtime brightness. Both are
  // integer maths; a WS2812B has 8 bits per channel and no use for anything finer.
  const std::uint32_t capped =
      (static_cast<std::uint32_t>(channel) * kGlobalBrightnessCap) / 255u;
  return static_cast<std::uint8_t>((capped * percent) / 100u);
}

}  // namespace

bool LedGrid::Init() {
  if (initialised_) {
    return true;
  }

  if (!pio_can_add_program(kPio, &ws2812_program)) {
    return false;
  }
  const uint offset = pio_add_program(kPio, &ws2812_program);

  const int sm = pio_claim_unused_sm(kPio, false);
  if (sm < 0) {
    return false;
  }
  pio_sm_ = static_cast<unsigned>(sm);

  ws2812_program_init(kPio, pio_sm_, offset, kLedDataGpio, kWs2812BitRateHz);

  dma_channel_ = dma_claim_unused_channel(false);
  if (dma_channel_ < 0) {
    return false;
  }

  dma_channel_config config =
      dma_channel_get_default_config(static_cast<uint>(dma_channel_));
  channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
  channel_config_set_read_increment(&config, true);
  channel_config_set_write_increment(&config, false);
  channel_config_set_dreq(&config, pio_get_dreq(kPio, pio_sm_, true));
  dma_channel_configure(static_cast<uint>(dma_channel_), &config, &kPio->txf[pio_sm_],
                        pixels_, kCellCount, false);

  Clear();
  initialised_ = true;

  // Push one blank frame so the strip starts dark rather than showing whatever was
  // in its shift registers at power-up.
  Show();
  return true;
}

std::uint32_t LedGrid::Encode(const Rgb& colour) const {
  const std::uint8_t r = ApplyCap(colour.r, brightness_percent_);
  const std::uint8_t g = ApplyCap(colour.g, brightness_percent_);
  const std::uint8_t b = ApplyCap(colour.b, brightness_percent_);
  // WS2812B expects GRB, most significant bit first. The PIO autopulls 24 bits from
  // the top of the word, so the value is left-aligned into 32 bits.
  return (static_cast<std::uint32_t>(g) << 24) | (static_cast<std::uint32_t>(r) << 16) |
         (static_cast<std::uint32_t>(b) << 8);
}

void LedGrid::SetCell(std::size_t cell, const Rgb& colour) {
  if (cell >= kCellCount) {
    return;
  }
  // THE serpentine mapping, applied in exactly one place in the firmware.
  pixels_[kCellToPixel[cell]] = Encode(colour);
}

void LedGrid::Clear() {
  const std::uint32_t off = Encode(kColourOff);
  for (std::size_t i = 0; i < kCellCount; ++i) {
    pixels_[i] = off;
  }
}

void LedGrid::SetOnly(std::size_t cell, const Rgb& colour) {
  Clear();
  SetCell(cell, colour);
}

bool LedGrid::Ready() const {
  if (!initialised_) {
    return false;
  }
  if (dma_channel_is_busy(static_cast<uint>(dma_channel_))) {
    return false;
  }
  return time_us_64() >= frame_done_us_;
}

bool LedGrid::Show() {
  if (dma_channel_ < 0) {
    return false;
  }
  // Dropping a frame is acceptable; blocking is not. Core 1 must never wait on
  // presentation, because the same loop is scanning keys.
  if (!Ready() && initialised_) {
    return false;
  }
  dma_channel_set_read_addr(static_cast<uint>(dma_channel_), pixels_, true);
  frame_done_us_ = time_us_64() + kFrameDurationUs;
  return true;
}

void LedGrid::SetBrightnessPercent(std::uint8_t percent) {
  brightness_percent_ = (percent > 100) ? 100 : percent;
}

}  // namespace gridpulse
