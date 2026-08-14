/**
 * GRID PULSE - the scored session state machine (Mode B game core).
 *
 * Implements docs/GAME_CORE.md sections 4 and 5 (spec 1.0.0).
 *
 *     IDLE --start--> COUNTDOWN --3s--> RUNNING --60s--> ENDED
 *
 * TIME IS INJECTED, NEVER READ
 * ----------------------------
 * Every method takes an explicit `nowMs`. The session owns no timer and calls no
 * clock. That is what makes it deterministic under test and directly comparable to
 * the C++ implementation against the same golden vectors, and it is what lets the
 * caller timestamp a keypress inside the keydown handler BEFORE doing any rendering
 * work, so render cost never inflates a measured reaction time.
 *
 * The caller must supply performance.now() values. Date.now() is forbidden by the
 * spec: it is not monotonic and steps backwards on NTP correction, which would
 * corrupt t and therefore the score.
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

  var K_EVAL_DURATION_MS = 60000;
  var K_COUNTDOWN_MS = 3000;
  var K_MIN_ALPHABET_SIZE = 3;

  var STATE = {
    IDLE: 'IDLE',
    COUNTDOWN: 'COUNTDOWN',
    RUNNING: 'RUNNING',
    ENDED: 'ENDED'
  };

  var PRESS = {
    HIT: 'hit',
    MISS: 'miss',
    IGNORED: 'ignored'
  };

  /** Nearest-rank percentile over an unsorted array of numbers. */
  function percentile(values, p) {
    if (values.length === 0) { return 0; }
    var sorted = values.slice().sort(function (a, b) { return a - b; });
    var rank = Math.ceil((p / 100) * sorted.length) - 1;
    if (rank < 0) { rank = 0; }
    if (rank >= sorted.length) { rank = sorted.length - 1; }
    return sorted[rank];
  }

  /**
   * @constructor
   * @param {Object} config
   * @param {number} config.seed          unsigned 32-bit RNG seed
   * @param {Array<number>=} config.alphabet  selectable cells, ascending (default 0..24)
   * @param {number=} config.durationMs   scored window length (0 = untimed practice)
   * @param {number=} config.countdownMs  pre-run countdown, clock not running
   * @param {string=} config.mode         'EVAL' or 'PRACTICE', for the report only
   * @param {function(Object)=} config.onEvent  sink for scoring events (logging/UI)
   */
  function Session(config) {
    config = config || {};

    var alphabet = config.alphabet
      ? config.alphabet.slice().sort(function (a, b) { return a - b; })
      : [];
    if (!config.alphabet) {
      for (var c = 0; c < GP.alphabet.K_CELL_COUNT; c++) { alphabet.push(c); }
    }
    for (var i = 1; i < alphabet.length; i++) {
      if (alphabet[i] === alphabet[i - 1]) {
        throw new Error('alphabet contains duplicate cell ' + alphabet[i]);
      }
    }
    if (alphabet.length < K_MIN_ALPHABET_SIZE) {
      throw new Error(
        'alphabet size ' + alphabet.length + ' is below the minimum of ' +
        K_MIN_ALPHABET_SIZE + '; log2(N-1) would not be positive'
      );
    }

    this.alphabet = alphabet;
    this.n = alphabet.length;
    this.inAlphabet = Object.create(null);
    for (var j = 0; j < alphabet.length; j++) { this.inAlphabet[alphabet[j]] = true; }

    this.seed = (config.seed === undefined ? GP.rng.randomSeed() : config.seed) >>> 0;
    this.rng = new GP.rng.Xoshiro128StarStar(this.seed);
    this.durationMs = (config.durationMs === undefined)
      ? K_EVAL_DURATION_MS : config.durationMs;
    this.countdownMs = (config.countdownMs === undefined)
      ? K_COUNTDOWN_MS : config.countdownMs;
    this.mode = config.mode || 'EVAL';
    this.onEvent = config.onEvent || function () {};

    this.state = STATE.IDLE;
    this.correct = 0;
    this.incorrect = 0;
    this.streak = 0;
    this.maxStreak = 0;
    this.draws = 0;
    this.targetCell = -1;
    this.previousTargetCell = -1;
    this.targetIsRepeat = false;

    this.startCommandMs = 0;   // when START was issued (countdown origin)
    this.t0Ms = 0;             // when the FIRST target was presented (t origin)
    this.tPresentMs = 0;       // when the CURRENT target was presented
    this.endedAtMs = 0;

    this.pausedTotalMs = 0;
    this.pauseStartedMs = 0;
    this.isPaused = false;
    this.focusInterrupted = false;

    this.reactionTimesMs = [];
    this.perCellHits = new Array(GP.alphabet.K_CELL_COUNT).fill(0);
    this.perCellTargets = new Array(GP.alphabet.K_CELL_COUNT).fill(0);
    // Two distinct kinds of non-scoring press, kept apart because the results screen
    // discloses them and a merged count would make that disclosure inaccurate.
    this.ignoredKeyPresses = 0;   // keys outside the alphabet
    this.prematurePresses = 0;    // alphabet keys pressed when they cannot score
    this.repeatCount = 0;
  }

  /**
   * Record a key press that never reached press() because the input layer filtered
   * it as being outside the alphabet.
   *
   * Mode B resolves keys to cells at the browser boundary, so a press of a key that
   * is not one of the 25 has no cell to pass in. Without this the session would
   * under-report, and the results screen would tell the player that no out-of-
   * alphabet keys were pressed when some were.
   */
  Session.prototype.noteIgnoredKey = function () {
    this.ignoredKeyPresses++;
  };

  // -- lifecycle -------------------------------------------------------------

  /**
   * Transition to a new state and announce it.
   *
   * Every state change goes through here so that the MODE event is emitted exactly
   * once per transition and always before any event the new state produces - a
   * consumer replaying the log must never see a TARGET before the MODE that explains
   * which run it belongs to.
   */
  Session.prototype._setState = function (next, nowMs) {
    this.state = next;
    this._emit({ type: 'MODE', mode: this.mode, state: next, tMs: nowMs });
  };

  /** Enter COUNTDOWN. The clock does not start here; see docs/GAME_CORE.md 4.1. */
  Session.prototype.start = function (nowMs) {
    if (this.state !== STATE.IDLE) {
      throw new Error('cannot start from state ' + this.state);
    }
    this.startCommandMs = nowMs;
    if (this.countdownMs > 0) {
      this._setState(STATE.COUNTDOWN, nowMs);
    } else {
      this._beginRun(nowMs);
    }
    return this.state;
  };

  /** Milliseconds left in the countdown, or 0 once running. */
  Session.prototype.countdownRemainingMs = function (nowMs) {
    if (this.state !== STATE.COUNTDOWN) { return 0; }
    return Math.max(0, this.countdownMs - (nowMs - this.startCommandMs));
  };

  Session.prototype._beginRun = function (nowMs) {
    this.t0Ms = nowMs;
    this._setState(STATE.RUNNING, nowMs);
    this._present(nowMs);
  };

  /**
   * Advance time-driven transitions. Safe and cheap to call every animation frame.
   * Returns the current state.
   */
  Session.prototype.tick = function (nowMs) {
    if (this.state === STATE.COUNTDOWN && this.countdownRemainingMs(nowMs) <= 0) {
      this._beginRun(nowMs);
    }
    if (this.state === STATE.RUNNING && this._isExpired(nowMs)) {
      this.end(nowMs, 'COMPLETE');
    }
    return this.state;
  };

  Session.prototype._isExpired = function (nowMs) {
    if (this.durationMs <= 0) { return false; }   // untimed practice
    return this.elapsedMs(nowMs) >= this.durationMs;
  };

  Session.prototype.end = function (nowMs, reason) {
    if (this.state === STATE.ENDED) { return; }
    if (this.isPaused) { this.resume(nowMs); }
    // Freeze at exactly the scored duration so a run that ends between frames does
    // not report 60.017 s and quietly shave the final bit rate.
    this.endedAtMs = (this.durationMs > 0 && this.elapsedMs(nowMs) > this.durationMs)
      ? this.t0Ms + this.pausedTotalMs + this.durationMs
      : nowMs;
    this.targetCell = -1;
    this._setState(STATE.ENDED, this.endedAtMs);
    this._emit({ type: 'END', tMs: this.endedAtMs, reason: reason || 'COMPLETE' });
  };

  // -- pausing (focus loss) --------------------------------------------------

  /**
   * Pause the scored clock, e.g. when the window loses focus mid-run.
   *
   * Pausing is the fair thing to do - an OS notification stealing focus should not
   * wreck a run - but it does mean the official t is shorter than wall time, so the
   * run is flagged and the report carries BOTH figures. See report().
   */
  Session.prototype.pause = function (nowMs) {
    if (this.state !== STATE.RUNNING || this.isPaused) { return; }
    this.isPaused = true;
    this.pauseStartedMs = nowMs;
    this.focusInterrupted = true;
    this._emit({ type: 'PAUSE', tMs: nowMs });
  };

  Session.prototype.resume = function (nowMs) {
    if (!this.isPaused) { return; }
    this.pausedTotalMs += Math.max(0, nowMs - this.pauseStartedMs);
    this.isPaused = false;
    // The current target has been on screen the whole time the window was away, so
    // its reaction time is meaningless. Re-baseline it rather than logging a 30 s
    // outlier that would wreck the latency percentiles.
    this.tPresentMs = nowMs;
    this._emit({ type: 'RESUME', tMs: nowMs, pausedTotalMs: this.pausedTotalMs });
  };

  // -- time ------------------------------------------------------------------

  /** Scored elapsed time in ms: wall time since t0, minus any paused time. */
  Session.prototype.elapsedMs = function (nowMs) {
    if (this.state === STATE.IDLE || this.state === STATE.COUNTDOWN) { return 0; }
    var end = (this.state === STATE.ENDED) ? this.endedAtMs : nowMs;
    var paused = this.pausedTotalMs;
    if (this.isPaused) { paused += Math.max(0, end - this.pauseStartedMs); }
    return Math.max(0, (end - this.t0Ms) - paused);
  };

  /** Wall-clock elapsed time in ms, including any paused time. */
  Session.prototype.wallElapsedMs = function (nowMs) {
    if (this.state === STATE.IDLE || this.state === STATE.COUNTDOWN) { return 0; }
    var end = (this.state === STATE.ENDED) ? this.endedAtMs : nowMs;
    return Math.max(0, end - this.t0Ms);
  };

  Session.prototype.remainingMs = function (nowMs) {
    if (this.durationMs <= 0) { return Infinity; }
    return Math.max(0, this.durationMs - this.elapsedMs(nowMs));
  };

  // -- target presentation ---------------------------------------------------

  Session.prototype._present = function (nowMs) {
    var drawn = GP.rng.drawIndex(this.rng, this.n);
    var cell = this.alphabet[drawn.index];
    this.previousTargetCell = this.targetCell;
    this.targetIsRepeat = (cell === this.previousTargetCell);
    if (this.targetIsRepeat) { this.repeatCount++; }
    this.targetCell = cell;
    this.tPresentMs = nowMs;
    this.draws++;
    this.perCellTargets[cell]++;
    this._emit({
      type: 'TARGET',
      tMs: nowMs,
      cell: cell,
      idx: this.draws,
      repeat: this.targetIsRepeat
    });
    return cell;
  };

  // -- input -----------------------------------------------------------------

  /**
   * Apply one key-down naming a cell.
   *
   * @param {number} cell cell index, or -1 for a key outside the alphabet
   * @param {number} nowMs timestamp captured in the input handler, before rendering
   * @returns {string} PRESS.HIT | PRESS.MISS | PRESS.IGNORED
   *
   * IGNORED covers every press that must not touch the tally: keys outside the
   * alphabet, presses before the run starts or after it ends, and presses while
   * paused. Those are counted separately for the log but are neither Sc nor Si.
   */
  Session.prototype.press = function (cell, nowMs) {
    if (this.state !== STATE.RUNNING || this.isPaused) {
      this.prematurePresses++;
      return PRESS.IGNORED;
    }
    if (this._isExpired(nowMs)) {
      this.end(nowMs, 'COMPLETE');
      this.prematurePresses++;
      return PRESS.IGNORED;
    }
    if (!this.inAlphabet[cell]) {
      this.ignoredKeyPresses++;
      return PRESS.IGNORED;
    }

    if (cell === this.targetCell) {
      var reactionMs = nowMs - this.tPresentMs;
      this.correct++;
      this.streak++;
      if (this.streak > this.maxStreak) { this.maxStreak = this.streak; }
      this.perCellHits[cell]++;
      this.reactionTimesMs.push(reactionMs);
      this._emit({
        type: 'HIT',
        tMs: nowMs,
        cell: cell,
        reactionMs: reactionMs,
        sc: this.correct,
        si: this.incorrect,
        streak: this.streak
      });
      this._present(nowMs);
      return PRESS.HIT;
    }

    this.incorrect++;
    this.streak = 0;
    this._emit({
      type: 'MISS',
      tMs: nowMs,
      pressed: cell,
      target: this.targetCell,
      sc: this.correct,
      si: this.incorrect
    });
    return PRESS.MISS;
  };

  // -- readouts --------------------------------------------------------------

  /** The official cumulative bit rate over all elapsed session time. */
  Session.prototype.bitRate = function (nowMs) {
    return GP.scoring.bitRate(
      this.n, this.correct, this.incorrect, this.elapsedMs(nowMs) / 1000
    );
  };


  Session.prototype.accuracy = function () {
    var total = this.correct + this.incorrect;
    return (total === 0) ? 0 : this.correct / total;
  };

  Session.prototype.pressesPerSecond = function (nowMs) {
    var seconds = this.elapsedMs(nowMs) / 1000;
    return (seconds <= 0) ? 0 : (this.correct + this.incorrect) / seconds;
  };

  /**
   * The end-of-run report. Carries the four figures the assignment requires -
   * B, N, Sc, Si - plus the audit trail.
   *
   * bitRateWallclock is present so an interrupted run is auditable rather than
   * silently flattering: if focusInterrupted is false the two rates are identical.
   */
  Session.prototype.report = function (nowMs) {
    var elapsedS = this.elapsedMs(nowMs) / 1000;
    var wallS = this.wallElapsedMs(nowMs) / 1000;
    return {
      specVersion: '1.0.0',
      mode: this.mode,
      inputMode: 'keyboard',
      state: this.state,
      seed: this.seed,
      n: this.n,
      correct: this.correct,
      incorrect: this.incorrect,
      elapsedS: elapsedS,
      wallElapsedS: wallS,
      bitRate: GP.scoring.bitRate(this.n, this.correct, this.incorrect, elapsedS),
      bitRateMbps: GP.scoring.bitRateMbps(this.n, this.correct, this.incorrect, elapsedS),
      bitRateWallclock: GP.scoring.bitRate(this.n, this.correct, this.incorrect, wallS),
      bitsPerSelection: GP.scoring.bitsPerSelection(this.n),
      accuracy: this.accuracy(),
      pressesPerSecond: this.pressesPerSecond(nowMs),
      maxStreak: this.maxStreak,
      draws: this.draws,
      repeatCount: this.repeatCount,
      ignoredKeyPresses: this.ignoredKeyPresses,
      prematurePresses: this.prematurePresses,
      focusInterrupted: this.focusInterrupted,
      pausedMs: this.pausedTotalMs,
      reactionMs: {
        p50: percentile(this.reactionTimesMs, 50),
        p95: percentile(this.reactionTimesMs, 95),
        p99: percentile(this.reactionTimesMs, 99),
        min: this.reactionTimesMs.length ? Math.min.apply(null, this.reactionTimesMs) : 0,
        count: this.reactionTimesMs.length
      },
      perCellHits: this.perCellHits.slice(),
      perCellTargets: this.perCellTargets.slice()
    };
  };

  Session.prototype._emit = function (event) {
    this.onEvent(event);
  };

  GP.session = {
    Session: Session,
    STATE: STATE,
    PRESS: PRESS,
    percentile: percentile,
    K_EVAL_DURATION_MS: K_EVAL_DURATION_MS,
    K_COUNTDOWN_MS: K_COUNTDOWN_MS
  };
});
