/**
 * GRID PULSE - hit and miss feedback effects.
 *
 * POOLED, NOT ALLOCATED. Each of the 25 cells gets one ring, one bloom and one X
 * element created at startup and reused forever. Replaying an effect is a matter of
 * toggling a data attribute, so a 5-presses-per-second run allocates nothing and
 * produces no garbage-collector pauses in the middle of a scored minute.
 *
 * Feedback is triple-encoded so it never depends on colour alone:
 *
 *   hit   cyan   expanding ring + radial bloom, grid pulses outward
 *   miss  amber  X mark, cell and grid shake laterally
 *
 * See css/fx.css for the accessibility contract this implements.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var HIT_FLASH_MS = 70;
  var MISS_FLASH_MS = 110;
  var MISS_CELL_FX_MS = 280;

  /**
   * @constructor
   * @param {GP.GridView} gridView
   * @param {HTMLElement} body document.body, for page-level flash state
   */
  function Fx(gridView, body) {
    this.grid = gridView;
    this.body = body;
    this.gridEl = gridView.container;
    this.rings = new Array(GP.alphabet.K_CELL_COUNT);
    this.blooms = new Array(GP.alphabet.K_CELL_COUNT);
    this.crosses = new Array(GP.alphabet.K_CELL_COUNT);
    this._flashTimer = 0;
    this._gridFxTimer = 0;
    this._cellFxTimers = new Array(GP.alphabet.K_CELL_COUNT).fill(0);
    this._build();
  }

  Fx.prototype._build = function () {
    for (var cell = 0; cell < GP.alphabet.K_CELL_COUNT; cell++) {
      var node = this.grid.cellNode(cell);

      var bloom = document.createElement('div');
      bloom.className = 'fx-bloom';
      node.appendChild(bloom);
      this.blooms[cell] = bloom;

      var ring = document.createElement('div');
      ring.className = 'fx-ring';
      node.appendChild(ring);
      this.rings[cell] = ring;

      var cross = document.createElement('div');
      cross.className = 'fx-x';
      node.appendChild(cross);
      this.crosses[cell] = cross;
    }
  };

  /**
   * Restart a CSS animation on a pooled element.
   *
   * Clearing the attribute alone is not enough - the browser coalesces the removal
   * and re-add into no change at all. Reading offsetWidth forces the style to be
   * recalculated in between, which is the standard way to retrigger an animation.
   */
  function replay(el) {
    el.removeAttribute('data-run');
    void el.offsetWidth;
    el.dataset.run = '1';
  }

  Fx.prototype._flash = function (kind, durationMs) {
    this.body.dataset.flash = kind;
    if (this._flashTimer) { clearTimeout(this._flashTimer); }
    var body = this.body;
    this._flashTimer = setTimeout(function () {
      delete body.dataset.flash;
    }, durationMs);
  };

  Fx.prototype._gridFx = function (kind, durationMs) {
    var el = this.gridEl;
    el.removeAttribute('data-fx');
    void el.offsetWidth;
    el.dataset.fx = kind;
    if (this._gridFxTimer) { clearTimeout(this._gridFxTimer); }
    this._gridFxTimer = setTimeout(function () {
      el.removeAttribute('data-fx');
    }, durationMs);
  };

  /** Play hit feedback on a cell. Called from the input path, before the next frame. */
  Fx.prototype.hit = function (cell) {
    replay(this.blooms[cell]);
    replay(this.rings[cell]);
    this._flash('hit', HIT_FLASH_MS);
    this._gridFx('hit', 180);
  };

  /** Play miss feedback on the cell that was wrongly pressed. */
  Fx.prototype.miss = function (cell) {
    if (cell >= 0 && cell < this.crosses.length) {
      replay(this.crosses[cell]);
      var node = this.grid.cellNode(cell);
      // Do not stomp the target styling if the player somehow mis-presses the target
      // cell; data-fx is a separate attribute from data-state for exactly that reason.
      node.dataset.fx = 'miss';
      if (this._cellFxTimers[cell]) { clearTimeout(this._cellFxTimers[cell]); }
      this._cellFxTimers[cell] = setTimeout(function () {
        node.removeAttribute('data-fx');
      }, MISS_CELL_FX_MS);
    }
    this._flash('miss', MISS_FLASH_MS);
    this._gridFx('miss', 220);
  };


  /** Cancel every in-flight effect and timer. Used on abort and on run end. */
  Fx.prototype.reset = function () {
    if (this._flashTimer) { clearTimeout(this._flashTimer); this._flashTimer = 0; }
    if (this._gridFxTimer) { clearTimeout(this._gridFxTimer); this._gridFxTimer = 0; }
    for (var i = 0; i < this._cellFxTimers.length; i++) {
      if (this._cellFxTimers[i]) { clearTimeout(this._cellFxTimers[i]); this._cellFxTimers[i] = 0; }
      this.grid.cellNode(i).removeAttribute('data-fx');
      this.rings[i].removeAttribute('data-run');
      this.blooms[i].removeAttribute('data-run');
      this.crosses[i].removeAttribute('data-run');
    }
    this.gridEl.removeAttribute('data-fx');
    delete this.body.dataset.flash;
  };

  GP.Fx = Fx;
});
