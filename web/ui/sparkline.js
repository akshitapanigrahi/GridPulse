/**
 * GRID PULSE - canvas sparkline of bit rate over the run.
 *
 * Plots the cumulative bit rate over the run - the number that actually counts,
 * converging toward its final value. Where a player found or lost their rhythm shows
 * up as the slope of that curve.
 *
 * Samples are stored in preallocated Float32Arrays with a fixed capacity. At 10 Hz
 * over a 60-second run that is 600 samples; the buffer holds far more than a
 * practice session will ever produce, and when it fills it simply stops growing
 * rather than reallocating mid-run.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var SAMPLE_INTERVAL_MS = 100;
  var CAPACITY = 4096;          // ~6.8 minutes at 10 Hz
  var PADDING = 6;

  function Sparkline(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.timeS = new Float32Array(CAPACITY);
    this.cumulative = new Float32Array(CAPACITY);
    this.count = 0;
    this.lastSampleMs = -Infinity;
    this.maxValue = 1;
    this._dpr = 0;
    this._cssWidth = 0;
    this._cssHeight = 0;
  }

  /** Match the backing store to the CSS size and device pixel ratio. */
  Sparkline.prototype._resize = function () {
    var dpr = (globalThis.devicePixelRatio || 1);
    var rect = this.canvas.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) { return false; }
    if (dpr === this._dpr && rect.width === this._cssWidth && rect.height === this._cssHeight) {
      return true;
    }
    this._dpr = dpr;
    this._cssWidth = rect.width;
    this._cssHeight = rect.height;
    this.canvas.width = Math.round(rect.width * dpr);
    this.canvas.height = Math.round(rect.height * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return true;
  };

  Sparkline.prototype.reset = function () {
    this.count = 0;
    this.lastSampleMs = -Infinity;
    this.maxValue = 1;
    if (this._cssWidth > 0) {
      this.ctx.clearRect(0, 0, this._cssWidth, this._cssHeight);
    }
  };

  /**
   * Record a sample if enough time has passed. Cheap to call every frame.
   * @returns {boolean} true if a sample was actually taken
   */
  Sparkline.prototype.sample = function (nowMs, elapsedS, cumulative) {
    if (nowMs - this.lastSampleMs < SAMPLE_INTERVAL_MS) { return false; }
    this.lastSampleMs = nowMs;
    if (this.count >= CAPACITY) { return false; }
    var i = this.count++;
    this.timeS[i] = elapsedS;
    this.cumulative[i] = cumulative;
    if (cumulative > this.maxValue) { this.maxValue = cumulative; }
    return true;
  };

  Sparkline.prototype._plot = function (series, colour, width, w, h, spanS, scale) {
    var ctx = this.ctx;
    ctx.beginPath();
    ctx.lineWidth = width;
    ctx.strokeStyle = colour;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    for (var i = 0; i < this.count; i++) {
      var x = PADDING + (this.timeS[i] / spanS) * (w - PADDING * 2);
      var y = h - PADDING - (series[i] * scale);
      if (i === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
  };

  /**
   * @param {number} durationS the full run length, so the x axis does not rescale as
   *   the run progresses - a moving axis makes the trace impossible to read.
   */
  Sparkline.prototype.draw = function (durationS) {
    if (!this._resize()) { return; }
    var w = this._cssWidth;
    var h = this._cssHeight;
    var ctx = this.ctx;

    ctx.clearRect(0, 0, w, h);
    if (this.count < 2) { return; }

    var spanS = Math.max(durationS, this.timeS[this.count - 1], 1);
    var headroom = this.maxValue * 1.15;
    var scale = (h - PADDING * 2) / headroom;

    // Horizontal guide at the current cumulative value: the line the player is trying
    // to push upward.
    var current = this.cumulative[this.count - 1];
    var guideY = h - PADDING - current * scale;
    ctx.beginPath();
    ctx.setLineDash([3, 4]);
    ctx.lineWidth = 1;
    ctx.strokeStyle = 'rgba(93, 104, 140, 0.55)';
    ctx.moveTo(0, guideY);
    ctx.lineTo(w, guideY);
    ctx.stroke();
    ctx.setLineDash([]);

    this._plot(this.cumulative, '#23e3ff', 2.25, w, h, spanS, scale);
  };

  GP.Sparkline = Sparkline;
  GP.Sparkline.SAMPLE_INTERVAL_MS = SAMPLE_INTERVAL_MS;
});
