/**
 * GRID PULSE - Mode A transport: the hardware keypad, via the host bridge.
 *
 * In Mode A the game core runs on the RP2040, not here. Target selection, hit and
 * miss classification, and all timestamps are the device's. This module is a
 * DISPLAY: it mirrors the device's state so the screen shows what the board is
 * doing, and it never generates a target, never decides a hit, and never contributes
 * to the score.
 *
 * That division is the point. A USB round trip is roughly 5 ms; at four presses per
 * second that is about 2% of the final bit rate, and it would also drag USB
 * scheduling jitter into every measured reaction time. Keeping the loop on the
 * device removes both. The screen trails the board by about a millisecond, which is
 * sub-frame at 60 Hz.
 *
 * TRANSPORT. Server-Sent Events over the local Python bridge. SSE is one
 * directional, which is all a display needs, and it costs no dependencies - unlike
 * WebSockets, which would. Commands go the other way as ordinary POSTs.
 *
 * RECONCILIATION. The live counters below are derived from the event stream and are
 * explicitly display-only. At end of run the device sends its own authoritative
 * tally, and if the two disagree the DEVICE's numbers are what get reported and the
 * discrepancy is logged. See docs/PROTOCOL.md.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var STATUS_URL = 'api/status';
  var EVENTS_URL = 'api/events';
  var COMMAND_URL = 'api/command';

  // How long to wait for the event stream to open before sending a command anyway.
  // A stream that has not opened by now is not going to, and a button that silently
  // does nothing is worse than one whose failure shows up in the link indicator.
  var STREAM_OPEN_TIMEOUT_MS = 2000;

  function DeviceTransport() {
    this.inputMode = 'hardware';
    this.handlers = Object.create(null);
    this.source = null;
    // Resolves once the event stream is registered on the host. Commands wait for
    // it; see connect().
    this.ready = null;
    this.eventLog = [];

    this.link = { state: 'idle', text: 'not connected', seqGaps: 0, lastSeq: -1 };
    // Set from the probe when the host is playing a recording back rather than
    // talking to a device.
    this.replaying = false;
    this.device = { connected: false, port: null, reason: 'not probed yet' };

    // Display-only mirror of the device's state.
    this.mirror = {
      state: 'IDLE', mode: 'IDLE', n: GP.alphabet.K_DEFAULT_ALPHABET_SIZE,
      sc: 0, si: 0, streak: 0, targetCell: -1, targetIsRepeat: false,
      elapsedMs: 0, durationMs: GP.session.K_EVAL_DURATION_MS,
      countdownStartedMs: 0, lastLatencyMs: 0, seed: 0, alphabet: null
    };
    this.finalReport = null;
    this.reconciliation = null;
  }

  DeviceTransport.prototype.on = function (type, handler) {
    (this.handlers[type] || (this.handlers[type] = [])).push(handler);
  };

  DeviceTransport.prototype._fire = function (type, payload) {
    var list = this.handlers[type];
    if (!list) { return; }
    for (var i = 0; i < list.length; i++) { list[i](payload); }
  };

  /**
   * Is a hardware run possible right now?
   *
   * A file:// page has no bridge to talk to, and that is a completely ordinary
   * situation - the answer is a plain-language reason, never an error.
   *
   * @returns {Promise<{available: boolean, reason: string}>}
   */
  DeviceTransport.prototype.probe = function () {
    var self = this;
    if (globalThis.location && globalThis.location.protocol === 'file:') {
      this.device = {
        connected: false, port: null,
        reason: 'this page was opened directly from a file, so there is no bridge ' +
                'to the device — run ./run.sh to use the keypad'
      };
      // fileOrigin tells the caller there is no bridge to stream from at all, as
      // opposed to a bridge that currently has no keypad.
      return Promise.resolve({
        available: false, reason: this.device.reason, fileOrigin: true
      });
    }
    return fetch(STATUS_URL, { cache: 'no-store' })
      .then(function (response) {
        if (!response.ok) { throw new Error('status ' + response.status); }
        return response.json();
      })
      .then(function (status) {
        self.device = status.device || { connected: false, reason: 'no device field' };
        return {
          available: !!self.device.connected,
          reason: self.device.connected
            ? ('connected on ' + self.device.port)
            : (self.device.reason || 'no keypad detected'),
          // Set when the host was launched with --selftest. The host no longer fires
          // the walk itself; it just asks the page to open the calibration screen, so
          // the five-second-per-cell clock starts when the operator is ready.
          selftestRequested: !!status.selftest_requested,
          // A replay is not an absent keypad. The bridge never opened one, so
          // reporting "not connected" would describe a fault that does not exist.
          replaying: !!status.replaying
        };
      })
      .catch(function (err) {
        self.device = {
          connected: false, port: null,
          reason: 'the host bridge is not responding (' + err.message + ')'
        };
        return { available: false, reason: self.device.reason };
      });
  };

  DeviceTransport.prototype.isAvailable = function () {
    return !!this.device.connected;
  };

  // -- event stream -----------------------------------------------------------

  /**
   * Open the event stream.
   *
   * Returns a promise that resolves once the stream is live, and NO COMMAND MAY BE
   * SENT BEFORE IT DOES. The host registers a subscriber as it opens the stream, and
   * publishes to whoever is registered at that instant - there is no backlog and no
   * replay, so an event produced before the subscription is delivered to nobody and
   * is gone for good.
   *
   * START travels a far shorter path than stream setup: one small POST, forwarded
   * straight down the USB link, answered by the device in a millisecond or two. Stream
   * setup needs a socket, a request, a handler thread and a subscription. Firing them
   * together is a race the first run of a session reliably loses, and losing it means
   * MODE COUNTDOWN is published into an empty subscriber list: no 3-2-1 overlay, and
   * three seconds of dead-looking screen before RUNNING arrives. Later runs usually
   * win the same race on a warm host, which is what made it look like a first-run-only
   * problem rather than a race.
   *
   * @returns {Promise<void>}
   */
  DeviceTransport.prototype.connect = function () {
    if (this.ready) { return this.ready; }
    var self = this;
    this.source = new EventSource(EVENTS_URL);
    this._setLink('warn', 'connecting…');

    this.ready = new Promise(function (resolve) {
      var settled = false;
      var settle = function () {
        if (settled) { return; }
        settled = true;
        resolve();
      };
      self.source.addEventListener('open', settle);
      setTimeout(settle, STREAM_OPEN_TIMEOUT_MS);
    });

    this.source.onopen = function () {
      self._setLink('ok', 'connected');
    };
    this.source.onerror = function () {
      // EventSource reconnects on its own; surface it rather than hiding it, because
      // a dropped link during a scored run is something the grader must see.
      self._setLink('error', 'stream interrupted — reconnecting');
    };
    this.source.onmessage = function (message) {
      var event;
      try {
        event = JSON.parse(message.data);
      } catch (err) {
        self.link.seqGaps++;
        self._setLink('warn', 'malformed event dropped');
        return;
      }
      self._ingest(event);
    };

    return this.ready;
  };

  DeviceTransport.prototype.disconnect = function () {
    if (this.source) { this.source.close(); this.source = null; }
    this.ready = null;
    this._setLink('idle', 'disconnected');
  };

  DeviceTransport.prototype._setLink = function (state, text) {
    this.link.state = state;
    this.link.text = text;
    this._fire('LINK', this.link);
  };

  /**
   * Fold one device event into the display mirror.
   *
   * Nothing here decides anything about the game. Every field written below came off
   * the wire from the device.
   */
  DeviceTransport.prototype._ingest = function (event) {
    this.eventLog.push(event);

    // Sequence numbers are monotonic from device boot. A gap means bytes were lost,
    // which means the display may be wrong - say so loudly rather than mis-scoring
    // in silence. The device's END tally remains authoritative regardless.
    if (typeof event.seq === 'number') {
      if (this.link.lastSeq >= 0 && event.seq !== this.link.lastSeq + 1) {
        this.link.seqGaps += Math.max(1, event.seq - this.link.lastSeq - 1);
        this._setLink('warn', this.link.seqGaps + ' dropped event(s) — display only');
      }
      this.link.lastSeq = event.seq;
    }

    var m = this.mirror;
    switch (event.type) {
      case 'HELLO':
        this.link.lastSeq = -1;
        this._setLink('ok', 'device ' + (event.fw || '?') + ' ready');
        break;
      case 'MODE':
        m.mode = event.mode;
        m.state = event.state;
        if (typeof event.n === 'number') { m.n = event.n; }
        if (typeof event.seed === 'number') { m.seed = event.seed; }
        if (event.alphabet) { m.alphabet = event.alphabet; }
        if (event.state === 'COUNTDOWN') {
          // The browser and RP2040 clocks have unrelated origins. Receipt time is
          // the closest display-only anchor; the RUNNING event remains authoritative
          // for hiding the overlay and starting the scored clock.
          m.countdownStartedMs = performance.now();
        }
        if (event.state === 'RUNNING') {
          m.sc = 0; m.si = 0; m.streak = 0; m.elapsedMs = 0;
        }
        break;
      case 'TARGET':
        m.targetCell = event.cell;
        m.targetIsRepeat = !!event.repeat;
        break;
      case 'HIT':
        m.sc = event.sc; m.si = event.si; m.streak = event.streak;
        m.lastLatencyMs = (event.rt_us || 0) / 1000;
        break;
      case 'MISS':
        m.sc = event.sc; m.si = event.si; m.streak = 0;
        break;
      case 'TICK':
        m.sc = event.sc; m.si = event.si;
        // t_us is device uptime. t_run_us is elapsed scored time.
        m.elapsedMs = (event.t_run_us || 0) / 1000;
        break;
      case 'END':
        m.state = 'ENDED';
        m.targetCell = -1;
        this._acceptFinal(event);
        break;
      case 'SELFTEST':
        // Carries no game state, so the mirror has nothing to update. It is still
        // dispatched to listeners below, which is how the calibration view renders
        // each cell's verdict.
        break;
      case 'LINK':
        // The host pushes this when the keypad is plugged in or pulled out, so it is
        // what makes hot-plug visible. Fold it into `device` as well as the link
        // state: that object is what the UI asks for presence, and leaving it stale
        // would show a keypad that is no longer there.
        this.device.connected = !!event.connected;
        this.device.reason = event.reason ||
          (event.connected ? 'connected' : 'the keypad was disconnected');
        if (event.port !== undefined) { this.device.port = event.port; }
        this._setLink(event.connected ? 'ok' : 'error', this.device.reason);
        break;
      default:
        break;
    }

    this._fire(event.type, event);
  };

  /**
   * Accept the device's authoritative end-of-run tally.
   *
   * The mirror's counters are compared against it purely so a discrepancy is
   * recorded. Where they differ, the device wins - it is the only party that saw
   * every keypress with a hardware timestamp.
   */
  DeviceTransport.prototype._acceptFinal = function (event) {
    var elapsedS = (event.t_us || 0) / 1e6;
    var mismatches = [];
    if (this.mirror.sc !== event.sc) {
      mismatches.push('Sc display=' + this.mirror.sc + ' device=' + event.sc);
    }
    if (this.mirror.si !== event.si) {
      mismatches.push('Si display=' + this.mirror.si + ' device=' + event.si);
    }
    this.reconciliation = {
      agreed: mismatches.length === 0,
      mismatches: mismatches,
      seqGaps: this.link.seqGaps
    };
    if (mismatches.length) {
      this._setLink('warn', 'display drifted from device — device tally used');
    }

    // Recompute B from the device's own Sc, Si, N and t. The device also sends its
    // own b_mbps; comparing the two catches a firmware scoring bug rather than
    // trusting a single number computed once.
    var recomputedMbps = GP.scoring.bitRateMbps(event.n, event.sc, event.si, elapsedS);
    if (typeof event.b_mbps === 'number' && event.b_mbps !== recomputedMbps) {
      this.reconciliation.agreed = false;
      this.reconciliation.mismatches.push(
        'B device=' + (event.b_mbps / 1000).toFixed(3) +
        ' recomputed=' + (recomputedMbps / 1000).toFixed(3)
      );
    }

    this.finalReport = {
      specVersion: '1.0.0',
      mode: event.mode || 'EVAL',
      inputMode: 'hardware',
      state: 'ENDED',
      seed: event.seed !== undefined ? event.seed : this.mirror.seed,
      n: event.n,
      correct: event.sc,
      incorrect: event.si,
      elapsedS: elapsedS,
      wallElapsedS: elapsedS,
      bitRate: GP.scoring.bitRate(event.n, event.sc, event.si, elapsedS),
      bitRateMbps: recomputedMbps,
      bitRateWallclock: GP.scoring.bitRate(event.n, event.sc, event.si, elapsedS),
      bitsPerSelection: GP.scoring.bitsPerSelection(event.n),
      accuracy: (event.sc + event.si) > 0 ? event.sc / (event.sc + event.si) : 0,
      pressesPerSecond: elapsedS > 0 ? (event.sc + event.si) / elapsedS : 0,
      maxStreak: event.max_streak || 0,
      draws: event.draws || 0,
      repeatCount: event.repeats || 0,
      ignoredKeyPresses: 0,
      focusInterrupted: false,
      pausedMs: 0,
      reactionMs: {
        p50: (event.p50_us || 0) / 1000,
        p95: (event.p95_us || 0) / 1000,
        p99: (event.p99_us || 0) / 1000,
        min: (event.min_us || 0) / 1000,
        count: event.sc
      },
      perCellHits: event.hist || [],
      perCellTargets: event.hist || [],
      reconciliation: this.reconciliation
    };
  };

  // -- commands ---------------------------------------------------------------

  DeviceTransport.prototype._command = function (name, args) {
    return fetch(COMMAND_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: name, args: args || {} })
    }).then(function (response) {
      if (!response.ok) { throw new Error('command rejected: ' + response.status); }
      return response.json();
    });
  };

  DeviceTransport.prototype.start = function (mode) {
    var self = this;
    this.eventLog = [];
    this.finalReport = null;
    this.reconciliation = null;
    this.link.seqGaps = 0;

    // Show the countdown from the moment the player commits, rather than leaving the
    // screen blank across the stream handshake and the USB round trip. This is
    // presentation only, and it is the same display-only anchoring already applied to
    // countdownStartedMs on the device's own MODE COUNTDOWN: that event re-anchors
    // this a few milliseconds later, and RUNNING - the transition that starts the
    // scored clock - remains the device's alone. Nothing here touches the score.
    this.mirror.state = 'COUNTDOWN';
    this.mirror.countdownStartedMs = performance.now();

    return this.connect().then(function () {
      return self._command('START', { mode: mode });
    });
  };

  DeviceTransport.prototype.abort = function () {
    return this._command('ABORT', {});
  };

  /**
   * Run the calibration walk.
   *
   * `force` retests cells a previous walk marked dead, which is what makes a bad
   * verdict recoverable without reflashing - a re-soldered joint, or simply a slow
   * finger, is picked back up. It defaults to true for exactly that reason: the
   * failure mode of not forcing (a cell silently staying excluded forever, quietly
   * lowering N) is far worse than the cost of retesting a cell that is genuinely dead.
   */
  DeviceTransport.prototype.selftest = function (force) {
    var self = this;
    var wanted = (force === undefined) ? true : !!force;
    // Same ordering requirement as start(): the walk reports its first cells
    // immediately, and those verdicts are only ever sent once.
    return this.connect().then(function () {
      return self._command('SELFTEST', { force: wanted });
    });
  };

  /** Time transitions are the device's job; nothing to advance here. */
  DeviceTransport.prototype.tick = function () {
    return this.mirror.state;
  };

  DeviceTransport.prototype.dispose = function () { this.disconnect(); };

  DeviceTransport.prototype.snapshot = function (nowMs) {
    var m = this.mirror;
    var elapsedS = m.elapsedMs / 1000;
    var countdownRemainingMs = (m.state === 'COUNTDOWN')
      ? Math.max(0, GP.session.K_COUNTDOWN_MS -
          ((nowMs === undefined ? performance.now() : nowMs) - m.countdownStartedMs))
      : 0;
    return {
      state: m.state,
      n: m.n,
      sc: m.sc,
      si: m.si,
      streak: m.streak,
      targetCell: m.targetCell,
      targetIsRepeat: m.targetIsRepeat,
      elapsedMs: m.elapsedMs,
      remainingMs: Math.max(0, m.durationMs - m.elapsedMs),
      durationMs: m.durationMs,
      countdownRemainingMs: countdownRemainingMs,
      bps: GP.scoring.bitRate(m.n, m.sc, m.si, elapsedS),
      lastLatencyMs: m.lastLatencyMs,
      isPaused: false
    };
  };

  DeviceTransport.prototype.report = function () { return this.finalReport; };

  DeviceTransport.prototype.logAsJsonl = function (report) {
    var lines = [JSON.stringify({
      type: 'SESSION', specVersion: '1.0.0', inputMode: 'hardware',
      port: this.device.port, recordedAt: new Date().toISOString()
    })];
    for (var i = 0; i < this.eventLog.length; i++) {
      lines.push(JSON.stringify(this.eventLog[i]));
    }
    if (report) { lines.push(JSON.stringify({ type: 'REPORT', report: report })); }
    return lines.join('\n') + '\n';
  };

  GP.DeviceTransport = DeviceTransport;
});
