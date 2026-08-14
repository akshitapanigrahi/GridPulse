/**
 * GRID PULSE - live numeric readouts.
 *
 * Driven from the requestAnimationFrame loop, but every write is guarded by a
 * comparison against the last value written. Assigning to textContent is cheap only
 * when the string actually changed; doing it unconditionally at 60 Hz for eight
 * readouts is enough DOM churn to show up in a profile. Caching the strings keeps
 * the render loop essentially free when nothing is moving.
 *
 * There is exactly one bit-rate readout: the cumulative rate over all elapsed session
 * time, which is the figure the assignment defines. A second, shorter-window rate was
 * tried and removed - two bit-rate numbers side by side is an invitation to record the
 * wrong one, and the sparkline already shows pace over the run.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var URGENT_REMAINING_MS = 10000;

  function Hud(elements) {
    this.el = elements;
    this._last = Object.create(null);
  }

  /** Write only when the rendered string actually differs. */
  Hud.prototype._set = function (key, text) {
    if (this._last[key] === text) { return; }
    this._last[key] = text;
    this.el[key].textContent = text;
  };

  /**
   * @param {Object} state
   * @param {number} state.bps cumulative bits/sec over all elapsed session time
   * @param {number} state.n alphabet size
   * @param {number} state.sc correct selections
   * @param {number} state.si incorrect selections
   * @param {number} state.remainingMs time left, or Infinity for untimed practice
   * @param {number} state.streak current streak
   * @param {number} state.lastLatencyMs reaction time of the most recent hit
   */
  Hud.prototype.render = function (state) {
    this._set('bps', state.bps.toFixed(2));
    this._set('n', String(state.n));
    this._set('sc', String(state.sc));
    this._set('si', String(state.si));
    this._set('streak', String(state.streak));

    this._set('time', (state.remainingMs === Infinity)
      ? '∞'
      : Math.ceil(Math.max(0, state.remainingMs) / 1000) + ' s');

    this._set('latency', (state.lastLatencyMs > 0)
      ? Math.round(state.lastLatencyMs) + ' ms'
      : '— ms');
  };

  /**
   * Time bar. Uses scaleX rather than width so it stays on the compositor and never
   * triggers layout, which matters because it updates every single frame.
   */
  Hud.prototype.setTimeBar = function (remainingMs, durationMs) {
    var fraction = (durationMs > 0)
      ? Math.max(0, Math.min(1, remainingMs / durationMs))
      : 1;
    this.el.timebar.style.transform = 'scaleX(' + fraction.toFixed(4) + ')';
    var urgent = (durationMs > 0 && remainingMs <= URGENT_REMAINING_MS) ? '1' : '0';
    if (this._last.urgent !== urgent) {
      this._last.urgent = urgent;
      this.el.timebar.dataset.urgent = urgent;
    }
  };

  /**
   * Blank the readouts for a new run.
   *
   * Clearing the memo cache is not enough, and used not to be: the cache only decides
   * whether the NEXT render writes, and render() is not called during the countdown -
   * the frame loop returns as soon as it has drawn the 3-2-1. So the previous run's
   * bit rate, counters and streak sat at the top of the screen for the whole three
   * seconds of the next one, which reads as the new run having inherited them.
   *
   * @param {number} [durationMs] scored length of the run about to start; 0 or
   *   undefined for untimed practice, which shows the infinity the run will keep.
   */
  Hud.prototype.reset = function (durationMs) {
    this._last = Object.create(null);
    this.el.timebar.style.transform = 'scaleX(1)';
    this.el.timebar.dataset.urgent = '0';

    this._set('bps', '0.00');
    this._set('sc', '0');
    this._set('si', '0');
    this._set('streak', '0');
    this._set('latency', '— ms');
    this._set('time', (durationMs > 0)
      ? Math.ceil(durationMs / 1000) + ' s'
      : '∞');
    // N is deliberately left alone. It is a property of the board, not of the run, and
    // blanking it to zero would state something false about the alphabet.
  };

  GP.Hud = Hud;
});
