#include "runevents.h"

namespace gridpulse {

RunAdvance AdvanceRun(Game* game, std::uint64_t now_us) {
  RunAdvance advance;

  const GameState before = game->state();
  advance.state = game->Tick(now_us);
  if (advance.state == before) {
    return advance;
  }

  // Tick() produces at most one transition per call: entering RUNNING sets t0 to
  // now, so the run cannot also be expired on the same call.
  switch (advance.state) {
    case GameState::kRunning:
      advance.began = true;
      advance.events[advance.count++] =
          MakeMode(now_us, game->mode(), advance.state, game->seed(),
                   static_cast<std::uint8_t>(game->alphabet_size()));
      // Tick() drew and lit the first target on its way into RUNNING. Every later
      // target is announced from the press path, when a hit draws the next one, so
      // this is the only place draw #1 ever reaches the host - and without it the
      // browser shows an unlit grid until the player finds the cell by hand.
      // MODE then TARGET, in the order they happened: docs/PROTOCOL.md section 7.
      advance.events[advance.count++] =
          MakeTarget(now_us, static_cast<std::uint8_t>(game->target_cell()),
                     game->draws(), game->target_is_repeat());
      break;

    case GameState::kEnded:
      // No events here on purpose. See RunAdvance::ended.
      advance.ended = true;
      break;

    case GameState::kIdle:
    case GameState::kCountdown:
      // Tick() never transitions into either of these.
      break;
  }

  return advance;
}

bool AbortShouldApply(const Command& command, bool self_testing) {
  return !command.host_detached || self_testing;
}

}  // namespace gridpulse
