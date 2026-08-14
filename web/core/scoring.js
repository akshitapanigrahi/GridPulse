/**
 * GRID PULSE - the achieved bit-rate metric.
 *
 * Implements docs/GAME_CORE.md section 3 (spec 1.0.0):
 *
 *     B = log2(N - 1) * max(Sc - Si, 0) / t        [bits per second]
 *
 * Two properties of this formula drive the whole game design and are worth stating
 * where the code lives:
 *
 *   1. `N - 1`, not `N`. One selection is reserved for error correction - without a
 *      backspace key, typing does not work. So N must be at least 3 for a positive
 *      rate, and at N=25 a correct selection is worth log2(24) = 4.585 bits.
 *   2. `Sc - Si`, not `Sc`. A miss does not merely fail to score, it cancels a hit.
 *      Combined with the time it consumes, a miss costs roughly twice what a hit
 *      earns, which is why the game never rewards mashing.
 *
 * `t` is ALL elapsed session time, never a rolling window. There is deliberately no
 * windowed variant: a second bit-rate figure on screen is an invitation to record the
 * wrong one, and the sparkline already shows how pace changed over the run.
 *
 * Classic script, no module system - see web/core/rng.js for why.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
  if (typeof module !== 'undefined' && module.exports) { module.exports = GP; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var K_MIN_ALPHABET_SIZE = 3;

  /**
   * Information credited to one correct selection: log2(N - 1).
   * Returns 0 for a non-scorable alphabet rather than -Infinity or NaN.
   */
  function bitsPerSelection(n) {
    if (n < K_MIN_ALPHABET_SIZE) { return 0; }
    return Math.log2(n - 1);
  }

  /**
   * The official bit rate, in bits per second.
   *
   * Guards return 0 rather than throwing, because the live HUD calls this every
   * animation frame - including before the first target is presented, when t is 0.
   *
   * @param {number} n alphabet size
   * @param {number} correct Sc
   * @param {number} incorrect Si
   * @param {number} elapsedS t, in seconds
   * @returns {number} bits per second, never negative, never NaN
   */
  function bitRate(n, correct, incorrect, elapsedS) {
    if (n < K_MIN_ALPHABET_SIZE) { return 0; }
    if (!(elapsedS > 0)) { return 0; }
    var net = correct - incorrect;
    if (net <= 0) { return 0; }
    return Math.log2(n - 1) * net / elapsedS;
  }

  /**
   * The bit rate as integer milli-bits per second.
   *
   * This is the form used on the wire and for device/host reconciliation, so that
   * float formatting differences between the RP2040 and the browser can never make
   * two agreeing implementations look like they disagree.
   */
  function bitRateMbps(n, correct, incorrect, elapsedS) {
    return Math.floor(bitRate(n, correct, incorrect, elapsedS) * 1000 + 0.5);
  }


  GP.scoring = {
    K_MIN_ALPHABET_SIZE: K_MIN_ALPHABET_SIZE,
    bitsPerSelection: bitsPerSelection,
    bitRate: bitRate,
    bitRateMbps: bitRateMbps
  };
});
