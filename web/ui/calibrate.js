/**
 * GRID PULSE - the calibration (self-test) view.
 *
 * WHAT THIS IS FOR
 * ----------------
 * The device already walks all 25 cells and reports the outcome of each one, but
 * until now nothing rendered those events (sse.js had `case 'SELFTEST': break;`), so
 * running the walk looked from the browser like nothing happening at all. This view
 * is the missing half: it turns each SELFTEST event into a visible verdict on a cell,
 * next to the GPIO and pixel index that cell is wired to.
 *
 * WHY GPIO AND PIXEL ARE ON SCREEN
 * --------------------------------
 * The walk's real job is proving the two mapping tables are right. Pressing the key
 * under the lit LED can only succeed if board_map.h's cell->GPIO table and its
 * cell->pixel table both agree with the physical board. Printing both numbers on the
 * cell means a mis-wire is not merely detectable, it is legible: the operator can see
 * *which* pin the firmware thinks it just tested.
 *
 * ON THE "NEXT" HIGHLIGHT
 * -----------------------
 * The firmware emits an outcome only once a cell resolves; it never announces which
 * cell it is currently lighting. So the pale outline this view draws on the next cell
 * is a prediction from the walk order, not a report of what the LED is doing. It is
 * styled as a hint and labelled as expected, deliberately, because presenting a guess
 * as ground truth is exactly the sort of thing this whole project avoids elsewhere.
 * The verdicts themselves are always device-reported.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var CELL_COUNT = 25;

  /** Nothing heard from the device for this long mid-walk means something is wrong. */
  var STALL_TIMEOUT_MS = 15000;

  var RESULT_CLASS = {
    OK: 'ok',
    NO_KEY: 'nokey',
    STUCK: 'stuck'
  };

  var RESULT_TEXT = {
    OK: 'ok',
    NO_KEY: 'no key',
    STUCK: 'stuck'
  };

  function CalibrateView(el) {
    this.el = el;
    this.cells = [];
    this.state = 'idle';   // idle | running | done | stalled
    this.onStart = null;
    this.onCancel = null;
    this.onDone = null;

    this._build();
    this._wire();
    this.reset();
  }

  // -- construction ---------------------------------------------------------------

  CalibrateView.prototype._build = function () {
    var map = GP.boardmap;
    this.el.grid.textContent = '';
    this.cells = [];

    for (var cell = 0; cell < CELL_COUNT; cell++) {
      var node = document.createElement('div');
      node.className = 'cal-cell';
      node.dataset.cell = String(cell);
      node.dataset.status = 'pending';

      var idx = document.createElement('span');
      idx.className = 'cal-cell__index';
      idx.textContent = String(cell);

      var pins = document.createElement('span');
      pins.className = 'cal-cell__pins';
      pins.textContent = 'GP' + map.CELL_TO_GPIO[cell] + ' · px' + map.CELL_TO_PIXEL[cell];

      var verdict = document.createElement('span');
      verdict.className = 'cal-cell__verdict';
      verdict.textContent = '—';

      node.appendChild(idx);
      node.appendChild(pins);
      node.appendChild(verdict);
      this.el.grid.appendChild(node);

      this.cells.push({ node: node, verdict: verdict, status: 'pending', pass: 0 });
    }
  };

  CalibrateView.prototype._wire = function () {
    var self = this;
    this.el.btnStart.addEventListener('click', function () {
      if (self.onStart) { self.onStart(); }
    });
    this.el.btnCancel.addEventListener('click', function () {
      if (self.onCancel) { self.onCancel(); }
    });
    this.el.btnDone.addEventListener('click', function () {
      if (self.onDone) { self.onDone(); }
    });
  };

  // -- lifecycle -------------------------------------------------------------------

  CalibrateView.prototype.reset = function () {
    for (var i = 0; i < this.cells.length; i++) {
      var c = this.cells[i];
      c.status = 'pending';
      c.pass = 0;
      c.node.dataset.status = 'pending';
      c.node.classList.remove('cal-cell--next');
      c.verdict.textContent = '—';
    }
    this.state = 'idle';
    this.lastCell = -1;
    this.lastEventAt = 0;
    this.notes = [];
    // The previous walk's verdict has to go with it. Leaving it on screen means a
    // re-run is watched with the last run's conclusion sitting underneath it - and
    // "All 25 cells passed" is exactly the sort of thing that gets read as the current
    // result when it is stale.
    this.el.summary.innerHTML = '';
    this.el.summary.hidden = true;
    this._setPhase('idle');
    this._renderProgress();
  };

  /** Called when the SELFTEST command has been accepted by the device. */
  CalibrateView.prototype.begin = function () {
    this.reset();
    this.state = 'running';
    this.lastEventAt = Date.now();
    this._setPhase('running');
    this._markNext();
    this._renderProgress();
  };

  /**
   * Apply one device-reported cell verdict.
   * @param {{cell:number, gpio:number, pixel:number, result:string, pass:number}} event
   */
  CalibrateView.prototype.applyOutcome = function (event) {
    if (this.state !== 'running') { return; }
    var cell = event.cell;
    if (typeof cell !== 'number' || cell < 0 || cell >= CELL_COUNT) { return; }

    var map = GP.boardmap;
    var record = this.cells[cell];
    var result = event.result || 'OK';

    // The firmware walks pass 1 in ascending grid order, so any lower-numbered cell
    // that still has not reported by the time this one does was dropped before the
    // walk began - a switch already closed at boot is excluded outright and its LED
    // is never lit. That is knowable the instant the walk steps over it, so say it
    // then, rather than leaving the cell looking like it is merely up next.
    if ((event.pass || 1) === 1) {
      for (var skipped = 0; skipped < cell; skipped++) {
        if (this.cells[skipped].status !== 'pending') { continue; }
        this.cells[skipped].status = 'skipped';
        this.cells[skipped].node.dataset.status = 'skipped';
        this.cells[skipped].node.classList.remove('cal-cell--next');
        this.cells[skipped].verdict.textContent = 'skipped';
        this._note('Cell ' + skipped + ' (GP' + map.CELL_TO_GPIO[skipped] + ') was ' +
          'excluded before the walk started: its switch read as already closed at ' +
          'boot. The firmware never lights an excluded cell, so pixel ' +
          map.CELL_TO_PIXEL[skipped] + ' staying dark here says nothing about the LED ' +
          '— only that the switch was not trusted.');
      }
    }
    record.status = RESULT_CLASS[result] || 'nokey';
    record.pass = event.pass || 1;
    record.node.dataset.status = record.status;
    record.node.classList.remove('cal-cell--next');
    record.verdict.textContent = (RESULT_TEXT[result] || result) +
      (record.pass > 1 ? ' (p' + record.pass + ')' : '');

    // Cross-check the device's own idea of the wiring against the mirrored table.
    // A disagreement means board_map.h and boardmap.js have drifted apart, which
    // would make every other number on this screen untrustworthy.
    if (typeof event.gpio === 'number' && event.gpio !== map.CELL_TO_GPIO[cell]) {
      this._note('cell ' + cell + ': device reports GP' + event.gpio +
        ' but boardmap.js says GP' + map.CELL_TO_GPIO[cell] +
        ' — the firmware and browser tables disagree.');
    }
    if (typeof event.pixel === 'number' && event.pixel !== map.CELL_TO_PIXEL[cell]) {
      this._note('cell ' + cell + ': device reports pixel ' + event.pixel +
        ' but boardmap.js says ' + map.CELL_TO_PIXEL[cell] +
        ' — the firmware and browser tables disagree.');
    }

    this.lastCell = cell;
    this.lastEventAt = Date.now();
    this._markNext();
    this._renderProgress();
  };

  /** A LOG event arriving mid-walk that the operator needs to see. */
  CalibrateView.prototype.applyLog = function (event) {
    if (this.state === 'idle') { return; }
    // Deliberately silent. The walk's verdicts are unaffected by a failed flash write;
    // the only consequence is that they are forgotten at the next power cycle. It is
    // named here rather than left to the catch-all below, which would otherwise print
    // the raw `health_mask_not_persisted` and be worse than saying nothing. The host
    // still writes the event to the session log, so an audit can still find it.
    if (event.msg === 'health_mask_not_persisted') { return; }
    if (event.msg === 'too_few_healthy_cells_to_play') {
      this._note('Fewer than three cells are healthy, so the device will refuse to ' +
        'start a run until this is fixed.');
    } else if (event.level === 'E' || event.level === 'W') {
      this._note('device: ' + event.msg);
    }
  };

  /**
   * The walk is over. The device signals this by re-announcing itself, so `n` here
   * is the alphabet size it will actually play with from now on.
   */
  CalibrateView.prototype.finish = function (n) {
    if (this.state !== 'running') { return; }
    this.state = 'done';
    this._setPhase('done');

    for (var i = 0; i < this.cells.length; i++) {
      this.cells[i].node.classList.remove('cal-cell--next');
      // A cell the walk never reached was excluded before it started - the firmware
      // drops switches that were already closed at boot. Say so rather than leaving
      // it looking merely untested.
      if (this.cells[i].status === 'pending') {
        this.cells[i].status = 'skipped';
        this.cells[i].node.dataset.status = 'skipped';
        this.cells[i].verdict.textContent = 'skipped';
      }
    }

    var bad = this._failedCells();
    if (typeof n === 'number' && n < CELL_COUNT) {
      this._note('N is now ' + n + ', not 25. log2(N−1) becomes ' +
        (Math.log(n - 1) / Math.LN2).toFixed(3) + ' bits per selection, so scores ' +
        'from this board are not comparable with a full 25-cell board.');
    }
    this._renderProgress();
    this._renderSummary(n, bad);
  };

  CalibrateView.prototype.stall = function () {
    if (this.state !== 'running') { return; }
    this.state = 'stalled';
    this._setPhase('stalled');
    this._note('No response from the device for ' + (STALL_TIMEOUT_MS / 1000) +
      ' seconds. The walk may have been interrupted; close this and try again.');
    this._renderSummary(null, this._failedCells());
  };

  /** Drives the stall detector. Safe to call every frame. */
  CalibrateView.prototype.tick = function () {
    if (this.state !== 'running' || !this.lastEventAt) { return; }
    if (Date.now() - this.lastEventAt > STALL_TIMEOUT_MS) { this.stall(); }
  };

  // -- internals ---------------------------------------------------------------------

  CalibrateView.prototype._failedCells = function () {
    var bad = [];
    for (var i = 0; i < this.cells.length; i++) {
      if (this.cells[i].status !== 'ok' && this.cells[i].status !== 'pending') {
        bad.push(i);
      }
    }
    return bad;
  };

  /**
   * Record something the operator needs to know.
   *
   * Shown as soon as it is raised rather than saved for the end-of-walk summary: the
   * things that get raised here - an excluded cell, a failed flash write - are
   * exactly the ones that look like a malfunction while the walk is still going, and
   * an explanation that arrives two minutes later has already failed at its job.
   */
  CalibrateView.prototype._note = function (text) {
    if (this.notes.indexOf(text) !== -1) { return; }
    this.notes.push(text);
    if (this.state === 'running') {
      this.el.summary.innerHTML =
        '<p><span class="cal-note">' + this.notes.join('</span></p><p><span class="cal-note">') +
        '</span></p>';
      this.el.summary.hidden = false;
    }
  };

  CalibrateView.prototype._setPhase = function (phase) {
    this.el.root.dataset.phase = phase;
    // The same button reappears once a walk has finished, and "START CALIBRATION"
    // then reads as though the last one had not happened. Naming it as a repeat also
    // makes the recovery path obvious: a cell marked dead by a slow finger is undone
    // by running the walk again, since a forced walk retests cells previously
    // condemned.
    this.el.btnStart.textContent = (phase === 'done' || phase === 'stalled')
      ? 'RE-RUN CALIBRATION'
      : 'START CALIBRATION';
  };

  /**
   * Outline the cell the walk is most likely sitting on. See the file header: this
   * is inferred from the walk order, not reported by the device.
   */
  CalibrateView.prototype._markNext = function () {
    for (var i = 0; i < this.cells.length; i++) {
      this.cells[i].node.classList.remove('cal-cell--next');
    }
    if (this.state !== 'running') { return; }
    // Ascending from the last cell the device reported, so the hint tracks the walk
    // instead of parking on a cell the walk has already stepped over.
    for (var c = this.lastCell + 1; c < this.cells.length; c++) {
      if (this.cells[c].status === 'pending') {
        this.cells[c].node.classList.add('cal-cell--next');
        return;
      }
    }
    // Nothing left above it, so pass 2 is about to retry the failures from the top.
    for (var again = 0; again < this.cells.length; again++) {
      if (this.cells[again].status === 'nokey') {
        this.cells[again].node.classList.add('cal-cell--next');
        return;
      }
    }
  };

  CalibrateView.prototype._renderProgress = function () {
    var checked = 0;
    for (var i = 0; i < this.cells.length; i++) {
      if (this.cells[i].status !== 'pending') { checked++; }
    }
    // The bar carries progress on its own, and every cell already shows its own
    // verdict in the grid. A running tally repeated the same information in words.
    this.el.bar.style.width = ((checked / CELL_COUNT) * 100).toFixed(1) + '%';
  };

  CalibrateView.prototype._renderSummary = function (n, bad) {
    var parts = [];
    if (bad.length === 0) {
      parts.push('<b class="cal-ok">All 25 cells passed.</b>');
    } else {
      parts.push('<b class="cal-bad">' + bad.length + ' cell(s) did not pass:</b> ' +
        this._describeBad(bad) + '.');
    }
    for (var i = 0; i < this.notes.length; i++) {
      parts.push('<span class="cal-note">' + this.notes[i] + '</span>');
    }
    this.el.summary.innerHTML = '<p>' + parts.join('</p><p>') + '</p>';
    this.el.summary.hidden = false;
  };

  CalibrateView.prototype._describeBad = function (bad) {
    var map = GP.boardmap;
    var out = [];
    for (var i = 0; i < bad.length; i++) {
      var cell = bad[i];
      out.push('cell ' + cell + ' (row ' + Math.floor(cell / 5) + ', col ' + (cell % 5) +
        ', GP' + map.CELL_TO_GPIO[cell] + ', ' + this.cells[cell].verdict.textContent + ')');
    }
    return out.join('; ');
  };

  GP.CalibrateView = CalibrateView;
});
