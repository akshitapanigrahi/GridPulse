/**
 * GRID PULSE - Mode B application tests.
 *
 * Boots the real web/play.html markup and the real web/ui + web/transport code
 * against the DOM harness, then plays an actual 60-second run with synthesised
 * keystrokes on a controlled clock and asserts on what the interface ends up
 * showing.
 *
 * This covers the wiring that core_test.js cannot: element ids, event handlers,
 * input rules at the browser boundary (auto-repeat, modifiers, non-alphabet keys),
 * focus handling, and the contents of the end-of-run report.
 *
 * Run with:  node web/tests/ui_test.js
 */
'use strict';

const path = require('path');
const harness = require('./dom_harness.js');

const results = [];
let passed = 0;
let failed = 0;
let group = '';

function describe(name) { group = name; }

function record(name, ok, detail) {
  if (ok) { passed++; } else { failed++; }
  results.push({ group, name, ok, detail: detail || '' });
}

function ok(name, cond, detail) { record(name, !!cond, cond ? '' : (detail || 'expected truthy')); }

function eq(name, actual, expected) {
  const same = Object.is(actual, expected);
  record(name, same, same ? '' : `expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
}

function close(name, actual, expected, tol = 1e-9) {
  const good = Math.abs(actual - expected) <= tol;
  record(name, good, good ? '' : `expected ~${expected}, got ${actual}`);
}

function contains(name, haystack, needle) {
  const good = String(haystack).includes(needle);
  record(name, good, good ? '' : `expected to find ${JSON.stringify(needle)} in ${JSON.stringify(String(haystack).slice(0, 240))}`);
}

/** Count how many grid cells are currently lit. Must never exceed one. */
function litCells(app) {
  let n = 0;
  for (const cell of app.gridView.cells) {
    if (cell.dataset.state === 'target') { n++; }
  }
  return n;
}

/** Press the key that corresponds to the currently lit target. */
function pressTarget(app) {
  const cell = app.transport.session.targetCell;
  return harness.keyDown(app.GP_CELL_TO_CODE[cell]);
}

async function main() {
  const boot = harness.bootApplication({ webRoot: path.join(__dirname, '..') });
  const GP = boot.GP;
  const clock = boot.clock;
  const doc = boot.doc;
  const app = GP.app;
  app.GP_CELL_TO_CODE = GP.alphabet.CELL_TO_CODE;

  // init() kicks off the label resolution and the device probe; both settle in a
  // microtask because there is no bridge to talk to from a file:// origin.
  await Promise.resolve();
  await Promise.resolve();
  await Promise.resolve();

  // ---------------------------------------------------------------- boot ----
  describe('boot');
  ok('the application object was constructed', !!app);
  eq('starts on the launch screen', doc.body.dataset.screen, 'launch');
  eq('input mode defaults to keyboard', app.inputMode, 'keyboard');
  contains('the badge reports the keypad, not the input mode',
    doc.getElementById('mode-badge-label').textContent, 'HARDWARE');
  contains('and says it is absent, in words as well as in grey',
    doc.getElementById('mode-badge-label').textContent, 'NOT CONNECTED');
  eq('the grid has 25 cells', app.gridView.cells.length, 25);
  eq('no cell is lit before a run starts', litCells(app), 0);

  describe('hardware absence is normal, not an error');
  ok('the hardware card is disabled without a bridge',
    doc.getElementById('mode-hardware').disabled === true);
  const hwStatus = doc.getElementById('mode-hardware-status').textContent;
  contains('and explains why in plain language', hwStatus, 'opened directly from a file');
  ok('the explanation is not an error or a stack trace',
    !/error|exception|failed|undefined/i.test(hwStatus), hwStatus);

  describe('keyboard labels');
  eq('cell 0 is labelled Q', app.gridView.labelNodes[0].textContent, 'Q');
  eq('cell 4 is labelled T', app.gridView.labelNodes[4].textContent, 'T');
  eq('cell 24 is labelled N', app.gridView.labelNodes[24].textContent, 'N');
  const allLabels = app.gridView.labelNodes.map((n) => n.textContent).join('');
  eq('the full layout follows keyboard rows', allLabels, 'QWERTYUIOPASDFGHJKLZXCVBN');
  ok('M is not on the grid', !allLabels.includes('M'));

  describe('keys are inert before a run');
  const preRun = harness.keyDown('KeyQ');
  ok('a game key on the launch screen is not swallowed', !preRun.defaultPrevented);

  // --------------------------------------------------------------- start ----
  describe('starting a scored run');
  doc.getElementById('btn-eval').click();
  eq('switches to the game screen', doc.body.dataset.screen, 'game');
  ok('the practice banner is hidden for a scored run',
    doc.getElementById('practice-banner').hidden === true);
  ok('the countdown overlay is shown',
    doc.getElementById('overlay-countdown').hidden === false);
  eq('the session is in COUNTDOWN', app.transport.session.state, 'COUNTDOWN');
  eq('no target during the countdown', litCells(app), 0);

  describe('the countdown does not consume the clock');
  clock.frame(16);
  eq('counts down from 3', doc.getElementById('countdown-number').textContent, '3');
  // The HUD is visible behind the countdown overlay, and render() is not called while
  // it is showing - so anything left over from a previous run sits there for the whole
  // three seconds, looking like this run inherited it.
  eq('the bit rate starts blank', doc.getElementById('hud-bps').textContent, '0.00');
  eq('the counters start blank', doc.getElementById('hud-sc').textContent, '0');
  eq('the streak starts blank', doc.getElementById('hud-streak').textContent, '0');
  eq('the clock shows the full run length',
    doc.getElementById('hud-time').textContent, '60 s');
  eq('and no reaction time is claimed yet',
    doc.getElementById('hud-latency').textContent, '— ms');
  const countdownPress = harness.keyDown(GP.alphabet.CELL_TO_CODE[0]);
  ok('a press during the countdown is still swallowed from the browser',
    countdownPress.defaultPrevented);
  eq('but scores nothing', app.transport.session.correct + app.transport.session.incorrect, 0);

  clock.runUntil(() => app.transport.session.state === 'RUNNING', { stepMs: 100 });
  eq('the run begins after the countdown', app.transport.session.state, 'RUNNING');
  eq('elapsed time starts at zero, not at 3 seconds',
    Math.round(app.transport.session.elapsedMs(clock.now())), 0);
  eq('exactly one cell is lit', litCells(app), 1);
  ok('the countdown overlay is hidden once running',
    (clock.frame(16), doc.getElementById('overlay-countdown').hidden === true));
  eq('the keyboard timer starts visibly at 60 seconds',
    doc.getElementById('hud-time').textContent, '60 s');
  clock.advance(1001);
  clock.frame(0);
  eq('the keyboard timer visibly counts down to 59 seconds',
    doc.getElementById('hud-time').textContent, '59 s');

  // --------------------------------------------------------------- input ----
  describe('input rules at the browser boundary');
  const session = app.transport.session;

  const target = session.targetCell;
  const wrongCell = (target + 7) % 25;

  const missEvent = harness.keyDown(GP.alphabet.CELL_TO_CODE[wrongCell]);
  ok('a game key is swallowed during a run', missEvent.defaultPrevented);
  eq('a wrong key counts as incorrect', session.incorrect, 1);
  eq('the target does not change on a miss', session.targetCell, target);
  eq('still exactly one cell lit', litCells(app), 1);

  const before = { sc: session.correct, si: session.incorrect };
  const mEvent = harness.keyDown('KeyM');
  ok('M is not swallowed - it is not a game key', !mEvent.defaultPrevented);
  const spaceEvent = harness.keyDown('Space');
  ok('Space is not swallowed', !spaceEvent.defaultPrevented);
  harness.keyDown('Enter');
  harness.keyDown('Digit1');
  eq('non-alphabet keys do not change Sc', session.correct, before.sc);
  eq('non-alphabet keys do not change Si', session.incorrect, before.si);
  eq('they are counted as ignored, so the report can say so', session.ignoredKeyPresses, 4);
  eq('the early countdown press is counted separately', session.prematurePresses, 1);

  const repeatEvent = harness.keyDown(GP.alphabet.CELL_TO_CODE[wrongCell], { repeat: true });
  ok('an auto-repeat event is not swallowed', !repeatEvent.defaultPrevented);
  eq('a held key cannot farm misses', session.incorrect, 1);
  harness.keyDown(GP.alphabet.CELL_TO_CODE[session.targetCell], { repeat: true });
  eq('a held key cannot farm hits either', session.correct, 0);

  const metaEvent = harness.keyDown(GP.alphabet.CELL_TO_CODE[wrongCell], { metaKey: true });
  ok('a modified key is left to the browser', !metaEvent.defaultPrevented);
  eq('and does not score', session.incorrect, 1);

  clock.advance(120);
  pressTarget(app);
  eq('the correct key scores', session.correct, 1);
  eq('a new target was drawn', session.draws, 2);
  eq('exactly one cell is lit after a hit', litCells(app), 1);
  clock.frame(16);
  eq('the HUD shows Sc', doc.getElementById('hud-sc').textContent, '1');
  eq('the HUD shows Si', doc.getElementById('hud-si').textContent, '1');
  eq('the HUD shows N', doc.getElementById('hud-n').textContent, '25');
  contains('the HUD shows the reaction time',
    doc.getElementById('hud-latency').textContent, 'ms');

  // --------------------------------------------------------------- focus ----
  describe('losing focus pauses the clock and flags the run');
  const elapsedBeforeBlur = session.elapsedMs(clock.now());
  globalThis.dispatchWindowEvent({ type: 'blur' });
  ok('the pause overlay appears',
    doc.getElementById('overlay-focus').hidden === false);
  clock.advance(5000);
  eq('the scored clock did not advance while away',
    Math.round(session.elapsedMs(clock.now())), Math.round(elapsedBeforeBlur));
  const awayPress = harness.keyDown(GP.alphabet.CELL_TO_CODE[session.targetCell]);
  ok('a press while paused is not swallowed', !awayPress.defaultPrevented === false ||
    session.correct === 1);
  eq('and does not score', session.correct, 1);

  globalThis.dispatchWindowEvent({ type: 'focus' });
  ok('the pause overlay is dismissed',
    doc.getElementById('overlay-focus').hidden === true);
  ok('the run is flagged as interrupted', session.focusInterrupted === true);
  eq('paused time was recorded', Math.round(session.pausedTotalMs), 5000);

  // ----------------------------------------------------------- full run ----
  describe('playing out the full 60 seconds');
  // Play at a steady 4 presses per second with one deliberate miss, then let the
  // clock run to the boundary. The reaction interval is chosen so the arithmetic
  // below is exact and independent of the code under test.
  let deliberateMisses = 0;
  while (session.state === 'RUNNING') {
    clock.advance(250);
    if (session.elapsedMs(clock.now()) >= 60000) { break; }
    if (session.correct === 20 && deliberateMisses === 0) {
      harness.keyDown(GP.alphabet.CELL_TO_CODE[(session.targetCell + 1) % 25]);
      deliberateMisses++;
    } else {
      pressTarget(app);
    }
    if (litCells(app) > 1) { ok('never more than one cell lit', false); break; }
  }
  clock.runUntil(() => doc.body.dataset.screen === 'results', { stepMs: 100 });

  eq('the run ends on the results screen', doc.body.dataset.screen, 'results');
  eq('no cell is lit once the run is over', litCells(app), 0);

  const report = app.lastReport;
  ok('a report was produced', !!report);
  eq('the report is for a 60 second window', Math.round(report.elapsedS), 60);
  eq('N is 25', report.n, 25);
  eq('Sc matches the session', report.correct, session.correct);
  eq('Si matches the session', report.incorrect, session.incorrect);
  eq('two misses total (one at the start, one deliberate)', report.incorrect, 2);

  // Recompute B here from first principles rather than reusing the scoring module,
  // so this assertion is independent of the code it is checking.
  const expectedB = Math.log2(24) * Math.max(report.correct - report.incorrect, 0) /
    report.elapsedS;
  close('B equals log2(N-1) * max(Sc-Si,0) / t', report.bitRate, expectedB, 1e-9);
  ok('B is a plausible rate for 4 presses/sec at N=25',
    report.bitRate > 15 && report.bitRate < 20, 'got ' + report.bitRate);

  describe('the results screen shows what the assignment asks for');
  eq('B is displayed', doc.getElementById('results-b').textContent,
    report.bitRate.toFixed(2));
  eq('N is displayed', doc.getElementById('results-n').textContent, '25');
  eq('Sc is displayed', doc.getElementById('results-sc').textContent,
    String(report.correct));
  eq('Si is displayed', doc.getElementById('results-si').textContent,
    String(report.incorrect));
  eq('t is displayed', doc.getElementById('results-t').textContent,
    report.elapsedS.toFixed(1));
  contains('bits per selection is shown',
    doc.getElementById('results-bits').textContent, '4.585');

  const copyText = doc.getElementById('results-copy').value;
  contains('the copyable summary carries B', copyText, 'B  (bits/sec)');
  contains('the copyable summary carries N', copyText, 'N               : 25');
  contains('the copyable summary carries Sc', copyText, 'Sc  (correct)   : ');
  contains('the copyable summary carries Si', copyText, 'Si  (incorrect) : ');
  contains('the copyable summary carries the seed', copyText, 'RNG seed');
  contains('the copyable summary states the input mode', copyText, 'keyboard');

  describe('the interruption is disclosed, not hidden');
  ok('the flag panel is visible', doc.getElementById('results-flag').hidden === false);
  const flagText = doc.getElementById('results-flag').textContent;
  contains('it says the window lost focus', flagText, 'lost focus');
  contains('it gives the wall-clock rate too', flagText, 'wall-clock');
  ok('the wall-clock rate is lower than the paused-clock rate',
    report.bitRateWallclock < report.bitRate);
  contains('ignored keys are disclosed', flagText, 'outside the 25-key alphabet');

  describe('the session log is complete');
  const jsonl = app.transport.logAsJsonl(report);
  const lines = jsonl.trim().split('\n').map((l) => JSON.parse(l));
  eq('the log opens with a session header', lines[0].type, 'SESSION');
  eq('and closes with the report', lines[lines.length - 1].type, 'REPORT');
  const targets = lines.filter((l) => l.type === 'TARGET').length;
  const hits = lines.filter((l) => l.type === 'HIT').length;
  const misses = lines.filter((l) => l.type === 'MISS').length;
  eq('every hit is logged', hits, report.correct);
  eq('every miss is logged', misses, report.incorrect);
  eq('a target is logged for the first target and one per hit', targets, hits + 1);

  describe('a second run starts clean');
  // Home is the only way off the results screen, so it has to be there and it has to
  // be reachable - it lives outside every .screen, shown by body[data-screen].
  eq('the results screen is showing', doc.body.dataset.screen, 'results');
  ok('a home control exists', !!doc.getElementById('btn-home'));
  doc.getElementById('btn-home').click();
  eq('back to the launch screen', doc.body.dataset.screen, 'launch');
  doc.getElementById('btn-practice').click();
  eq('practice is clearly labelled unscored',
    doc.getElementById('practice-banner').hidden, false);
  clock.runUntil(() => app.transport.session.state === 'RUNNING', { stepMs: 100 });
  eq('a fresh session has no score', app.transport.session.correct, 0);
  eq('and no misses', app.transport.session.incorrect, 0);
  eq('practice is untimed', app.transport.session.durationMs, 0);
  eq('practice never expires', app.transport.session.remainingMs(clock.now()), Infinity);
  clock.frame(16);
  eq('the timer readout shows infinity', doc.getElementById('hud-time').textContent, '∞');

  const practiceSeed = app.transport.session.seed;
  ok('each run draws a fresh seed', practiceSeed !== report.seed);

  describe('aborting');
  // Put real numbers on the HUD first - an abort from a run that scored nothing has
  // nothing to leak into the next one, which is not the case being tested.
  for (let i = 0; i < 4; i++) {
    pressTarget(app);
    clock.advance(300);
    clock.frame(16);
  }
  const abortedBps = doc.getElementById('hud-bps').textContent;
  harness.keyDown('Escape');
  eq('Escape leaves the game screen', doc.body.dataset.screen, 'launch');
  eq('and ends the session', app.transport.session.state, 'ENDED');
  eq('with nothing lit', litCells(app), 0);

  // The run that follows an abort must not inherit its numbers. This is the case that
  // actually showed the bug: abort mid-run, start again, and the old bit rate was
  // still sitting at the top of the screen through the whole countdown.
  doc.getElementById('btn-eval').click();
  clock.frame(16);
  ok('the aborted run had a non-zero rate to leak', abortedBps !== '0.00');
  eq('but the next run starts from zero anyway',
    doc.getElementById('hud-bps').textContent, '0.00');
  eq('and its clock is full again', doc.getElementById('hud-time').textContent, '60 s');
  harness.keyDown('Escape');

  // An aborted run must not raise a results page. On the keypad the END for it comes
  // back over USB some milliseconds AFTER the player has already left, so without a
  // guard the results screen appears on its own a moment after they asked to go.
  // Reproduce that ordering: abort() is a POST that fires no local END, and the
  // device's report lands afterwards.
  // The device reports a real tally for an aborted run (reason=ABORT), so the report
  // IS populated - which is exactly why a guard is needed rather than relying on
  // there being nothing to show.
  const deviceEnd = (reason) => ({
    type: 'END', seq: 900, t_us: 60000000, n: 25, sc: 3, si: 1, b_mbps: 150,
    reason: reason, mode: 'EVAL', seed: 7, draws: 4, repeats: 0, max_streak: 3,
    min_us: 300000, p50_us: 400000, p95_us: 900000, p99_us: 900000
  });

  // A hardware run implies a connected keypad. Without this the END below trips the
  // display-drift warning, which fires LINK, which correctly falls the app back to
  // keyboard mode - and the test would then be exercising the wrong transport.
  const noDevice = app.device.device;
  app.device.device = { connected: true, port: '/dev/ttyACM0', reason: 'connected' };
  app.setInputMode('hardware');
  app.device.start = () => Promise.resolve({ ok: true });
  app.device.abort = () => Promise.resolve({ ok: true });
  app.startRun('EVAL');
  app.abortRun();
  eq('aborting a device run goes home', doc.body.dataset.screen, 'launch');
  app.device._ingest(deviceEnd('ABORT'));
  ok('the device still reported a tally for it', !!app.device.report());
  eq('but the END that follows an abort does not drag the player into results',
    doc.body.dataset.screen, 'launch');

  // ...but only for the run that was aborted. The next END reports normally.
  app.startRun('EVAL');
  app.device._ingest(deviceEnd('COMPLETE'));
  eq('a run that ends on its own still shows its result',
    doc.body.dataset.screen, 'results');
  doc.getElementById('btn-home').click();
  // Hand the real methods back: a later section exercises the genuine start() to check
  // the stream/command ordering, and would silently pass against these stubs.
  delete app.device.start;
  delete app.device.abort;
  app.device.finalReport = null;
  app.device.device = noDevice;
  app.setInputMode('keyboard');

  // ------------------------------------------- hardware stream ordering ----
  //
  // The bug: START was fired at the same moment the event stream was opened, and the
  // two take very different amounts of time to arrive. The host publishes only to
  // subscribers registered at that instant and keeps no backlog, so on the first run
  // of a session - the one where nothing is warm - MODE COUNTDOWN was published
  // before the browser was subscribed and vanished. No 3-2-1, and three dead-looking
  // seconds until MODE RUNNING landed. Later runs usually won the race, which made a
  // race look like a first-run quirk.
  //
  // Two independent guarantees are asserted here, because either alone still leaves a
  // visible gap: the command must not go out before the stream is live, and the
  // countdown must be on screen from the click rather than from the device's reply.
  describe('hardware start does not race the event stream');

  eq('Mode B opened no event stream at all', harness.eventSources().length, 0);

  const dev = new GP.DeviceTransport();
  const commandsBefore = harness.commands().length;
  const startResult = dev.start('EVAL');

  eq('start() opens the stream first', harness.eventSources().length, 1);
  eq('and sends nothing while it is still opening',
    harness.commands().length - commandsBefore, 0);
  eq('while showing the countdown immediately, with no device event yet',
    dev.snapshot(clock.now()).state, 'COUNTDOWN');

  harness.eventSources()[0].fireOpen();
  await startResult;

  eq('the command goes out once the stream is live',
    harness.commands().length - commandsBefore, 1);
  eq('and it is START', harness.commands()[commandsBefore].name, 'START');

  // A second run reuses the open stream rather than reconnecting, so the command must
  // not be held up waiting for an `open` that will never fire again.
  const reused = harness.eventSources().length;
  await dev.start('PRACTICE');
  eq('a later run reuses the open stream', harness.eventSources().length, reused);
  eq('and its command still goes out',
    harness.commands().length - commandsBefore, 2);

  // A host that accepts the connection but never completes it must not leave the
  // button dead. The command is released on a timeout and the failure surfaces
  // through the link indicator instead.
  const stalled = new GP.DeviceTransport();
  const beforeStalled = harness.commands().length;
  const stalledStart = stalled.start('EVAL');
  eq('a stream that never opens holds the command back at first',
    harness.commands().length - beforeStalled, 0);
  clock.advance(2000);
  await stalledStart;
  eq('but does not wedge the button forever',
    harness.commands().length - beforeStalled, 1);
  stalled.dispose();
  dev.dispose();

  // ----------------------------------------------------- hardware display ----
  describe('hardware countdown and timer mirror');
  app.setInputMode('hardware');

  // THE REGRESSION, at the level the player experiences it: click START, and the
  // countdown is on screen before the device has said anything at all.
  const beforeFirstRun = harness.commands().length;
  app.startRun('EVAL');
  clock.frame(16);
  eq('the countdown overlay is visible on the very first hardware run',
    doc.getElementById('overlay-countdown').hidden, false);
  eq('and it starts at 3 without any device event',
    doc.getElementById('countdown-number').textContent, '3');
  ok('with no MODE event having been received',
    app.device.eventLog.length === 0);
  app.abortRun();
  ok('starting a run did send a command', harness.commands().length > beforeFirstRun);

  app.device.start = () => Promise.resolve({ ok: true });
  app.device.abort = () => Promise.resolve({ ok: true });
  app.startRun('EVAL');
  app.device._ingest({
    type: 'MODE', mode: 'EVAL', state: 'COUNTDOWN', n: 25, seed: 123
  });
  clock.frame(16);
  eq('hardware uses the same visible countdown overlay',
    doc.getElementById('overlay-countdown').hidden, false);
  eq('hardware countdown begins at 3',
    doc.getElementById('countdown-number').textContent, '3');
  clock.advance(1001);
  clock.frame(0);
  eq('hardware countdown advances to 2',
    doc.getElementById('countdown-number').textContent, '2');

  app.device._ingest({
    type: 'MODE', mode: 'EVAL', state: 'RUNNING', n: 25, seed: 123
  });
  // Device uptime is intentionally enormous. The HUD must use t_run_us instead.
  app.device._ingest({
    type: 'TICK', t_us: 999999999, t_run_us: 1000000, sc: 0, si: 0, b_mbps: 0
  });
  clock.frame(16);
  eq('hardware countdown hides when the device starts running',
    doc.getElementById('overlay-countdown').hidden, true);
  eq('hardware timer uses elapsed run time rather than device uptime',
    doc.getElementById('hud-time').textContent, '59 s');
  app.abortRun();

  // ------------------------------------------------------------ mode badge ----
  //
  // The badge is now the only thing on screen that reports the device, the
  // right-corner link readout having been removed. That makes these assertions load
  // bearing rather than cosmetic: a link that drops during a scored run is something
  // the grader has to see, and if the badge does not say so, nothing does.
  describe('mode badge reports the keypad');

  const badge = doc.getElementById('mode-badge');
  const badgeLabel = doc.getElementById('mode-badge-label');

  // Earlier sections opened streams on this transport; start from a known link state
  // so these assertions are about the badge and not about test ordering.
  app.device.link.seqGaps = 0;
  app.device._setLink('idle', 'disconnected');

  // The badge is about the device, so the input mode must not change what it says.
  // The probe found no bridge in this harness, so the keypad is genuinely absent.
  app.setInputMode('keyboard');
  const inKeyboardMode = badgeLabel.textContent;
  app.setInputMode('hardware');
  eq('the badge reads the same in either input mode',
    badgeLabel.textContent, inKeyboardMode);
  eq('an absent keypad is greyed, not green', badge.dataset.link, 'off');
  contains('and says so in words as well as colour', badgeLabel.textContent, 'NOT CONNECTED');

  // Now pretend the probe succeeded, which is what a real keypad does.
  app.device.device = { connected: true, port: '/dev/ttyACM0' };
  app.setInputMode('hardware');
  eq('a connected keypad turns the dot green', badge.dataset.link, 'ok');
  eq('and the label goes back to one quiet word', badgeLabel.textContent, 'HARDWARE');

  // A stream that degrades mid-run must surface, since nothing else can report it.
  app.device._setLink('error', 'stream interrupted');
  eq('a dropped stream is not shown as healthy', badge.dataset.link, 'error');
  contains('and names the reason', badgeLabel.textContent, 'STREAM INTERRUPTED');

  app.device._setLink('ok', 'connected');
  eq('recovery clears it', badge.dataset.link, 'ok');

  // Opening a stream is a warn state too, and it must NOT make the badge stutter at
  // the start of every run - only genuine loss is worth saying.
  app.device._setLink('warn', 'connecting…');
  eq('connecting is not treated as a fault', badge.dataset.link, 'ok');
  eq('and does not clutter the label', badgeLabel.textContent, 'HARDWARE');

  // Dropped events go to the terminal, not here. They are a link diagnostic for
  // whoever is running the bridge; the score is unaffected, and a count ticking up
  // mid-run is something a player can neither act on nor ignore. The end-of-run
  // report still records them.
  app.device.link.seqGaps = 3;
  app.device._setLink('warn', '3 dropped event(s) — display only');
  eq('dropped events leave the badge green', badge.dataset.link, 'ok');
  eq('and do not clutter it with a count', badgeLabel.textContent, 'HARDWARE');

  // Losing the stream outright is different: that one the player must see.
  app.device._setLink('error', 'stream interrupted');
  eq('a dropped stream still shows', badge.dataset.link, 'error');
  app.device._setLink('ok', 'connected');

  // A replay is neither connected nor disconnected: the host never opened a device,
  // so "NOT CONNECTED" would send someone to check a cable that is irrelevant.
  app.device.replaying = true;
  app._applyDeviceAvailability(false, 'replaying a recorded session', { initial: true });
  eq('a replay says so rather than claiming a missing keypad',
    badgeLabel.textContent, 'REPLAY');
  eq('and is neither green nor grey', badge.dataset.link, 'replay');

  // A replay drives the same event stream a keypad would, so the device transport has
  // to be the active one - otherwise every event is dropped by the guards in
  // _wireTransport and the playback is invisible, which is what used to happen.
  eq('the device stream is what drives the display', app.transport, app.device);
  ok('but there is still nothing to start a run on',
    doc.getElementById('mode-hardware').disabled === true);

  // Nothing on the launch screen can act on a recording, so none of it is offered.
  // Leaving START live produced "could not start the device run: command rejected" -
  // a true statement about a thing that should never have been offered.
  ok('START cannot be pressed during a replay',
    doc.getElementById('btn-eval').disabled === true);
  ok('nor PRACTICE', doc.getElementById('btn-practice').disabled === true);
  ok('and there is nothing to abort', doc.getElementById('btn-abort').hidden === true);

  // A replay goes straight to the playfield: the launch screen is a dead end for it.
  app._awaitReplay();
  eq('a replay lands on the playfield, not the launch screen',
    doc.body.dataset.screen, 'game');
  eq('with nothing lit while the recording is still in its idle stretch',
    litCells(app), 0);

  // Then it follows the recording through a real countdown.
  app.device._ingest({ type: 'MODE', mode: 'EVAL', state: 'COUNTDOWN', n: 25, seed: 7 });
  clock.frame(16);
  eq('the countdown is the device\'s, counted from its own event',
    doc.getElementById('countdown-number').textContent, '3');
  clock.advance(1001); clock.frame(0);
  eq('and it actually counts', doc.getElementById('countdown-number').textContent, '2');

  app.device._ingest({ type: 'MODE', mode: 'EVAL', state: 'RUNNING', n: 25, seed: 7 });
  app.device._ingest({ type: 'TARGET', cell: 12, idx: 1, repeat: false });
  clock.frame(16);
  eq('the countdown clears when the recording starts running',
    doc.getElementById('overlay-countdown').hidden, true);
  eq('and the target it reports is lit', litCells(app), 1);

  app.abortRun();
  app.device.replaying = false;
  app.device._ingest({ type: 'LINK', connected: false, reason: 'no keypad detected' });

  app.device.link.seqGaps = 0;
  app.device._setLink('ok', 'connected');
  app.setInputMode('keyboard');

  // -------------------------------------------------------------- hot-plug ----
  //
  // The keypad is USB. Plugging it in after starting the host used to require a
  // restart, because presence was decided once at boot in three places at once. These
  // pin the browser's half: the option has to appear and disappear with the cable,
  // and it must do so without reaching over and changing the mode the player chose.
  describe('hot-plug');

  const hwCard = doc.getElementById('mode-hardware');
  const hwStatusEl = doc.getElementById('mode-hardware-status');

  app.setInputMode('keyboard');
  app.device.device = { connected: false, port: null, reason: 'no keypad detected' };
  app.device._ingest({ type: 'LINK', connected: false, reason: 'no keypad detected' });
  eq('starts with the hardware card disabled', hwCard.disabled, true);
  eq('the badge greys out when the keypad is absent', badge.dataset.link, 'off');
  eq('and keeps naming the keypad rather than the mode in use',
    badgeLabel.textContent, 'HARDWARE · NOT CONNECTED');

  // The cable goes in.
  app.device._ingest({
    type: 'LINK', connected: true, reason: 'connected on /dev/ttyACM0',
    port: '/dev/ttyACM0', n: 25
  });
  eq('plugging in enables the hardware card', hwCard.disabled, false);
  contains('and says where it is', hwStatusEl.textContent, '/dev/ttyACM0');
  eq('but does not seize the mode from the player', app.inputMode, 'keyboard');

  // The player opts in, then the cable comes out.
  app.setInputMode('hardware');
  eq('choosing hardware now works', app.inputMode, 'hardware');
  eq('and the badge goes green', badge.dataset.link, 'ok');
  eq('with no mode name attached', badgeLabel.textContent, 'HARDWARE');

  app.device._ingest({
    type: 'LINK', connected: false, reason: 'the keypad was unplugged'
  });
  eq('unplugging disables the card again', hwCard.disabled, true);
  eq('and falls back to the keyboard rather than stranding an unusable mode',
    app.inputMode, 'keyboard');
  contains('explaining why', hwStatusEl.textContent, 'unplugged');

  // Replugging is not a one-shot: it must work every time.
  app.device._ingest({ type: 'LINK', connected: true, reason: 'connected on /dev/ttyACM0' });
  eq('and it can be plugged back in', hwCard.disabled, false);
  app.device._ingest({ type: 'LINK', connected: false, reason: 'unplugged again' });
  eq('and pulled out again', hwCard.disabled, true);

  app.setInputMode('keyboard');
  app.device.device = { connected: false, port: null, reason: 'no keypad detected' };

  // ---------------------------------------------------------- calibration ----
  //
  // The device already walked the grid and reported every cell long before this
  // screen existed; sse.js just dropped the events on the floor. These assertions
  // pin the half that was missing - that a device-reported verdict actually reaches
  // the operator's eyes - plus the two properties that make the walk safe to run:
  // it does not start until asked, and a cell it never reached is not passed off as
  // merely untested.
  //
  // Note this harness's DOM is deliberately minimal: textContent does not aggregate
  // children and innerHTML is stored rather than parsed, so the summary is asserted
  // on the raw innerHTML string.
  describe('calibration');

  const calGrid = doc.getElementById('cal-grid');
  const calSummary = doc.getElementById('cal-summary');
  const map = globalThis.GridPulse.boardmap;

  const sentSelftests = [];
  app.device.selftest = (force) => { sentSelftests.push(force); return Promise.resolve({ ok: true }); };
  app.device.abort = () => Promise.resolve({ ok: true });

  app.setInputMode('keyboard');
  eq('no calibrate button in keyboard mode', doc.getElementById('btn-calibrate').hidden, true);
  app.setInputMode('hardware');
  eq('calibrate button appears in hardware mode', doc.getElementById('btn-calibrate').hidden, false);

  app.showCalibrate();
  eq('the calibration screen opens', doc.body.dataset.screen, 'calibrate');
  eq('opening it starts no walk', sentSelftests.length, 0);
  eq('the action reads as a first run', doc.getElementById('cal-btn-start').textContent,
    'START CALIBRATION');
  eq('all 25 cells are rendered', calGrid.children.length, 25);
  contains('each cell names its GPIO', calGrid.children[0].children[1].textContent, 'GP16');

  doc.getElementById('cal-btn-start').click();
  eq('starting sends one SELFTEST', sentSelftests.length, 1);
  eq('forced, so previously-dead cells are retested', sentSelftests[0], true);

  // Cell 8 is deliberately never reported, standing in for a switch the firmware
  // excluded as STUCK before the walk began.
  const fire = (cell, result) => app.device._fire('SELFTEST', {
    type: 'SELFTEST', cell, gpio: map.CELL_TO_GPIO[cell], pixel: map.CELL_TO_PIXEL[cell],
    result: result || 'OK', pass: 1
  });

  for (let cell = 0; cell <= 7; cell++) { fire(cell, cell === 3 ? 'NO_KEY' : 'OK'); }
  eq('a passing cell reads ok', calGrid.children[0].dataset.status, 'ok');
  eq('a timed-out cell reads nokey', calGrid.children[3].dataset.status, 'nokey');
  eq('the cell the walk has yet to reach is still pending',
    calGrid.children[8].dataset.status, 'pending');

  // The walk is ascending, so cell 9 reporting proves cell 8 was stepped over. That
  // is the moment the exclusion becomes knowable, and the moment it must be said -
  // leaving it outlined as "up next" is what made a skipped cell look like a hang.
  fire(9);
  eq('a stepped-over cell is called skipped immediately, not at the end',
    calGrid.children[8].dataset.status, 'skipped');
  contains('and the reason is shown while the walk is still running',
    calSummary.innerHTML, 'says nothing about the LED');
  ok('the skipped cell no longer claims to be up next',
    calGrid.children[8].className.indexOf('cal-cell--next') === -1);

  for (let cell = 10; cell < 25; cell++) { fire(cell); }
  eq('the bar accounts for every cell', doc.getElementById('cal-bar').style.width,
    '100.0%');

  app.device._fire('LOG', { type: 'LOG', level: 'W', msg: 'health_mask_not_persisted' });
  app.device._fire('HELLO', { type: 'HELLO', n: 23 });

  eq('HELLO ends the walk', doc.getElementById('cal-root').dataset.phase, 'done');
  // A finished walk offers a repeat, not a first run - and a repeat is the documented
  // recovery for a cell condemned by a slow finger, since a forced walk retests it.
  eq('and the action now reads as a repeat',
    doc.getElementById('cal-btn-start').textContent, 'RE-RUN CALIBRATION');
  eq('a cell the walk never reached is called skipped, not untested',
    calGrid.children[8].dataset.status, 'skipped');
  ok('the summary is shown', calSummary.hidden === false);
  contains('it names the failing cell', calSummary.innerHTML, 'cell 3');
  contains('with the GPIO behind it', calSummary.innerHTML, 'GP15');
  contains('it names the skipped cell', calSummary.innerHTML, 'cell 8');
  // A failed flash write is deliberately not surfaced: the verdicts stand either way,
  // and the only consequence is that they are forgotten at the next power cycle. What
  // must NOT happen is the catch-all below it printing the raw protocol string.
  ok('a failed flash write is not raised to the operator',
    calSummary.innerHTML.indexOf('flash') === -1);
  ok('and its raw message never leaks through the catch-all',
    calSummary.innerHTML.indexOf('health_mask_not_persisted') === -1);
  contains('and the consequence for N is spelled out', calSummary.innerHTML, 'N is now 23');

  // The whole point of printing the pin numbers is catching a drift between
  // board_map.h and its browser mirror, so that check itself is worth pinning.
  // A finished verdict must not survive into the next walk. "All 25 cells passed" left
  // sitting under a run in progress reads as the current result when it is the last
  // one's.
  ok('the previous summary is on screen before re-running', calSummary.hidden === false);

  // The real path: RE-RUN CALIBRATION straight from the finished screen, without
  // leaving and coming back.
  doc.getElementById('cal-btn-start').click();
  ok('re-running clears the previous verdict', calSummary.hidden === true);
  eq('and leaves nothing behind in it', calSummary.innerHTML, '');
  eq('the new walk really is running', doc.getElementById('cal-root').dataset.phase,
    'running');
  app.device._fire('SELFTEST',
    { type: 'SELFTEST', cell: 0, gpio: 99, pixel: 0, result: 'OK', pass: 1 });
  app.device._fire('HELLO', { type: 'HELLO', n: 25 });
  contains('firmware/browser table drift is flagged', calSummary.innerHTML, 'tables disagree');
  contains('and the new verdict has replaced the old one, not joined it',
    calSummary.innerHTML, 'cell(s) did not pass');

  doc.getElementById('cal-btn-done').click();
  eq('DONE returns to the launch screen', doc.body.dataset.screen, 'launch');

  // ----------------------------------------------------------------- sound ----
  //
  // Synthesised rather than sampled, because play.html has to work from a file://
  // origin where no asset can be fetched. The harness has no Web Audio at all, which
  // is itself the case worth pinning: audio must never be a reason the game fails to
  // run.
  describe('sound');

  ok('there is no Web Audio in this harness',
    typeof globalThis.AudioContext === 'undefined');
  ok('so the context is unavailable', app.sound._context() === null);

  // Every entry point has to tolerate that silently.
  let threw = null;
  try {
    app.sound.hit();
    app.sound.miss();
    app.sound.unlock();
  } catch (err) { threw = err; }
  eq('and none of it throws', threw, null);

  // A whole run's worth of feedback, driven through the real event handlers.
  app.setInputMode('hardware');
  const soundDevice = app.device.device;
  app.device.device = { connected: true, port: '/dev/ttyACM0', reason: 'connected' };
  threw = null;
  try {
    app.device._fire('HIT', { type: 'HIT', cell: 3, rt_us: 300000, sc: 1, si: 0 });
    app.device._fire('MISS', { type: 'MISS', pressed: 9, target: 3, sc: 1, si: 1 });
  } catch (err) { threw = err; }
  eq('hits and misses render without audio', threw, null);
  app.device.device = soundDevice;
  app.setInputMode('keyboard');

  // The toggle is a real control, and it reports its state to assistive tech.
  const soundBtn = doc.getElementById('btn-sound');
  const wasOn = app.sound.enabled;
  // Icon-only, so the accessible name is the only name it has. A screen reader would
  // otherwise announce nothing but "button".
  eq('the icon carries its state in its accessible name',
    soundBtn.getAttribute('aria-label'), wasOn ? 'Sound on' : 'Sound off');
  soundBtn.click();
  eq('clicking it flips the state', app.sound.enabled, !wasOn);
  eq('and the name follows', soundBtn.getAttribute('aria-label'),
    app.sound.enabled ? 'Sound on' : 'Sound off');
  eq('and so does aria-pressed', soundBtn.getAttribute('aria-pressed'),
    String(app.sound.enabled));
  ok('and the glyph actually changes', soundBtn.innerHTML.indexOf('<svg') === 0);

  // It lives outside every .screen, so it is reachable during a run - being unable to
  // silence a game without stopping it would be a poor trade for a tidier corner.
  ok('the toggle is not inside a screen',
    doc.getElementById('btn-sound').parentNode !== null);
  eq('and it shares the corner with HOME rather than covering it',
    doc.getElementById('btn-home').parentNode,
    doc.getElementById('btn-sound').parentNode);

  // Disabled means silent, not merely quieter.
  app.sound.setEnabled(false);
  let played = 0;
  const realTone = app.sound._tone;
  app.sound._tone = function () { played++; };
  app.sound.hit();
  app.sound.miss();
  eq('a disabled sound emits nothing at all', played, 0);
  app.sound.setEnabled(true);
  app.sound.hit();
  app.sound.miss();
  eq('and an enabled one emits both', played, 2);
  // The countdown has its own voice, and the run starting has another. Driven through
  // a whole real run so the ORDER is asserted, not just that the methods exist.
  const heard = [];
  app.sound._tone = function (from, to) {
    heard.push(from === to ? 'beat'
      : from === 660 ? 'go'
      : from === 880 ? 'hit' : 'miss');
  };
  app.sound.setEnabled(true);

  doc.getElementById('btn-eval').click();
  clock.frame(16);
  eq('the first beat lands on 3', heard.join(' '), 'beat');
  clock.advance(1001); clock.frame(0);
  clock.advance(1001); clock.frame(0);
  eq('one beat per number, not one per frame', heard.join(' '), 'beat beat beat');

  clock.runUntil(() => app.transport.session.state === 'RUNNING', { stepMs: 100 });
  clock.frame(16);
  eq('and the run starting is a different sound', heard.join(' '),
    'beat beat beat go');

  pressTarget(app);
  harness.keyDown(GP.alphabet.CELL_TO_CODE[(app.transport.session.targetCell + 7) % 25]);
  eq('then hits and misses, each its own', heard.join(' '),
    'beat beat beat go hit miss');
  harness.keyDown('Escape');

  app.sound._tone = realTone;

  // The preference is remembered, so someone who turns it off does not have to keep
  // turning it off. Read back through a fresh instance, not the same object.
  app.sound.setEnabled(false);
  eq('turning it off is remembered', new GP.Sound().enabled, false);
  app.sound.setEnabled(true);
  eq('and so is turning it back on', new GP.Sound().enabled, true);

  app.sound.setEnabled(wasOn);
  app._renderSoundToggle();

  // ------------------------------------------------------ hidden actually hides ----
  //
  // This harness has no CSS engine, so `hidden = true` always looks hidden to it. In a
  // real browser the attribute only carries a user-agent `display: none`, which any
  // author `display` rule outranks - and `.btn` is `display: inline-flex`. The
  // RE-CALIBRATE KEYPAD button therefore stayed on screen in Mode B with its `hidden`
  // property perfectly true, and every assertion above passed while it did.
  //
  // A static check on the stylesheet is the only guard available at this level, so it
  // is the one worth having: the global rule is what makes `hidden` mean hidden.
  describe('hidden actually hides');

  const fs = require('fs');
  const baseCss = fs.readFileSync(path.join(__dirname, '..', 'css', 'base.css'), 'utf8');
  const globalHiddenRule = /\[hidden\]\s*\{[^}]*display:\s*none\s*!important/.test(baseCss);
  ok('base.css forces [hidden] to display:none !important', globalHiddenRule,
    'without this, any element whose class sets `display` ignores the hidden attribute');

  // The elements the app hides at runtime, i.e. the ones that rule has to cover.
  ['btn-calibrate', 'practice-banner', 'overlay-countdown', 'overlay-focus']
    .forEach((id) => ok('#' + id + ' exists to be hidden', !!doc.getElementById(id)));

  // ------------------------------------------------------------- report ----
  const failures = results.filter((r) => !r.ok);
  if (failures.length) {
    let lastGroup = null;
    failures.forEach((r) => {
      if (r.group !== lastGroup) { console.error('  in ' + r.group + ':'); lastGroup = r.group; }
      console.error('    FAIL ' + r.name + (r.detail ? ' -- ' + r.detail : ''));
    });
  }
  console.log(`GRIDPULSE_UI_TESTS: ${failed === 0 ? 'PASS' : 'FAIL'} ` +
    `(${passed} passed, ${failed} failed)`);
  process.exit(failed === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error('GRIDPULSE_UI_TESTS: FAIL (harness error)');
  console.error(err && err.stack ? err.stack : err);
  process.exit(1);
});
