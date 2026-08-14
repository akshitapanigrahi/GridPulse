/**
 * GRID PULSE - Mode B transport: the keyboard, driving the in-browser game core.
 *
 * This is not a stand-in for the hardware. In Mode B there is no device, so running
 * the game core here IS running it as close to the input as it can possibly get -
 * adding a round trip through a local server would be pure latency for no benefit.
 * The score it produces is a real score.
 *
 * INPUT RULES (docs/GAME_CORE.md section 5), all enforced here:
 *
 *   - keydown only. keyup is never a selection.
 *   - event.repeat is discarded, so holding a key cannot farm selections.
 *   - matching is on event.code (physical key position), never event.key, so the
 *     game behaves identically on QWERTY, AZERTY and Dvorak.
 *   - a key outside the 25 is ignored entirely: not a hit, not a miss, no streak
 *     change, no effect on the score.
 *   - preventDefault() on the 25 game keys while a run is live, so a browser or OS
 *     shortcut cannot fire mid-run.
 *
 * TIMING. The press timestamp is taken with performance.now() on the first line of
 * the handler, before any lookup, any scoring and any rendering, so render cost can
 * never inflate a measured reaction time. Date.now() is never used: it is not
 * monotonic and would corrupt t if the system clock stepped.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  /**
   * @constructor
   * @param {Object=} options
   * @param {Array<number>=} options.alphabet selectable cells (default all 25)
   */
  function KeyboardTransport(options) {
    options = options || {};
    this.inputMode = 'keyboard';
    this.alphabet = options.alphabet || null;
    this.session = null;
    this.handlers = Object.create(null);
    this.eventLog = [];
    this.lastLatencyMs = 0;
    this._boundKeyDown = this._onKeyDown.bind(this);
    this._boundBlur = this._onBlur.bind(this);
    this._boundFocus = this._onFocus.bind(this);
    this._boundVisibility = this._onVisibility.bind(this);
    this._listening = false;
  }

  /** Whether this transport can be used at all. The keyboard always can. */
  KeyboardTransport.prototype.isAvailable = function () { return true; };

  KeyboardTransport.prototype.on = function (type, handler) {
    (this.handlers[type] || (this.handlers[type] = [])).push(handler);
  };

  KeyboardTransport.prototype._fire = function (type, payload) {
    var list = this.handlers[type];
    if (!list) { return; }
    for (var i = 0; i < list.length; i++) { list[i](payload); }
  };

  // -- lifecycle -------------------------------------------------------------

  /**
   * Begin a run.
   * @param {string} mode 'EVAL' (scored, 60 s) or 'PRACTICE' (untimed, unscored)
   * @param {number=} seed optional fixed seed, for reproducing a specific session
   */
  KeyboardTransport.prototype.start = function (mode, seed) {
    var self = this;
    this.eventLog = [];
    this.lastLatencyMs = 0;

    this.session = new GP.session.Session({
      seed: (seed === undefined) ? GP.rng.randomSeed() : seed,
      alphabet: this.alphabet,
      durationMs: (mode === 'PRACTICE') ? 0 : GP.session.K_EVAL_DURATION_MS,
      countdownMs: GP.session.K_COUNTDOWN_MS,
      mode: mode,
      onEvent: function (event) {
        self.eventLog.push(event);
        if (event.type === 'HIT') { self.lastLatencyMs = event.reactionMs; }
        self._fire(event.type, event);
      }
    });

    this._listen();
    this.session.start(performance.now());
    return this.session;
  };

  KeyboardTransport.prototype.abort = function () {
    if (this.session && this.session.state !== GP.session.STATE.ENDED) {
      this.session.end(performance.now(), 'ABORT');
    }
    this._unlisten();
  };

  /** Drive time-based transitions. Called from the render loop. */
  KeyboardTransport.prototype.tick = function (nowMs) {
    if (!this.session) { return null; }
    var state = this.session.tick(nowMs);
    if (state === GP.session.STATE.ENDED) { this._unlisten(); }
    return state;
  };

  KeyboardTransport.prototype.dispose = function () {
    this._unlisten();
    this.session = null;
  };

  // -- listeners --------------------------------------------------------------

  KeyboardTransport.prototype._listen = function () {
    if (this._listening) { return; }
    this._listening = true;
    // Capture phase, so the game sees the key before any other handler can consume it.
    globalThis.addEventListener('keydown', this._boundKeyDown, true);
    globalThis.addEventListener('blur', this._boundBlur);
    globalThis.addEventListener('focus', this._boundFocus);
    document.addEventListener('visibilitychange', this._boundVisibility);
  };

  KeyboardTransport.prototype._unlisten = function () {
    if (!this._listening) { return; }
    this._listening = false;
    globalThis.removeEventListener('keydown', this._boundKeyDown, true);
    globalThis.removeEventListener('blur', this._boundBlur);
    globalThis.removeEventListener('focus', this._boundFocus);
    document.removeEventListener('visibilitychange', this._boundVisibility);
  };

  KeyboardTransport.prototype._onKeyDown = function (event) {
    // FIRST LINE. Everything below this costs time that must not be charged to the
    // player's reaction.
    var pressMs = performance.now();

    // Auto-repeat from a held key is not a selection. Discarded before anything else
    // so a leaned-on key cannot farm hits or misses.
    if (event.repeat) { return; }

    // Modified keys are shortcuts, not gameplay: Cmd+Q must still quit.
    if (event.ctrlKey || event.metaKey || event.altKey) { return; }

    var session = this.session;
    if (!session) { return; }
    var live = (session.state === GP.session.STATE.RUNNING ||
                session.state === GP.session.STATE.COUNTDOWN);
    if (!live) { return; }

    var cell = GP.alphabet.cellForCode(event.code);
    if (cell < 0) {
      // Not one of the 25. Ignored entirely: not a hit, not a miss, no streak change.
      // Deliberately NOT preventDefault'd, so Escape, Tab and browser shortcuts keep
      // working. Recorded on the session purely so the end-of-run report can state
      // honestly how many such presses happened.
      session.noteIgnoredKey();
      return;
    }

    // A game key during a live run belongs to the game, not to the browser.
    event.preventDefault();

    var result = session.press(cell, pressMs);
    this._fire('PRESS', { cell: cell, result: result, tMs: pressMs });
  };

  KeyboardTransport.prototype._onBlur = function () {
    if (this.session && this.session.state === GP.session.STATE.RUNNING) {
      this.session.pause(performance.now());
      this._fire('PAUSED', {});
    }
  };

  KeyboardTransport.prototype._onFocus = function () {
    if (this.session && this.session.isPaused) {
      this.session.resume(performance.now());
      this._fire('RESUMED', {});
    }
  };

  KeyboardTransport.prototype._onVisibility = function () {
    // A backgrounded tab is throttled to roughly one frame per second, which would
    // make the run unplayable and the timing meaningless. Treated exactly like a
    // focus loss.
    if (document.hidden) { this._onBlur(); } else { this._onFocus(); }
  };

  // -- readouts ---------------------------------------------------------------

  /** A snapshot for the render loop. Allocates one small object per frame. */
  KeyboardTransport.prototype.snapshot = function (nowMs) {
    var s = this.session;
    if (!s) { return null; }
    return {
      state: s.state,
      n: s.n,
      sc: s.correct,
      si: s.incorrect,
      streak: s.streak,
      targetCell: s.targetCell,
      targetIsRepeat: s.targetIsRepeat,
      elapsedMs: s.elapsedMs(nowMs),
      remainingMs: s.remainingMs(nowMs),
      durationMs: s.durationMs,
      countdownRemainingMs: s.countdownRemainingMs(nowMs),
      bps: s.bitRate(nowMs),
      lastLatencyMs: this.lastLatencyMs,
      isPaused: s.isPaused
    };
  };

  KeyboardTransport.prototype.report = function (nowMs) {
    if (!this.session) { return null; }
    var report = this.session.report(
      (nowMs === undefined) ? performance.now() : nowMs
    );
    report.inputMode = 'keyboard';
    return report;
  };

  /** The full event stream as JSON Lines, for the download-log button. */
  KeyboardTransport.prototype.logAsJsonl = function (report) {
    var lines = [];
    lines.push(JSON.stringify({
      type: 'SESSION',
      specVersion: '1.0.0',
      inputMode: 'keyboard',
      mode: report ? report.mode : null,
      seed: report ? report.seed : null,
      n: report ? report.n : null,
      userAgent: (typeof navigator !== 'undefined') ? navigator.userAgent : 'unknown',
      recordedAt: new Date().toISOString()
    }));
    for (var i = 0; i < this.eventLog.length; i++) {
      lines.push(JSON.stringify(this.eventLog[i]));
    }
    if (report) { lines.push(JSON.stringify({ type: 'REPORT', report: report })); }
    return lines.join('\n') + '\n';
  };

  GP.KeyboardTransport = KeyboardTransport;
});
