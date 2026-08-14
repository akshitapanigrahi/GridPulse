/**
 * GRID PULSE - audio feedback.
 *
 * WHY SYNTHESISED AND NOT SAMPLES
 * -------------------------------
 * Every asset in this project loads from disk, and web/play.html has to work when it
 * is double-clicked from a file:// origin - no server, no build step, no network. An
 * audio file would either be another asset to fetch (blocked on an opaque origin) or a
 * base64 blob measured in hundreds of kilobytes inside the page. Two oscillators and a
 * gain envelope cost nothing, load nothing, and are tunable in the source rather than
 * in an editor.
 *
 * WHY IT CANNOT AFFECT A SCORE
 * ----------------------------
 * The sound is triggered by an event that has ALREADY been scored: on the keypad the
 * press was timestamped and classified on the RP2040 before the host heard about it,
 * and in Mode B it was scored inside the keydown handler. Audio is fired afterwards
 * and nothing waits on it.
 *
 * It also cannot help a player find the target, which is the property that matters for
 * the measurement. Both sounds are strictly post-hoc: they report what a press WAS,
 * and neither is emitted before or during a target presentation, so no information
 * about where to press next is carried.
 *
 * DISTINGUISHABILITY
 * ------------------
 * Hit and miss differ in pitch, in direction and in timbre - a bright rising sine
 * against a low falling triangle - so they are told apart by more than "one is higher".
 * The same principle as the visual feedback, which never leans on colour alone.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var STORAGE_KEY = 'gridpulse.sound';

  // Loud enough to hear over a mechanical keypad being hammered, quiet enough to still
  // be feedback rather than an alarm - this fires several times a second for a minute.
  // Peaks are gain, so 1.0 would clip; these sit well under it and are mixed relative
  // to each other, a hit being the one worth noticing most.
  var HIT_PEAK = 0.26;
  var MISS_PEAK = 0.22;
  var COUNTDOWN_PEAK = 0.18;
  var GO_PEAK = 0.26;

  function Sound() {
    this.enabled = readPreference();
    this.ctx = null;
  }

  function readPreference() {
    // localStorage throws outright on some file:// configurations rather than merely
    // being empty, so absence of a preference must not take the page down with it.
    try {
      var stored = globalThis.localStorage &&
                   globalThis.localStorage.getItem(STORAGE_KEY);
      return stored === null || stored === undefined ? true : stored === 'on';
    } catch (err) {
      return true;
    }
  }

  Sound.prototype._savePreference = function () {
    try {
      if (globalThis.localStorage) {
        globalThis.localStorage.setItem(STORAGE_KEY, this.enabled ? 'on' : 'off');
      }
    } catch (err) { /* a browser that refuses to remember is not a failure */ }
  };

  /**
   * The audio context, created on first use.
   *
   * Lazily, because browsers refuse to start one outside a user gesture and a context
   * created at page load would be born suspended. Returns null where Web Audio is
   * absent - the test harness has no audio at all - and every method below tolerates
   * that, so sound is never a reason for the game not to run.
   */
  Sound.prototype._context = function () {
    if (this.ctx) { return this.ctx; }
    var Ctor = globalThis.AudioContext || globalThis.webkitAudioContext;
    if (!Ctor) { return null; }
    try {
      this.ctx = new Ctor();
    } catch (err) {
      this.ctx = null;
    }
    return this.ctx;
  };

  /** Call from a click. Browsers will not let audio start any other way. */
  Sound.prototype.unlock = function () {
    var ctx = this._context();
    if (ctx && ctx.state === 'suspended' && ctx.resume) { ctx.resume(); }
  };

  Sound.prototype.setEnabled = function (on) {
    this.enabled = !!on;
    this._savePreference();
    if (this.enabled) { this.unlock(); }
    return this.enabled;
  };

  Sound.prototype.toggle = function () {
    return this.setEnabled(!this.enabled);
  };

  /**
   * One shaped tone.
   *
   * @param {number} from starting frequency in Hz
   * @param {number} to frequency to glide to, which is what gives each sound a
   *   direction rather than just a pitch
   * @param {number} seconds total length
   * @param {number} peak gain at the attack
   * @param {string} shape oscillator type
   */
  Sound.prototype._tone = function (from, to, seconds, peak, shape) {
    var ctx = this._context();
    if (!ctx || !ctx.createOscillator) { return; }

    var now = ctx.currentTime;
    var osc = ctx.createOscillator();
    var gain = ctx.createGain();

    osc.type = shape;
    osc.frequency.setValueAtTime(from, now);
    osc.frequency.exponentialRampToValueAtTime(to, now + seconds);

    // A near-instant attack and an exponential decay: a click has to sound like an
    // event, not like a note being held.
    gain.gain.setValueAtTime(0.0001, now);
    gain.gain.exponentialRampToValueAtTime(peak, now + 0.006);
    gain.gain.exponentialRampToValueAtTime(0.0001, now + seconds);

    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(now);
    osc.stop(now + seconds + 0.02);
  };

  /**
   * One beat of the countdown. Flat, unhurried, and the same every time.
   *
   * Flat on purpose: the three beats say "not yet", and a pitch that climbed would
   * imply progress toward something the player should act on. Everything that means
   * ACT here glides - a hit rises, a miss falls - so a level tone is unmistakably
   * neither.
   */
  Sound.prototype.countdown = function () {
    if (!this.enabled) { return; }
    this._tone(520, 520, 0.10, COUNTDOWN_PEAK, 'sine');
  };

  /** The run starting. Longer and fuller than a beat, so "go" is not another "wait". */
  Sound.prototype.go = function () {
    if (!this.enabled) { return; }
    this._tone(660, 990, 0.22, GO_PEAK, 'sine');
  };

  /** A hit: bright, short, rising. */
  Sound.prototype.hit = function () {
    if (!this.enabled) { return; }
    this._tone(880, 1320, 0.09, HIT_PEAK, 'sine');
  };

  /** A miss: low, duller, falling. Opposite on every axis the ear notices. */
  Sound.prototype.miss = function () {
    if (!this.enabled) { return; }
    this._tone(200, 120, 0.16, MISS_PEAK, 'triangle');
  };

  GP.Sound = Sound;
});
