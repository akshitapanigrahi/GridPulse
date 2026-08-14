/**
 * GRID PULSE - the 5x5 grid view.
 *
 * Builds the 25 cells once and thereafter only flips data attributes on them. There
 * is no re-render, no innerHTML churn, and no allocation while the game is running:
 * lighting a target is two attribute writes, which the browser resolves to a
 * compositor-level style change.
 *
 * The view is deliberately dumb. It knows how to show a target and how to show
 * feedback; it does not know the rules, the clock, or the score.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  /**
   * @constructor
   * @param {HTMLElement} container the .grid element
   */
  function GridView(container) {
    this.container = container;
    this.cells = new Array(GP.alphabet.K_CELL_COUNT);
    this.labelNodes = new Array(GP.alphabet.K_CELL_COUNT);
    this.currentTarget = -1;
    this._build();
  }

  GridView.prototype._build = function () {
    var frag = document.createDocumentFragment();
    for (var cell = 0; cell < GP.alphabet.K_CELL_COUNT; cell++) {
      var node = document.createElement('div');
      node.className = 'cell';
      node.dataset.cell = String(cell);
      node.dataset.state = 'idle';

      var label = document.createElement('span');
      label.className = 'cell__label';
      label.textContent = '';
      node.appendChild(label);

      // Diagnostics: the physical pixel and GPIO for this grid position. Hidden
      // unless diagnostics are on, but present so a mapping problem can be read off
      // the screen without opening a console.
      var pixel = document.createElement('span');
      pixel.className = 'cell__pixel';
      pixel.textContent = 'px' + GP.boardmap.CELL_TO_PIXEL[cell] +
        ' gp' + GP.boardmap.CELL_TO_GPIO[cell];
      node.appendChild(pixel);

      this.cells[cell] = node;
      this.labelNodes[cell] = label;
      frag.appendChild(node);
    }
    this.container.appendChild(frag);
  };

  /** Set the visible key labels (Mode B) or clear them (Mode A). */
  GridView.prototype.setLabels = function (labels) {
    for (var cell = 0; cell < this.labelNodes.length; cell++) {
      this.labelNodes[cell].textContent = labels ? labels[cell] : '';
    }
  };

  /**
   * Mark cells excluded by the hardware self-test.
   * @param {Array<number>|null} alphabet healthy cells, or null for all healthy
   */
  GridView.prototype.setHealthy = function (alphabet) {
    var healthy = null;
    if (alphabet) {
      healthy = Object.create(null);
      for (var i = 0; i < alphabet.length; i++) { healthy[alphabet[i]] = true; }
    }
    for (var cell = 0; cell < this.cells.length; cell++) {
      var dead = healthy ? !healthy[cell] : false;
      if (dead) {
        this.cells[cell].dataset.health = 'dead';
      } else {
        delete this.cells[cell].dataset.health;
      }
    }
  };

  /**
   * Light exactly one cell, or none when cell is -1.
   *
   * Never more than one at a time - that is the ground-truth guarantee, and it is
   * enforced here rather than trusted.
   *
   * @param {number} cell target cell index, or -1
   * @param {boolean} isRepeat true when this target equals the previous one, which
   *   gets an extra ring so "flash then relight" is visually unambiguous
   */
  GridView.prototype.setTarget = function (cell, isRepeat) {
    if (this.currentTarget >= 0 && this.currentTarget !== cell) {
      this.cells[this.currentTarget].dataset.state = 'idle';
      delete this.cells[this.currentTarget].dataset.repeat;
    }
    if (cell < 0) {
      if (this.currentTarget >= 0) {
        this.cells[this.currentTarget].dataset.state = 'idle';
        delete this.cells[this.currentTarget].dataset.repeat;
      }
      this.currentTarget = -1;
      return;
    }
    var node = this.cells[cell];
    if (isRepeat) {
      // Force the animation to restart on a repeat: without dropping the attribute
      // and reading back a layout property, the browser coalesces the change and the
      // player sees nothing happen at all.
      node.dataset.state = 'idle';
      delete node.dataset.repeat;
      void node.offsetWidth;
      node.dataset.repeat = '1';
    }
    node.dataset.state = 'target';
    this.currentTarget = cell;
  };

  GridView.prototype.clear = function () {
    this.setTarget(-1, false);
  };

  GridView.prototype.cellNode = function (cell) {
    return this.cells[cell];
  };

  GP.GridView = GridView;
});
