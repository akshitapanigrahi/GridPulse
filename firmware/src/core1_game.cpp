#include "core1_game.h"

#include "hardware/structs/rosc.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include "hal/flashstore.h"
#include "hal/keys.h"
#include "hal/leds.h"
#include "pure/fsm.h"
#include "pure/keyscan.h"
#include "pure/runevents.h"
#include "pure/selftest.h"

namespace gridpulse {

SpscQueue<Event, kEventQueueDepth> g_event_queue;
SpscQueue<Command, kCommandQueueDepth> g_command_queue;

namespace {

// What core 1 is currently doing. Distinct from GameState, which describes a run.
enum class DeviceMode : std::uint8_t {
  kIdle,
  kSelfTest,
  kPlaying,
};

// Attract mode walks a single dim pixel around the grid so an idle board looks
// alive. One cell at a time, like everything else here.
constexpr std::uint64_t kAttractStepUs = 120000;

LedGrid g_leds;
KeyScanner g_scanner;
Game g_game;
SelfTest g_selftest;

DeviceMode g_device_mode = DeviceMode::kIdle;
HealthMask g_health;
bool g_leds_ok = false;

std::uint64_t g_last_scan_us = 0;
std::uint64_t g_last_tick_us = 0;
std::uint64_t g_attract_step_us = 0;
std::size_t g_attract_cell = 0;

// Deadline until which the target cell is shown in the miss colour instead of the
// target colour. Zero means no flash in progress.
std::uint64_t g_miss_flash_until_us = 0;

// Deadline until which a repeated target is held dark before relighting, so a repeat
// reads as flash-then-relight rather than as nothing happening.
std::uint64_t g_repeat_dark_until_us = 0;

// What the LEDs are currently showing, so a frame is only pushed when it changes.
std::size_t g_shown_cell = kCellCount;
Rgb g_shown_colour = kColourOff;
bool g_shown_valid = false;

// Pushes an event, or counts it as dropped. Never blocks.
//
// Dropping is survivable because the device's own END tally is computed from state
// the host never touches: a lost event makes the live display wrong, not the score.
// The host surfaces the resulting sequence gap.
void Emit(const Event& event) {
  g_event_queue.Push(event);
}

// Draws a 32-bit seed from the ring oscillator's random bit register.
//
// The ROSC runs asynchronously to the system clock, so its low bit is genuinely
// unpredictable. Bits are sampled well apart to avoid correlation from the sampling
// itself. This is the only source of the seed: docs/PROTOCOL.md section 6 explains
// that the host has no command that can supply, bias or replace it.
std::uint32_t HardwareSeed() {
  std::uint32_t seed = 0;
  for (int bit = 0; bit < 32; ++bit) {
    seed = (seed << 1) | (rosc_hw->randombit & 1u);
    busy_wait_us(5);
  }
  return seed;
}

void ShowNothing() {
  if (g_shown_valid && g_shown_cell == kCellCount) {
    return;
  }
  g_leds.Clear();
  g_leds.Show();
  g_shown_cell = kCellCount;
  g_shown_colour = kColourOff;
  g_shown_valid = true;
}

// Lights exactly one cell. The single-lit-LED invariant is a property of this being
// the only function that ever turns an LED on.
void ShowOnly(std::size_t cell, const Rgb& colour) {
  if (g_shown_valid && g_shown_cell == cell && g_shown_colour.r == colour.r &&
      g_shown_colour.g == colour.g && g_shown_colour.b == colour.b) {
    return;
  }
  g_leds.SetOnly(cell, colour);
  if (g_leds.Show()) {
    g_shown_cell = cell;
    g_shown_colour = colour;
    g_shown_valid = true;
  } else {
    // The frame was dropped because the previous one is still clocking out. Forget
    // what is displayed so the next pass retries rather than believing it succeeded.
    g_shown_valid = false;
  }
}

void EmitMode(GameState state, std::uint64_t now_us) {
  Emit(MakeMode(now_us, g_game.mode(), state, g_game.seed(),
                static_cast<std::uint8_t>(g_game.alphabet_size())));
}

void EmitHello(std::uint64_t now_us) {
  Emit(MakeHello(now_us, static_cast<std::uint8_t>(g_game.alphabet_size()), g_leds_ok));
}

void PresentSelfTestCell(std::uint64_t now_us) {
  (void)now_us;
  const std::size_t cell = g_selftest.current_cell();
  if (cell >= kCellCount) {
    ShowNothing();
    return;
  }
  ShowOnly(cell, kColourSelfTest);
}

void FinishSelfTest(std::uint64_t now_us) {
  g_health = g_selftest.mask();

  const bool stored = FlashStoreHealthMask(g_health);
  if (!stored) {
    Emit(MakeLog(now_us, LogLevel::kWarn, "health_mask_not_persisted"));
  }

  if (!g_game.Configure(g_health)) {
    // Fewer than three working cells: no positive bit rate exists, so the device
    // must refuse to run rather than report a meaningless number.
    Emit(MakeLog(now_us, LogLevel::kError, "too_few_healthy_cells_to_play"));
  }

  g_device_mode = DeviceMode::kIdle;
  ShowNothing();
  EmitHello(now_us);
}

void StartSelfTest(bool force, std::uint64_t now_us) {
  g_selftest.Begin(now_us, g_scanner.stuck_mask(), force, g_health);
  g_device_mode = DeviceMode::kSelfTest;
  Emit(MakeMode(now_us, g_game.mode(), GameState::kIdle, 0,
                static_cast<std::uint8_t>(g_game.alphabet_size())));
  if (g_selftest.complete()) {
    FinishSelfTest(now_us);
  } else {
    PresentSelfTestCell(now_us);
  }
}

void HandleSelfTestOutcome(const SelfTestOutcome& outcome, std::uint64_t now_us) {
  if (!outcome.valid) {
    return;
  }
  Emit(MakeSelfTest(now_us, static_cast<std::uint8_t>(outcome.cell),
                    GpioForCell(outcome.cell), PixelForCell(outcome.cell), outcome.result,
                    outcome.pass));
  if (g_selftest.complete()) {
    FinishSelfTest(now_us);
  } else {
    PresentSelfTestCell(now_us);
  }
}

void StartRun(RunMode mode, std::uint64_t now_us) {
  if (!g_game.Configure(g_health)) {
    Emit(MakeLog(now_us, LogLevel::kError, "alphabet_too_small_to_start"));
    return;
  }
  const std::uint32_t seed = HardwareSeed();
  if (!g_game.Start(mode, seed, now_us)) {
    Emit(MakeLog(now_us, LogLevel::kWarn, "start_refused_run_in_progress"));
    return;
  }
  g_device_mode = DeviceMode::kPlaying;
  g_miss_flash_until_us = 0;
  g_repeat_dark_until_us = 0;
  g_last_tick_us = now_us;
  EmitMode(GameState::kCountdown, now_us);
}

void EmitEndAndHistogram(std::uint64_t now_us) {
  RunReport report;
  g_game.BuildReport(&report);
  Emit(MakeEnd(now_us, report));

  // The histogram travels as its own event because 25 counts plus the tally would
  // exceed a protocol line. Core 0 splits it into chunks on the way out.
  Emit(MakeHist(now_us, report));
}

// The one end sequence, shared by the two ways a run can finish: the clock expiring
// (AdvanceRun reports `ended` and leaves MODE to this function) and ABORT, which never
// goes through AdvanceRun at all. Both produce MODE ENDED, END, HIST, exactly once.
void FinishRun(std::uint64_t now_us) {
  EmitMode(GameState::kEnded, now_us);
  EmitEndAndHistogram(now_us);
  g_device_mode = DeviceMode::kIdle;
  ShowNothing();
}

void HandleCommand(const Command& command, std::uint64_t now_us) {
  switch (command.name) {
    case CommandName::kPing:
    case CommandName::kProto:
      EmitHello(now_us);
      break;

    case CommandName::kStart:
      if (g_device_mode == DeviceMode::kIdle) {
        StartRun(command.mode, now_us);
      } else {
        Emit(MakeLog(now_us, LogLevel::kWarn, "start_ignored_device_busy"));
      }
      break;

    case CommandName::kAbort:
      if (!AbortShouldApply(command, g_device_mode == DeviceMode::kSelfTest)) {
        break;
      }
      if (g_device_mode == DeviceMode::kPlaying) {
        const GameState before = g_game.state();
        g_game.Abort(now_us);
        if (before == GameState::kCountdown) {
          // The clock never started, so there is no run to report.
          EmitMode(GameState::kIdle, now_us);
          g_device_mode = DeviceMode::kIdle;
          ShowNothing();
        } else {
          FinishRun(now_us);
        }
      } else if (g_device_mode == DeviceMode::kSelfTest) {
        g_device_mode = DeviceMode::kIdle;
        ShowNothing();
        EmitHello(now_us);
      }
      break;

    case CommandName::kSelfTest:
      if (g_device_mode != DeviceMode::kPlaying) {
        StartSelfTest(command.force, now_us);
      } else {
        Emit(MakeLog(now_us, LogLevel::kWarn, "selftest_ignored_run_in_progress"));
      }
      break;

    case CommandName::kBright:
      g_leds.SetBrightnessPercent(command.brightness_pct);
      g_shown_valid = false;  // force a repaint at the new brightness
      break;
  }
}

void HandleKeyDown(std::size_t cell, std::uint64_t now_us) {
  switch (g_device_mode) {
    case DeviceMode::kSelfTest:
      HandleSelfTestOutcome(g_selftest.OnKeyDown(cell, now_us), now_us);
      break;

    case DeviceMode::kPlaying: {
      const std::size_t target_before = g_game.target_cell();
      const PressResult result = g_game.Press(cell, now_us);
      if (result == PressResult::kHit) {
        Emit(MakeHit(now_us, static_cast<std::uint8_t>(cell), g_game.last_reaction_us(),
                     g_game.correct(), g_game.incorrect(), g_game.streak()));
        // The new target is already drawn and lit by Press(). Announce it here so
        // the host sees HIT then TARGET in the order they happened.
        Emit(MakeTarget(now_us, static_cast<std::uint8_t>(g_game.target_cell()),
                        g_game.draws(), g_game.target_is_repeat()));
        g_miss_flash_until_us = 0;
        // Sampling is with replacement, so the next target may be the cell just hit.
        // Hold it dark briefly so a repeat reads as flash-then-relight rather than
        // as nothing having happened. The target is NOT resampled to avoid this.
        g_repeat_dark_until_us =
            g_game.target_is_repeat() ? (now_us + kRepeatBlinkGapUs) : 0;
      } else if (result == PressResult::kMiss) {
        Emit(MakeMiss(now_us, static_cast<std::uint8_t>(cell),
                      static_cast<std::uint8_t>(target_before), g_game.correct(),
                      g_game.incorrect()));
        // Feedback on a miss flashes the TARGET amber rather than lighting the cell
        // that was wrongly pressed. That keeps exactly one LED lit, and it keeps the
        // player's eye on the cell they still have to hit.
        g_miss_flash_until_us = now_us + kMissFlashUs;
      }
      break;
    }

    case DeviceMode::kIdle:
      break;
  }
}

}  // namespace

void Core1Main() {
  // Lets core 0 park this core before it stalls XIP to write flash. Without this, a
  // flash write while core 1 is executing from flash hangs the device.
  multicore_lockout_victim_init();

  KeysInit();
  g_leds_ok = g_leds.Init();

  const std::uint64_t boot_us = time_us_64();
  g_scanner.Reset(KeysReadRaw(), boot_us);

  g_health = FlashLoadHealthMask();
  g_game.Configure(g_health);

  if (g_scanner.stuck_mask() != 0) {
    Emit(MakeLog(boot_us, LogLevel::kWarn, "switch_closed_at_boot"));
  }
  if (!g_leds_ok) {
    Emit(MakeLog(boot_us, LogLevel::kError, "led_driver_init_failed"));
  }
  EmitHello(boot_us);

  g_last_scan_us = boot_us;
  g_attract_step_us = boot_us;

  for (;;) {
    const std::uint64_t now = time_us_64();

    Command command;
    while (g_command_queue.Pop(&command)) {
      HandleCommand(command, now);
    }

    // --- key scan, the latency-critical path -------------------------------
    if (now - g_last_scan_us >= kScanIntervalUs) {
      g_last_scan_us = now;
      const std::uint32_t went_down = g_scanner.Scan(KeysReadRaw(), now);
      if (went_down != 0) {
        for (std::size_t cell = 0; cell < kCellCount; ++cell) {
          if ((went_down & (std::uint32_t{1} << cell)) != 0) {
            HandleKeyDown(cell, now);
          }
        }
      }
    }

    // --- time-driven transitions -------------------------------------------
    if (g_device_mode == DeviceMode::kSelfTest) {
      HandleSelfTestOutcome(g_selftest.Tick(now), now);
    } else if (g_device_mode == DeviceMode::kPlaying) {
      // What the transition means is decided in pure/runevents.cpp, where the native
      // suite can assert the exact wire sequence. This is only the plumbing.
      const RunAdvance advance = AdvanceRun(&g_game, now);
      for (std::size_t i = 0; i < advance.count; ++i) {
        Emit(advance.events[i]);
      }
      if (advance.ended) {
        FinishRun(now);
      }
    }

    // --- heartbeat ----------------------------------------------------------
    if (g_device_mode == DeviceMode::kPlaying && g_game.state() == GameState::kRunning &&
        now - g_last_tick_us >= kTickIntervalUs) {
      g_last_tick_us = now;
      Emit(MakeTick(now, g_game.ElapsedUs(now), g_game.correct(), g_game.incorrect(),
                    g_game.BitRateMbps(now)));
    }

    // --- presentation --------------------------------------------------------
    switch (g_device_mode) {
      case DeviceMode::kSelfTest:
        PresentSelfTestCell(now);
        break;

      case DeviceMode::kPlaying:
        if (g_game.state() == GameState::kCountdown) {
          // Pulse the centre cell once per second, on the board the player is looking
          // at. See kCountdownFlashes: three timed flashes, not a steady light that
          // would read as a target, and not a printed "3 2 1" that is only a guess at
          // where the device's clock has got to.
          const std::uint64_t into_countdown =
              kCountdownUs - g_game.CountdownRemainingUs(now);
          const std::uint64_t period = kCountdownUs / kCountdownFlashes;
          if ((into_countdown % period) < kCountdownFlashOnUs) {
            ShowOnly(kCellCount / 2, kColourCountdown);
          } else {
            ShowNothing();
          }
        } else if (g_game.state() == GameState::kRunning) {
          const std::size_t target = g_game.target_cell();
          if (target < kCellCount) {
            if (now < g_repeat_dark_until_us) {
              ShowNothing();
            } else if (now < g_miss_flash_until_us) {
              ShowOnly(target, kColourMiss);
            } else {
              ShowOnly(target, kColourTarget);
            }
          }
        }
        break;

      case DeviceMode::kIdle:
        if (now - g_attract_step_us >= kAttractStepUs) {
          g_attract_step_us = now;
          g_attract_cell = (g_attract_cell + 1) % kCellCount;
        }
        ShowOnly(g_attract_cell, kColourReady);
        break;
    }
  }
}

}  // namespace gridpulse
