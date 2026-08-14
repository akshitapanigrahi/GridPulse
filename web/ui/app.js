/**
 * GRID PULSE - application controller.
 *
 * Owns the screens, the render loop and the wiring between a transport and the
 * views. It is transport-agnostic on purpose: KeyboardTransport and DeviceTransport
 * expose the same small interface (start / abort / tick / snapshot / report /
 * logAsJsonl / on), so everything below works identically whether the game core is
 * running in this tab or on an RP2040.
 *
 * RENDER LOOP DISCIPLINE
 * ----------------------
 * requestAnimationFrame drives presentation only. It never gates scoring: a keypress
 * is timestamped and scored inside the keydown handler, so a dropped frame costs the
 * player nothing. Equally, target lighting is applied in the event handler rather
 * than waiting for the next frame, so the visible latency is one paint and no more.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var COUNTDOWN_STEPS = 3;

  function $(id) {
    var el = document.getElementById(id);
    if (!el) { throw new Error('missing required element #' + id); }
    return el;
  }

  function App() {
    this.body = document.body;
    this.el = {
      screenBody: this.body,
      grid: $('grid'),
      sparkline: $('sparkline'),
      countdownOverlay: $('overlay-countdown'),
      countdownNumber: $('countdown-number'),
      focusOverlay: $('overlay-focus'),
      practiceBanner: $('practice-banner'),
      modeBadge: $('mode-badge'),
      modeBadgeLabel: $('mode-badge-label'),
      hardwareCard: $('mode-hardware'),
      hardwareStatus: $('mode-hardware-status'),
      keyboardCard: $('mode-keyboard'),
      btnPractice: $('btn-practice'),
      btnEval: $('btn-eval'),
      btnAbort: $('btn-abort'),
      btnHome: $('btn-home'),
      btnCopy: $('btn-copy'),
      btnDownload: $('btn-download'),
      btnCalibrate: $('btn-calibrate'),
      btnSound: $('btn-sound'),
      howtoLayout: $('howto-layout')
    };

    this.calibrate = new GP.CalibrateView({
      root: $('cal-root'),
      grid: $('cal-grid'),
      bar: $('cal-bar'),
      summary: $('cal-summary'),
      btnStart: $('cal-btn-start'),
      btnCancel: $('cal-btn-cancel'),
      btnDone: $('cal-btn-done')
    });

    this.hud = new GP.Hud({
      bps: $('hud-bps'), n: $('hud-n'),
      sc: $('hud-sc'), si: $('hud-si'), streak: $('hud-streak'),
      time: $('hud-time'), latency: $('hud-latency'), timebar: $('timebar-fill')
    });

    this.results = {
      kicker: $('results-kicker'), b: $('results-b'), n: $('results-n'),
      sc: $('results-sc'), si: $('results-si'), t: $('results-t'),
      bits: $('results-bits'), formula: $('results-formula'),
      accuracy: $('results-accuracy'), pps: $('results-pps'),
      streak: $('results-streak'), latency: $('results-latency'),
      repeats: $('results-repeats'), inputMode: $('results-inputmode'),
      seed: $('results-seed'), flag: $('results-flag'), copy: $('results-copy')
    };

    this.gridView = new GP.GridView(this.el.grid);
    this.fx = new GP.Fx(this.gridView, this.body);
    this.sparkline = new GP.Sparkline(this.el.sparkline);
    this.sound = new GP.Sound();

    this.keyboard = new GP.KeyboardTransport();
    this.device = new GP.DeviceTransport();
    this.transport = this.keyboard;
    this.inputMode = 'keyboard';
    this.labels = GP.alphabet.DEFAULT_LABELS.slice();
    this.labelSource = 'assumed-us-qwerty';

    this.rafHandle = 0;
    this.lastCountdownShown = -1;
    this.lastReport = null;
    // Set by abortRun so the END that follows an abort does not raise a results page.
    this.abortedRun = false;

    this._boundFrame = this._frame.bind(this);
  }

  // -- boot -------------------------------------------------------------------

  App.prototype.init = function () {
    var self = this;

    this._wireButtons();
    this._wireTransport(this.keyboard);
    this._wireTransport(this.device);
    this.device.on('LINK', function () { self._renderBadge(); });

    // Calibration events. Only the device has these - there is nothing to calibrate
    // about a keyboard - so they are wired to the device transport directly rather
    // than through _wireTransport.
    this.device.on('SELFTEST', function (event) {
      self.calibrate.applyOutcome(event);
    });
    this.device.on('LOG', function (event) {
      self.calibrate.applyLog(event);
    });
    // The firmware re-announces itself once the walk finishes (FinishSelfTest calls
    // EmitHello), so HELLO arriving mid-calibration is the completion signal, and its
    // `n` is the alphabet size the device will actually play with from now on.
    this.device.on('HELLO', function (event) {
      if (self.body.dataset.screen === 'calibrate') { self.calibrate.finish(event.n); }
    });

    this.gridView.setLabels(this.labels);
    this._renderSoundToggle();
    this.setInputMode('keyboard');

    // Relabel the grid to the player's real layout where the browser can tell us.
    GP.alphabet.resolveLabels().then(function (resolved) {
      self.labels = resolved.labels;
      self.labelSource = resolved.source;
      if (self.inputMode === 'keyboard') { self.gridView.setLabels(self.labels); }
      self._renderLayoutNote();
    });

    // Probe for the keypad. Its absence is normal and must read as normal.
    this.el.hardwareStatus.textContent = 'looking for the keypad…';
    this.device.probe().then(function (result) {
      self.device.replaying = !!result.replaying;
      self._applyDeviceAvailability(result.available, result.reason, { initial: true });
      if (result.replaying) {
        // Straight to the playfield. A replay has nothing to choose on the launch
        // screen - no mode to pick, no run to start - so landing there first is a
        // dead end that only invites pressing a button that cannot work.
        self._awaitReplay();
      }
      if (result.available && result.selftestRequested) {
        // ./run.sh --selftest lands the operator straight on the calibration screen.
        self.showCalibrate();
      }
      // Open the event stream now rather than at the start of a run. The host pushes
      // a LINK event the moment the keypad is plugged in or pulled out, and a stream
      // that only exists during a run could not carry it - the page would keep
      // showing whatever was true when it loaded. Harmless when the bridge has no
      // device: the stream simply stays quiet.
      if (!result.fileOrigin) { self.device.connect(); }
    });

    // Follow the device into a run this page did not start.
    //
    // Needed for replay, where the run begins on its own - but true generally: the
    // browser is a display, and a display that only shows runs it initiated is not
    // mirroring the device. It also means a second tab follows along with the first.
    this.device.on('MODE', function (event) {
      if (self.transport !== self.device) { return; }
      if ((event.state === 'COUNTDOWN' || event.state === 'RUNNING') &&
          self.body.dataset.screen !== 'game') {
        self._followDeviceRun(event.mode);
      }
    });

    // Hot-plug. The keypad is USB and gets plugged in mid-session at least as often
    // as before one; the option to use it has to appear and disappear with the cable.
    // Read presence back off the transport rather than off the payload: 'LINK' is
    // fired both by the host's plug/unplug events and by local stream-health changes,
    // and only the former carries `connected`. The transport has already folded the
    // event in, so asking it cannot disagree with it.
    this.device.on('LINK', function () {
      self._applyDeviceAvailability(self.device.isAvailable(), self.device.device.reason);
    });

    // Keep the sparkline crisp across window resizes and monitor changes.
    globalThis.addEventListener('resize', function () {
      if (self.body.dataset.screen === 'game') { self.sparkline.draw(60); }
    });
  };

  App.prototype._wireButtons = function () {
    var self = this;
    this.el.keyboardCard.addEventListener('click', function () {
      self.setInputMode('keyboard');
    });
    this.el.hardwareCard.addEventListener('click', function () {
      if (!self.el.hardwareCard.disabled) { self.setInputMode('hardware'); }
    });
    // Browsers refuse to start audio outside a user gesture, so the click that starts
    // a run is also what unlocks it. Doing this at page load would create a context
    // born suspended and silently stay that way.
    this.el.btnPractice.addEventListener('click', function () {
      self.sound.unlock();
      self.startRun('PRACTICE');
    });
    this.el.btnEval.addEventListener('click', function () {
      self.sound.unlock();
      self.startRun('EVAL');
    });
    this.el.btnAbort.addEventListener('click', function () { self.abortRun(); });
    this.el.btnHome.addEventListener('click', function () { self.showLaunch(); });
    this.el.btnCopy.addEventListener('click', function () { self._copyResult(); });
    this.el.btnDownload.addEventListener('click', function () { self._downloadLog(); });
    this.el.btnCalibrate.addEventListener('click', function () { self.showCalibrate(); });
    this.el.btnSound.addEventListener('click', function () {
      self.sound.toggle();
      self._renderSoundToggle();
    });

    this.calibrate.onStart = function () { self.startCalibration(); };
    this.calibrate.onCancel = function () { self.cancelCalibration(); };
    this.calibrate.onDone = function () { self.showLaunch(); };

    // Click anywhere on the pause overlay to take focus back.
    this.el.focusOverlay.addEventListener('click', function () {
      globalThis.focus();
    });

    document.addEventListener('keydown', function (event) {
      if (event.code === 'Escape' && self.body.dataset.screen === 'game' &&
          !self.device.replaying) {
        self.abortRun();
      }
    });
  };

  App.prototype._wireTransport = function (transport) {
    var self = this;
    transport.on('TARGET', function (event) {
      if (self.transport !== transport) { return; }
      self.gridView.setTarget(event.cell, event.repeat);
    });
    transport.on('HIT', function (event) {
      if (self.transport !== transport) { return; }
      self.fx.hit(event.cell);
      self.sound.hit();
    });
    transport.on('MISS', function (event) {
      if (self.transport !== transport) { return; }
      self.fx.miss(event.pressed);
      self.sound.miss();
    });
    transport.on('END', function () {
      if (self.transport !== transport) { return; }
      self.finishRun();
    });
    transport.on('PAUSED', function () {
      if (self.transport !== transport) { return; }
      self.el.focusOverlay.hidden = false;
    });
    transport.on('RESUMED', function () {
      if (self.transport !== transport) { return; }
      self.el.focusOverlay.hidden = true;
    });
  };

  // -- mode selection ----------------------------------------------------------

  App.prototype.setInputMode = function (mode) {
    this.inputMode = mode;
    this.transport = (mode === 'hardware') ? this.device : this.keyboard;
    this.body.dataset.inputMode = mode;

    this.el.keyboardCard.setAttribute('aria-pressed', String(mode === 'keyboard'));
    this.el.hardwareCard.setAttribute('aria-pressed', String(mode === 'hardware'));
    this._renderBadge();
    // Calibration is a property of physical switches and LEDs; a keyboard has none.
    this.el.btnCalibrate.hidden = (mode !== 'hardware');

    // Letters belong to the keyboard. On the physical keypad the cells are blank and
    // the player reads position, exactly as on the board in front of them.
    this.gridView.setLabels(mode === 'keyboard' ? this.labels : null);
    this._renderLayoutNote();
  };

  // Inline SVG rather than an emoji or an icon font: nothing on this page may load an
  // external asset, and a font emoji renders differently on every machine. currentColor
  // means the CSS above can dim it when muted without a second copy of the artwork.
  var SPEAKER_ON =
    '<svg viewBox="0 0 24 24" aria-hidden="true">' +
    '<path d="M4 9v6h3.6L12 18.6V5.4L7.6 9H4z" fill="currentColor"/>' +
    '<path d="M15.4 8.8a4.6 4.6 0 0 1 0 6.4" fill="none" stroke="currentColor" ' +
    'stroke-width="1.8" stroke-linecap="round"/>' +
    '<path d="M17.9 6.4a8 8 0 0 1 0 11.2" fill="none" stroke="currentColor" ' +
    'stroke-width="1.8" stroke-linecap="round"/></svg>';

  var SPEAKER_OFF =
    '<svg viewBox="0 0 24 24" aria-hidden="true">' +
    '<path d="M4 9v6h3.6L12 18.6V5.4L7.6 9H4z" fill="currentColor"/>' +
    '<path d="M15.8 9.6l5.2 5.2M21 9.6l-5.2 5.2" fill="none" stroke="currentColor" ' +
    'stroke-width="1.8" stroke-linecap="round"/></svg>';

  App.prototype._renderSoundToggle = function () {
    var on = this.sound.enabled;
    var label = on ? 'Sound on' : 'Sound off';
    this.el.btnSound.innerHTML = on ? SPEAKER_ON : SPEAKER_OFF;
    // The button has no text, so its name has to come from here or a screen reader
    // announces nothing but "button".
    this.el.btnSound.setAttribute('aria-pressed', String(on));
    this.el.btnSound.setAttribute('aria-label', label);
    this.el.btnSound.setAttribute('title', label);
  };

  App.prototype._renderLayoutNote = function () {
    this.el.howtoLayout.textContent = (this.inputMode === 'hardware')
      ? 'The on-screen grid mirrors the physical keypad cell for cell.'
      : 'The grid follows QWERTY keyboard rows.';
  };

  /**
   * Render the one badge, which states the input mode and, in hardware mode, whether
   * the keypad is actually live.
   *
   * Healthy is deliberately silent: just the mode, with a green dot. A degraded link
   * appends its reason, so the badge growing is itself the signal - the same rule the
   * results screen uses for its flags. That matters because a link that drops during
   * a scored run is something the grader has to see, and this is now the only place
   * on screen that can tell them.
   */
  /**
   * Apply the keypad's presence to the interface.
   *
   * Called for the initial probe and for every later plug or unplug, so the two paths
   * cannot drift - the hot-plug case being the one nobody tests by hand.
   *
   * Selecting hardware is only automatic on the FIRST answer. Plugging a keypad in
   * later enables the option and says so; it does not reach over and change the mode
   * the player is in, which would be startling and could land mid-decision. Losing the
   * keypad is different: that is not a choice, so hardware mode falls back to the
   * keyboard rather than leaving a mode selected that cannot work.
   *
   * @param {boolean} available
   * @param {string} reason plain-language explanation, shown on the mode card
   * @param {{initial: boolean}} [options]
   */
  App.prototype._applyDeviceAvailability = function (available, reason, options) {
    var initial = !!(options && options.initial);

    // A replay is its own input mode in all but name. The recording drives the same
    // event stream a keypad would, so the device transport has to be the active one or
    // every event is dropped by the guards in _wireTransport and the playback is
    // invisible. The card stays disabled because there is still nothing to send a
    // START to.
    if (this.device.replaying) {
      this.el.hardwareCard.disabled = true;
      this.el.hardwareStatus.textContent = reason;
      this.el.hardwareStatus.classList.remove('mode-card__status--ok');
      if (this.inputMode !== 'hardware') { this.setInputMode('hardware'); }

      // Nothing here can start, stop or influence a recording. Leaving the buttons
      // live meant pressing START produced "could not start the device run: command
      // rejected" - a true statement about a thing that should never have been
      // offered.
      this.el.btnPractice.disabled = true;
      this.el.btnEval.disabled = true;
      this.el.btnAbort.hidden = true;
      this.el.btnCalibrate.hidden = true;

      this._renderBadge();
      return;
    }

    this.el.hardwareCard.disabled = !available;
    this.el.hardwareStatus.textContent = reason;
    if (available) {
      this.el.hardwareStatus.classList.add('mode-card__status--ok');
    } else {
      this.el.hardwareStatus.classList.remove('mode-card__status--ok');
    }

    if (available && initial) {
      this.setInputMode('hardware');
    } else if (!available && this.inputMode === 'hardware') {
      // Mid-run, leave the run alone: it is already lost, and the end-of-run report
      // records the dropped link. Yanking the screen out from under it would only
      // destroy the evidence.
      if (this.body.dataset.screen === 'launch') {
        this.setInputMode('keyboard');
      }
    }

    this._renderBadge();
  };

  App.prototype._renderBadge = function () {
    // The badge reports the KEYPAD, not the input mode. The keyboard is always
    // available, so naming it carries no information; whether the keypad is plugged
    // in changes minute to minute and is the only thing here worth a persistent
    // corner of the screen.
    //
    // Connectivity is what the probe answers. The event stream is a different thing
    // entirely: it only exists once a run starts, so reading the dot off it would
    // show a plugged-in keypad as disconnected for the whole launch screen.
    // A replay is neither connected nor disconnected: the host never opened a device.
    // Saying NOT CONNECTED would send someone to check a cable that is irrelevant.
    if (this.device.replaying) {
      this.el.modeBadge.dataset.link = 'replay';
      this.el.modeBadgeLabel.textContent = 'REPLAY';
      return;
    }

    var connected = this.device.isAvailable();
    var label = 'HARDWARE';
    var state = connected ? 'ok' : 'off';
    if (!connected) {
      // Not colour alone: grey and green are the same shape, and this project's rule
      // is that no state is signalled by hue by itself.
      label += ' · NOT CONNECTED';
    }

    // A stream that degrades mid-run does override, because that is news and this is
    // now the only place on screen that can report it. "Degraded" means lost, not
    // busy: `connecting…` is a warn state too, and letting that through would make
    // the badge stutter at the start of every single run for no information.
    // A stream that has actually dropped still overrides, because losing the keypad
    // mid-run is something the player has to see. Only while the keypad is present,
    // though: an unplug already sets the link to `error`, and letting that through
    // would paint the badge as a stream fault in magenta when the plain grey "not
    // connected" is both truer and less alarming.
    //
    // Dropped events deliberately do NOT appear here. They are a link diagnostic for
    // whoever is running the bridge, which prints them in the terminal; the score is
    // unaffected, and a count ticking up mid-run is something a player can neither act
    // on nor ignore. The end-of-run report still records them, where they belong.
    var link = this.device.link;
    if (connected && link.state === 'error') {
      state = link.state;
      label = 'HARDWARE · ' + link.text.toUpperCase();
    }

    this.el.modeBadge.dataset.link = state;
    this.el.modeBadgeLabel.textContent = label;
  };

  // -- run lifecycle -------------------------------------------------------------

  App.prototype.showLaunch = function () {
    this._stopLoop();
    this._stopCalibrationWatch();
    // Release the keyboard listeners, but NOT the device stream. Disposing that here
    // is what would make hot-plug work exactly once: the launch screen is precisely
    // where someone plugs the keypad in, and with the stream closed the page would
    // never hear about it.
    this.keyboard.dispose();
    this.fx.reset();
    this.gridView.clear();
    this.body.dataset.screen = 'launch';
  };

  // -- calibration ------------------------------------------------------------------

  /**
   * Open the calibration screen without starting the walk.
   *
   * Deliberately two steps. The walk gives you five seconds per cell, so firing it
   * the moment the screen appears would burn the first few cells while the operator
   * is still reading what they are supposed to do - and a cell that times out twice
   * is marked dead and lowers N. Showing the instructions first, and starting only on
   * an explicit click, means the clock starts when the player is ready for it.
   */
  App.prototype.showCalibrate = function () {
    this._stopLoop();
    this.calibrate.reset();
    this.body.dataset.screen = 'calibrate';
  };

  App.prototype.startCalibration = function () {
    var self = this;
    this.calibrate.begin();
    this._startCalibrationWatch();

    var sent = this.device.selftest(true);
    if (sent && typeof sent.catch === 'function') {
      sent.catch(function (err) {
        self.calibrate.applyLog({ level: 'E', msg: 'could not start calibration: ' + err.message });
        self.calibrate.stall();
      });
    }
  };

  App.prototype.cancelCalibration = function () {
    // ABORT drops the device out of self-test and back to idle. Whatever verdicts
    // already landed stay on screen; the device keeps the mask it had.
    this.device.abort();
    this._stopCalibrationWatch();
    this.body.dataset.screen = 'launch';
  };

  /** Drives the view's stall detector while a walk is in flight. */
  App.prototype._startCalibrationWatch = function () {
    var self = this;
    this._stopCalibrationWatch();
    this.calWatch = setInterval(function () { self.calibrate.tick(); }, 1000);
  };

  App.prototype._stopCalibrationWatch = function () {
    if (this.calWatch) { clearInterval(this.calWatch); this.calWatch = 0; }
  };

  App.prototype.startRun = function (mode) {
    this.fx.reset();
    this.hud.reset(mode === 'PRACTICE' ? 0 : GP.session.K_EVAL_DURATION_MS);
    this.sparkline.reset();
    this.gridView.clear();
    this.gridView.setHealthy(this.transport.alphabet || null);
    this.lastCountdownShown = -1;
    this.lastReport = null;
    this.abortedRun = false;

    this.el.practiceBanner.hidden = (mode !== 'PRACTICE');
    this.el.focusOverlay.hidden = true;
    // Both transports expose COUNTDOWN, so both modes use the same visible 3-2-1.
    this.el.countdownOverlay.hidden = false;
    this.body.dataset.screen = 'game';

    var self = this;
    var started = this.transport.start(mode);
    if (started && typeof started.catch === 'function') {
      started.catch(function (err) {
        self._failRun('could not start the device run: ' + err.message);
      });
    }
    this._startLoop();
  };

  /**
   * Show a run that started somewhere other than this page.
   *
   * Everything startRun does to prepare the screen, minus the part that tells a device
   * to begin - there is nothing to tell, the run is already under way.
   */
  /**
   * Sit on the playfield with nothing lit, waiting for the recording to reach its
   * first run.
   *
   * A log begins whenever the host was started, which can be a long way before anyone
   * pressed START - one of the sample logs has 63 seconds of it. So this is a real
   * state to be in rather than a flicker, and it should look like a game about to
   * begin rather than like something broken.
   */
  App.prototype._awaitReplay = function () {
    this.fx.reset();
    this.hud.reset(GP.session.K_EVAL_DURATION_MS);
    this.sparkline.reset();
    this.gridView.clear();
    this.el.practiceBanner.hidden = true;
    this.el.focusOverlay.hidden = true;
    this.el.countdownOverlay.hidden = true;
    this.body.dataset.screen = 'game';
    this._startLoop();
  };

  App.prototype._followDeviceRun = function (mode) {
    this.fx.reset();
    this.hud.reset(mode === 'PRACTICE' ? 0 : GP.session.K_EVAL_DURATION_MS);
    this.sparkline.reset();
    this.gridView.clear();
    this.lastCountdownShown = -1;
    this.lastReport = null;
    this.abortedRun = false;

    this.el.practiceBanner.hidden = (mode !== 'PRACTICE');
    this.el.focusOverlay.hidden = true;
    this.el.countdownOverlay.hidden = false;
    this.body.dataset.screen = 'game';
    this._startLoop();
  };

  App.prototype.abortRun = function () {
    // An abort is a decision to stop, not a result worth presenting.
    //
    // The run still ENDs properly - the device reports its tally with reason=ABORT and
    // the host writes it to the log, so nothing is hidden from an audit. What changes
    // is only where the player lands. The flag is needed because that END arrives
    // AFTER this function has already gone home: on the keypad it comes back over USB
    // some milliseconds later, and without it the results screen would appear on its
    // own a moment after the player asked to leave.
    this.abortedRun = true;
    this.transport.abort();
    this._stopLoop();
    this.fx.reset();
    this.gridView.clear();
    this.body.dataset.screen = 'launch';
  };

  App.prototype._failRun = function (message) {
    this._stopLoop();
    this.el.hardwareStatus.textContent = message;
    this.body.dataset.screen = 'launch';
  };

  App.prototype.finishRun = function () {
    this._stopLoop();
    this.gridView.clear();
    this.fx.reset();
    this.el.countdownOverlay.hidden = true;
    this.el.focusOverlay.hidden = true;

    // The player already left. See abortRun: this END is the device's report of a run
    // that was deliberately stopped, and it belongs in the log rather than on screen.
    if (this.abortedRun) {
      this.abortedRun = false;
      this.body.dataset.screen = 'launch';
      return;
    }

    var report = this.transport.report();
    if (!report) { this.body.dataset.screen = 'launch'; return; }
    this.lastReport = report;
    this._renderResults(report);
    this.body.dataset.screen = 'results';
  };

  // -- render loop -----------------------------------------------------------------

  App.prototype._startLoop = function () {
    if (this.rafHandle) { return; }
    this.rafHandle = requestAnimationFrame(this._boundFrame);
  };

  App.prototype._stopLoop = function () {
    if (this.rafHandle) { cancelAnimationFrame(this.rafHandle); this.rafHandle = 0; }
  };

  App.prototype._frame = function () {
    this.rafHandle = requestAnimationFrame(this._boundFrame);

    var now = performance.now();
    this.transport.tick(now);

    var snap = this.transport.snapshot(now);
    if (!snap) { return; }

    if (snap.state === 'COUNTDOWN') {
      this._renderCountdown(snap.countdownRemainingMs);
      return;
    }
    if (!this.el.countdownOverlay.hidden) {
      // Fires exactly once, on the frame the countdown gives way to the first target.
      this.el.countdownOverlay.hidden = true;
      this.sound.go();
    }

    this.hud.render({
      bps: snap.bps,
      n: snap.n,
      sc: snap.sc,
      si: snap.si,
      streak: snap.streak,
      remainingMs: snap.remainingMs,
      lastLatencyMs: snap.lastLatencyMs
    });
    this.hud.setTimeBar(snap.remainingMs, snap.durationMs);

    if (this.sparkline.sample(now, snap.elapsedMs / 1000, snap.bps)) {
      this.sparkline.draw((snap.durationMs || 60000) / 1000);
    }
  };

  App.prototype._renderCountdown = function (remainingMs) {
    this.el.countdownOverlay.hidden = false;
    var step = Math.min(COUNTDOWN_STEPS, Math.ceil(remainingMs / 1000));
    if (step === this.lastCountdownShown) { return; }
    this.lastCountdownShown = step;
    // One beat per number, and only for real numbers: the overlay can briefly compute
    // a step of 0 at the boundary, which is not a beat anyone is waiting for.
    if (step >= 1) { this.sound.countdown(); }
    this.el.countdownNumber.textContent = String(step);
    // Retrigger the entrance animation on each tick of the countdown.
    var node = this.el.countdownNumber;
    node.style.animation = 'none';
    void node.offsetWidth;
    node.style.animation = '';
  };

  // -- results ---------------------------------------------------------------------

  App.prototype._renderResults = function (r) {
    var unscored = (r.mode === 'PRACTICE');

    this.results.kicker.textContent = unscored
      ? 'PRACTICE COMPLETE — THIS RUN WAS NOT SCORED'
      : '60-SECOND EVALUATION COMPLETE';
    this.results.kicker.dataset.unscored = unscored ? '1' : '0';

    this.results.b.textContent = r.bitRate.toFixed(2);
    this.results.n.textContent = String(r.n);
    this.results.sc.textContent = String(r.correct);
    this.results.si.textContent = String(r.incorrect);
    this.results.t.textContent = r.elapsedS.toFixed(1);
    this.results.bits.textContent = r.bitsPerSelection.toFixed(3) + ' bits / selection';

    var net = Math.max(r.correct - r.incorrect, 0);
    this.results.formula.innerHTML =
      'B = log<sub>2</sub>(N−1) × max(S<sub>c</sub>−S<sub>i</sub>, 0) / t = ' +
      '<b>' + r.bitsPerSelection.toFixed(3) + '</b> × <b>' + net + '</b> / ' +
      '<b>' + r.elapsedS.toFixed(1) + '</b> = <b>' + r.bitRate.toFixed(3) + '</b> bits/s';

    this.results.accuracy.textContent = (r.accuracy * 100).toFixed(1) + '%';
    this.results.pps.textContent = r.pressesPerSecond.toFixed(2) + ' /s';
    this.results.streak.textContent = String(r.maxStreak);
    this.results.latency.textContent =
      Math.round(r.reactionMs.p50) + ' / ' + Math.round(r.reactionMs.p95) +
      ' / ' + Math.round(r.reactionMs.p99) + ' ms';
    this.results.repeats.textContent =
      r.repeatCount + ' of ' + r.draws + ' draws (expect ~' +
      Math.round(r.draws / r.n) + ')';
    this.results.inputMode.textContent =
      (r.inputMode === 'hardware') ? 'hardware keypad' : 'keyboard';
    this.results.seed.textContent = '0x' + (r.seed >>> 0).toString(16).toUpperCase();

    this._renderFlags(r);
    this.results.copy.value = this._resultText(r);
  };

  /**
   * Surface anything that makes this run less than a clean measurement. Present only
   * when there is something to say, so its appearance is itself the signal.
   */
  App.prototype._renderFlags = function (r) {
    var notes = [];
    if (r.focusInterrupted) {
      notes.push(
        'FLAGGED: the window lost focus for ' + (r.pausedMs / 1000).toFixed(1) +
        ' s during this run. The clock was paused, so the official rate above is ' +
        'computed over ' + r.elapsedS.toFixed(1) + ' s of play. Measured against ' +
        'wall-clock time instead, the rate would be ' +
        r.bitRateWallclock.toFixed(2) + ' bits/s.'
      );
    }
    if (r.reconciliation && !r.reconciliation.agreed) {
      notes.push('The on-screen counters drifted from the device tally (' +
        r.reconciliation.mismatches.join('; ') + '). The device figures are ' +
        'authoritative and are what is shown above.');
    }
    if (r.reconciliation && r.reconciliation.seqGaps > 0) {
      notes.push(r.reconciliation.seqGaps + ' event(s) were dropped on the USB link. ' +
        'Display only — the device tally is unaffected.');
    }
    if (r.ignoredKeyPresses > 0) {
      notes.push(r.ignoredKeyPresses + ' key press(es) outside the ' + r.n +
        '-key alphabet were ignored entirely, as specified: neither correct nor ' +
        'incorrect, and with no effect on the score.');
    }
    if (r.prematurePresses > 0) {
      notes.push(r.prematurePresses + ' game-key press(es) landed outside the scored ' +
        'window — during the countdown, while paused, or after time expired — and ' +
        'were not counted.');
    }
    this.results.flag.hidden = (notes.length === 0);
    this.results.flag.textContent = notes.join('  ');
  };

  App.prototype._resultText = function (r) {
    var lines = [
      'GRID PULSE — ' + (r.mode === 'PRACTICE' ? 'PRACTICE (UNSCORED)' : '60-SECOND RUN'),
      'input mode      : ' + (r.inputMode === 'hardware' ? 'hardware keypad' : 'keyboard'),
      '',
      'B  (bits/sec)   : ' + r.bitRate.toFixed(3),
      'N               : ' + r.n + '   (' + r.bitsPerSelection.toFixed(3) + ' bits/selection)',
      'Sc  (correct)   : ' + r.correct,
      'Si  (incorrect) : ' + r.incorrect,
      't   (seconds)   : ' + r.elapsedS.toFixed(3),
      '',
      'B = log2(N-1) * max(Sc-Si,0) / t = ' +
        r.bitsPerSelection.toFixed(3) + ' * ' + Math.max(r.correct - r.incorrect, 0) +
        ' / ' + r.elapsedS.toFixed(3) + ' = ' + r.bitRate.toFixed(3),
      '',
      'accuracy        : ' + (r.accuracy * 100).toFixed(1) + '%',
      'presses/sec     : ' + r.pressesPerSecond.toFixed(2),
      'best streak     : ' + r.maxStreak,
      'reaction p50/95/99 (ms): ' + Math.round(r.reactionMs.p50) + ' / ' +
        Math.round(r.reactionMs.p95) + ' / ' + Math.round(r.reactionMs.p99),
      'repeat targets  : ' + r.repeatCount + ' of ' + r.draws,
      'RNG seed        : 0x' + (r.seed >>> 0).toString(16).toUpperCase(),
      'spec version    : ' + r.specVersion
    ];
    if (r.focusInterrupted) {
      lines.push('');
      lines.push('FLAGGED: focus lost for ' + (r.pausedMs / 1000).toFixed(1) +
        ' s; wall-clock rate would be ' + r.bitRateWallclock.toFixed(3) + ' bits/s');
    }
    return lines.join('\n');
  };

  App.prototype._copyResult = function () {
    var self = this;
    var text = this.results.copy.value;
    var done = function () {
      self.el.btnCopy.textContent = 'COPIED';
      setTimeout(function () { self.el.btnCopy.textContent = 'COPY RESULT'; }, 1400);
    };
    // navigator.clipboard needs a secure context and is absent on file:// in some
    // browsers. Selecting the textarea always works, so fall back to that rather
    // than failing.
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done, function () {
        self.results.copy.select();
      });
    } else {
      this.results.copy.select();
      this.el.btnCopy.textContent = 'PRESS ⌘C / CTRL+C';
      setTimeout(function () { self.el.btnCopy.textContent = 'COPY RESULT'; }, 2200);
    }
  };

  /**
   * Download the session log.
   *
   * When the host bridge is running it has already written a JSON Lines log and a
   * CSV summary to logs/. A file:// page cannot write to disk at all, so this button
   * is how a filesystem-launched run still produces an auditable artefact.
   */
  App.prototype._downloadLog = function () {
    var text = this.transport.logAsJsonl(this.lastReport);
    var stamp = new Date().toISOString().replace(/[:.]/g, '-');
    var blob = new Blob([text], { type: 'application/x-ndjson' });
    var url = URL.createObjectURL(blob);
    var anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = 'gridpulse-' + stamp + '.jsonl';
    document.body.appendChild(anchor);
    anchor.click();
    document.body.removeChild(anchor);
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
  };

  GP.App = App;

  if (typeof document !== 'undefined') {
    var boot = function () {
      var app = new GP.App();
      GP.app = app;
      app.init();
    };
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', boot);
    } else {
      boot();
    }
  }
});
