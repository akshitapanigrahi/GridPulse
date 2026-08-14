// GRID PULSE - WS2812B LED grid driver.
//
// PURPOSE
//   Drives the 25-pixel strip via PIO and DMA. The public interface speaks in GRID
//   CELLS; the serpentine strip mapping is applied here and nowhere else, using
//   kCellToPixel from board_map.h.
//
// INVARIANTS
//   - Never more than one cell lit during a run. Enforced by the callers' use of
//     SetTarget(), which clears the previous target before lighting the new one.
//   - Show() never blocks. It starts a DMA transfer and returns; the waveform is
//     produced entirely by the PIO state machine. A frame is pushed immediately on a
//     hit rather than waiting for a periodic tick, so the visible latency is the
//     380 us the strip takes to clock in and nothing else.
//   - No dynamic allocation. The pixel buffer is a fixed static array.
//
// POWER
//   Every colour passes through the global brightness setting in config.h. See
//   docs/HARDWARE.md: 25 pixels at full white would draw about 1.5 A, well beyond
//   the 500 mA a USB port must supply. Full output is safe only because the game and
//   self-test use SetOnly(), which enforces one lit pixel at a time. Any future
//   multi-pixel presentation needs an aggregate-current limiter first.
//
// COLOUR AND ACCESSIBILITY
//   Hit and miss feedback never relies on hue alone. The colours below differ in
//   LUMINANCE as well as hue, and the animations differ in shape, so the game is
//   playable with any colour vision deficiency. See docs/GAME_CORE.md section 5.

#ifndef GRIDPULSE_LEDS_H_
#define GRIDPULSE_LEDS_H_

#include <cstddef>
#include <cstdint>

#include "../../include/board_map.h"

namespace gridpulse {

struct Rgb {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

// The palette. Chosen so that every pair a player must distinguish differs in
// luminance as well as hue.
inline constexpr Rgb kColourOff{0, 0, 0};
// Use the same blue/teal as the idle walk so the active target matches the colour
// already established by the physical device before a run.
inline constexpr Rgb kColourTarget{0, 90, 60};
inline constexpr Rgb kColourHit{0, 200, 255};       // cyan, bright
inline constexpr Rgb kColourMiss{255, 110, 0};      // amber, dimmer than a hit
inline constexpr Rgb kColourCountdown{255, 180, 0};  // yellow, and never a target
inline constexpr Rgb kColourSelfTest{160, 0, 255};  // violet, unmistakably not play
inline constexpr Rgb kColourReady{0, 90, 60};       // idle walk; also the target hue

class LedGrid {
 public:
  // Claims a PIO state machine and a DMA channel. Call once at startup.
  //
  // Returns false if no PIO program space or DMA channel is available, which is a
  // startup fault the caller must surface rather than continue past.
  bool Init();

  // Sets one grid cell. The serpentine mapping and the brightness cap are applied
  // here; callers never see strip indices.
  void SetCell(std::size_t cell, const Rgb& colour);

  void Clear();

  // Lights exactly one cell and extinguishes every other. This is the call the game
  // loop uses, so "only one LED at a time" is a property of the interface rather
  // than a rule callers have to remember.
  void SetOnly(std::size_t cell, const Rgb& colour);

  // Starts the DMA transfer. Non-blocking.
  //
  // If a previous frame is still clocking out, this returns false and the frame is
  // dropped: presentation frames are droppable, and blocking here would stall the
  // key scan. Scoring never depends on a frame being sent.
  bool Show();

  // True once the previous frame has finished clocking out and the strip's reset
  // latch has elapsed.
  bool Ready() const;

  // Runtime brightness, as a percentage of kGlobalBrightnessCap.
  void SetBrightnessPercent(std::uint8_t percent);
  std::uint8_t brightness_percent() const { return brightness_percent_; }

 private:
  std::uint32_t Encode(const Rgb& colour) const;

  std::uint32_t pixels_[kCellCount] = {};
  int dma_channel_ = -1;
  unsigned pio_sm_ = 0;
  std::uint64_t frame_done_us_ = 0;
  std::uint8_t brightness_percent_ = 100;
  bool initialised_ = false;
};

}  // namespace gridpulse

#endif  // GRIDPULSE_LEDS_H_
