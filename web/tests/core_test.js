/**
 * GRID PULSE - JavaScript game core test suite.
 *
 * Runs unmodified in three environments, because the suite must be runnable by a
 * grader who has none of node, a package manager, or a build step:
 *
 *   node   web/tests/core_test.js                    (exit code carries the result)
 *   jsc    web/core/*.js web/vectors.gen.js ...      (sentinel line carries it)
 *   browser  open web/tests.html                     (rendered on the page)
 *
 * The substantive job is proving that this implementation agrees exactly with the
 * independent Python reference implementation, via the golden vectors in
 * tests/vectors/ (mirrored into web/vectors.gen.js because a file:// origin cannot
 * fetch JSON). If the JS RNG diverges from the C++ RNG by so much as one draw, the
 * sequence tests here fail loudly and name the index where it first went wrong.
 *
 * CRC16 and wire-protocol framing are not exercised here: Mode B has no wire, and
 * those paths are covered by the native C++ and Python suites. The runner reports
 * that as an explicit note rather than leaving it silently absent.
 */
'use strict';

(function () {
  var isNode = (typeof process !== 'undefined' && process.versions && process.versions.node);
  var isBrowser = (typeof document !== 'undefined');

  var GP;
  var VEC;

  if (isNode) {
    GP = require('../core/rng.js');
    require('../core/scoring.js');
    require('../core/alphabet.js');
    require('../core/session.js');
    require('./sha256.js');
    VEC = require('../vectors.gen.js');
    GP = globalThis.GridPulse;
  } else {
    // Browser and jsc: every dependency has already been loaded into the shared
    // global scope by <script> tags or by the jsc argument list.
    GP = globalThis.GridPulse;
    VEC = globalThis.GRID_PULSE_VECTORS;
  }

  // --- tiny assertion framework -------------------------------------------

  function Runner() {
    this.results = [];
    this.group = '';
    this.passed = 0;
    this.failed = 0;
  }

  Runner.prototype.describe = function (name, body) {
    this.group = name;
    body(this);
    this.group = '';
  };

  Runner.prototype._record = function (name, ok, detail) {
    if (ok) { this.passed++; } else { this.failed++; }
    this.results.push({ group: this.group, name: name, ok: ok, detail: detail || '' });
  };

  Runner.prototype.ok = function (name, cond, detail) {
    this._record(name, !!cond, cond ? '' : (detail || 'expected truthy'));
  };

  Runner.prototype.eq = function (name, actual, expected) {
    var ok = Object.is(actual, expected);
    this._record(name, ok, ok ? '' : 'expected ' + expected + ', got ' + actual);
  };

  Runner.prototype.close = function (name, actual, expected, tol) {
    tol = (tol === undefined) ? 1e-9 : tol;
    var diff = Math.abs(actual - expected);
    var ok = diff <= tol || (expected !== 0 && diff / Math.abs(expected) <= tol);
    this._record(name, ok, ok ? '' : 'expected ~' + expected + ', got ' + actual +
      ' (diff ' + diff + ')');
  };

  Runner.prototype.arrayEq = function (name, actual, expected) {
    if (actual.length !== expected.length) {
      this._record(name, false, 'length ' + actual.length + ' != ' + expected.length);
      return;
    }
    for (var i = 0; i < expected.length; i++) {
      if (actual[i] !== expected[i]) {
        this._record(name, false,
          'first difference at index ' + i + ': expected ' + expected[i] +
          ', got ' + actual[i]);
        return;
      }
    }
    this._record(name, true);
  };

  Runner.prototype.throws = function (name, fn) {
    var threw = false;
    try { fn(); } catch (err) { threw = true; }
    this._record(name, threw, threw ? '' : 'expected a throw');
  };

  // --- helpers -------------------------------------------------------------

  /** A session driven by an explicit, caller-advanced clock. */
  function makeSession(overrides) {
    var config = {
      seed: 0xDEADBEEF,
      durationMs: 60000,
      countdownMs: 0,
      mode: 'EVAL'
    };
    for (var key in overrides) {
      if (Object.prototype.hasOwnProperty.call(overrides, key)) {
        config[key] = overrides[key];
      }
    }
    return new GP.session.Session(config);
  }

  /** Chi-square statistic for a histogram against a uniform expectation. */
  function chiSquare(histogram, total) {
    var expected = total / histogram.length;
    var stat = 0;
    for (var i = 0; i < histogram.length; i++) {
      var d = histogram[i] - expected;
      stat += (d * d) / expected;
    }
    return stat;
  }

  // --- the suite -----------------------------------------------------------

  function run() {
    var t = new Runner();

    if (!GP || !GP.rng || !GP.session) {
      t._record('bootstrap', false, 'GridPulse core not loaded');
      return t;
    }
    if (!VEC) {
      t._record('bootstrap', false, 'golden vectors not loaded (web/vectors.gen.js)');
      return t;
    }

    // -- the hash the vector comparison depends on --------------------------
    t.describe('sha256 (test support)', function (t) {
      t.eq('FIPS 180-4 "abc"', GP.sha256('abc'),
        'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
      t.eq('FIPS 180-4 empty', GP.sha256(''),
        'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
      t.eq('FIPS 180-4 two-block',
        GP.sha256('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
        '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
    });

    // -- 32-bit arithmetic hazards -------------------------------------------
    t.describe('32-bit arithmetic', function (t) {
      t.eq('rotl32 wraps bit 31', GP.rng.rotl32(0x80000000, 1), 1);
      t.eq('rotl32 by 0 is identity', GP.rng.rotl32(0xDEADBEEF, 0), 0xDEADBEEF);
      t.eq('rotl32 result is unsigned', GP.rng.rotl32(0x40000000, 1), 0x80000000);

      // The regression this whole vector apparatus exists for: a plain `*` loses low
      // bits once the product passes 2^53, and produces a different - wrong - answer.
      var imul = Math.imul(0xFFFFFFFF, 0x21F0AAAD) >>> 0;
      var naive = (0xFFFFFFFF * 0x21F0AAAD) >>> 0;
      t.eq('Math.imul is exact for large 32-bit products', imul, 3725546835);
      t.ok('naive multiply would silently differ', imul !== naive,
        'imul=' + imul + ' naive=' + naive + ' - if these ever match, this ' +
        'regression test has stopped protecting anything');

      // `>>` would sign-extend and corrupt SplitMix32's mixing; `>>>` must be used.
      t.eq('logical shift on a high-bit value', 0x80000000 >>> 31, 1);
      t.ok('arithmetic shift differs', (0x80000000 >> 31) !== (0x80000000 >>> 31));

      t.eq('splitmix32 advances its counter',
        GP.rng.splitmix32(0).state, 0x9E3779B9);
    });

    // -- RNG and sampler against the golden vectors --------------------------
    t.describe('RNG vs golden vectors', function (t) {
      t.eq('vector spec version', VEC.sequences.spec_version, '1.0.0');
      t.ok('vector cases present', VEC.sequences.cases.length > 0);

      VEC.sequences.cases.forEach(function (c) {
        var label = c.name;

        // Raw generator output, isolated from the sampler.
        var rawRng = new GP.rng.Xoshiro128StarStar(c.seed);
        var raw = [];
        for (var i = 0; i < c.raw16.length; i++) { raw.push(rawRng.nextU32()); }
        t.arrayEq(label + ': raw next() outputs', raw, c.raw16);

        // Full draw run: prefix, digest, rejection count, final state, histogram.
        var rng = new GP.rng.Xoshiro128StarStar(c.seed);
        var indices = new Array(c.draw_count);
        var rejections = 0;
        var histogram = new Array(c.n).fill(0);
        for (var d = 0; d < c.draw_count; d++) {
          var drawn = GP.rng.drawIndex(rng, c.n);
          indices[d] = drawn.index;
          rejections += drawn.rejections;
          histogram[drawn.index]++;
        }
        t.arrayEq(label + ': first ' + VEC.sequences.prefix_len + ' indices',
          indices.slice(0, VEC.sequences.prefix_len), c.prefix);
        t.eq(label + ': sha256 over all ' + c.draw_count + ' indices',
          GP.sha256(indices.join(',')), c.sha256);
        t.eq(label + ': rejected draws', rejections, c.rejections);
        t.arrayEq(label + ': state after run', rng.state(), c.state_after);
        t.arrayEq(label + ': histogram', histogram, c.histogram);
      });
    });

    t.describe('rejection sampling', function (t) {
      t.eq('LIMIT(25)', GP.rng.rejectionLimit(25), 4294967275);
      t.eq('LIMIT(3)', GP.rng.rejectionLimit(3), 4294967295);
      t.eq('LIMIT(24)', GP.rng.rejectionLimit(24), 4294967280);
      t.eq('LIMIT(2) divides evenly', GP.rng.rejectionLimit(2), 4294967296);
      t.throws('LIMIT(0) rejects', function () { GP.rng.rejectionLimit(0); });
      t.throws('LIMIT(-1) rejects', function () { GP.rng.rejectionLimit(-1); });

      // Unbiasedness, empirically. 24 degrees of freedom; the 99.9th percentile of
      // chi-square(24) is about 51.2, so a fixed-seed statistic well under that is
      // consistent with uniform. A modulo-biased sampler at N=25 would not be caught
      // by this alone - the vectors are what catch that - but a gross sampling bug is.
      var n25 = null;
      VEC.sequences.cases.forEach(function (c) {
        if (c.n === 25 && c.seed === 0xDEADBEEF) { n25 = c; }
      });
      if (n25) {
        var stat = chiSquare(n25.histogram, n25.draw_count);
        t.ok('chi-square over 10000 draws at N=25 is plausible (' + stat.toFixed(2) + ')',
          stat < 51.2, 'chi-square = ' + stat);
      } else {
        t.ok('N=25 vector case present', false);
      }

      // All indices in range, and every index actually reachable.
      var rng = new GP.rng.Xoshiro128StarStar(7);
      var seen = new Array(25).fill(false);
      var inRange = true;
      for (var i = 0; i < 5000; i++) {
        var idx = GP.rng.drawIndex(rng, 25).index;
        if (idx < 0 || idx >= 25) { inRange = false; }
        seen[idx] = true;
      }
      t.ok('all draws are in [0, N)', inRange);
      t.ok('every index occurs', seen.every(function (s) { return s; }));
    });

    // -- scoring against the golden vectors ----------------------------------
    t.describe('scoring vs golden vectors', function (t) {
      VEC.scoring_cases.cases.forEach(function (c) {
        t.close(c.name + ' [' + c.note + ']',
          GP.scoring.bitRate(c.n, c.correct, c.incorrect, c.elapsed_s),
          c.bit_rate, 1e-12);
        t.eq(c.name + ': milli-bits/s',
          GP.scoring.bitRateMbps(c.n, c.correct, c.incorrect, c.elapsed_s), c.b_mbps);
        t.close(c.name + ': bits per selection',
          GP.scoring.bitsPerSelection(c.n), c.bits_per_selection, 1e-12);
      });
    });

    t.describe('scoring guards', function (t) {
      t.eq('Si > Sc clamps to exactly 0', GP.scoring.bitRate(25, 10, 20, 60), 0);
      t.eq('Si == Sc is exactly 0', GP.scoring.bitRate(25, 10, 10, 60), 0);
      t.eq('t = 0 returns 0, not Infinity', GP.scoring.bitRate(25, 10, 0, 0), 0);
      t.eq('negative t returns 0', GP.scoring.bitRate(25, 10, 0, -5), 0);
      t.eq('N = 2 is not scorable', GP.scoring.bitRate(2, 10, 0, 60), 0);
      t.eq('N = 1 does not produce -Infinity', GP.scoring.bitRate(1, 10, 0, 60), 0);
      t.eq('N = 0 does not produce NaN', GP.scoring.bitRate(0, 10, 0, 60), 0);
      t.close('N = 3 gives exactly 1 bit per selection',
        GP.scoring.bitsPerSelection(3), 1, 1e-15);
      t.close('N = 25 gives log2(24)',
        GP.scoring.bitsPerSelection(25), 4.584962500721156, 1e-15);
      t.ok('bit rate is never negative',
        GP.scoring.bitRate(25, 0, 1000, 60) >= 0);
    });

    // -- alphabet -------------------------------------------------------------
    t.describe('keyboard alphabet', function (t) {
      var A = GP.alphabet;
      t.eq('25 cells', A.K_CELL_COUNT, 25);
      t.eq('25 labels', A.DEFAULT_LABELS.length, 25);
      t.eq('25 distinct labels', new Set(A.DEFAULT_LABELS).size, 25);
      t.ok('M is the dropped letter', A.DEFAULT_LABELS.indexOf('M') === -1);
      t.eq('25 distinct codes', new Set(A.CELL_TO_CODE).size, 25);

      var rowsMatchQwerty = true;
      var expectedRows = ['QWERT', 'YUIOP', 'ASDFG', 'HJKLZ', 'XCVBN'];
      for (var r = 0; r < 5; r++) {
        if (A.DEFAULT_LABELS.slice(r * 5, r * 5 + 5).join('') !== expectedRows[r]) {
          rowsMatchQwerty = false;
        }
      }
      t.ok('grid rows are contiguous QWERTY runs', rowsMatchQwerty);

      var roundTrip = true;
      for (var cell = 0; cell < 25; cell++) {
        if (A.cellForCode(A.CELL_TO_CODE[cell]) !== cell) { roundTrip = false; }
      }
      t.ok('cell -> code -> cell round trips', roundTrip);

      t.eq('unknown code is not a cell', A.cellForCode('KeyM'), -1);
      t.eq('Space is not a cell', A.cellForCode('Space'), -1);
      t.eq('Enter is not a cell', A.cellForCode('Enter'), -1);
      t.ok('KeyM is not a game key', !A.isGameKey('KeyM'));
      t.ok('KeyQ is a game key', A.isGameKey('KeyQ'));
      t.eq('cell 0 is top-left', A.rowOf(0) + ',' + A.colOf(0), '0,0');
      t.eq('cell 24 is bottom-right', A.rowOf(24) + ',' + A.colOf(24), '4,4');
      t.eq('cell 7 row/col', A.rowOf(7) + ',' + A.colOf(7), '1,2');
    });

    // -- session state machine -------------------------------------------------
    t.describe('session: hits, misses, and ignored keys', function (t) {
      var s = makeSession({});
      s.start(1000);
      t.eq('state is RUNNING once the countdown is skipped', s.state, 'RUNNING');
      t.eq('t0 is the first target presentation', s.t0Ms, 1000);
      t.ok('a target is lit', s.targetCell >= 0 && s.targetCell < 25);

      var target = s.targetCell;
      var wrong = (target + 1) % 25;

      t.eq('a wrong key is a miss', s.press(wrong, 1100), 'miss');
      t.eq('Si incremented', s.incorrect, 1);
      t.eq('Sc unchanged', s.correct, 0);
      t.eq('THE TARGET DOES NOT CHANGE ON A MISS', s.targetCell, target);

      t.eq('a second miss on the same target still counts', s.press(wrong, 1150), 'miss');
      t.eq('Si is 2', s.incorrect, 2);
      t.eq('target still unchanged', s.targetCell, target);

      t.eq('the correct key is a hit', s.press(target, 1200), 'hit');
      t.eq('Sc incremented', s.correct, 1);
      t.eq('streak is 1', s.streak, 1);
      t.ok('a new target was drawn', s.draws === 2);

      var newTarget = s.targetCell;
      t.eq('streak resets on a miss',
        (s.press((newTarget + 3) % 25, 1250), s.streak), 0);

      // Keys outside the alphabet must be inert: not a hit, not a miss, no streak change.
      var scBefore = s.correct;
      var siBefore = s.incorrect;
      t.eq('a non-alphabet key is ignored', s.press(-1, 1300), 'ignored');
      t.eq('a cell above the grid is ignored', s.press(99, 1310), 'ignored');
      t.eq('Sc untouched by ignored keys', s.correct, scBefore);
      t.eq('Si untouched by ignored keys', s.incorrect, siBefore);
      t.eq('ignored presses are counted separately', s.ignoredKeyPresses, 2);
    });

    t.describe('session: repeats are not resampled', function (t) {
      // The session's target stream must be exactly the sampler's output, repeats and
      // all. Resampling to avoid a consecutive repeat would break i.i.d. sampling and
      // leak information (after seeing x, the player would know x cannot be next).
      var seed = 0x5A5A5A5A;
      var s = makeSession({ seed: seed });
      var now = 0;
      s.start(now);
      var observed = [s.targetCell];
      for (var i = 0; i < 2000; i++) {
        now += 10;
        s.press(s.targetCell, now);
        observed.push(s.targetCell);
      }

      var rng = new GP.rng.Xoshiro128StarStar(seed);
      var expected = [];
      for (var j = 0; j < observed.length; j++) {
        expected.push(GP.rng.drawIndex(rng, 25).index);
      }
      t.arrayEq('target stream equals the raw sampler stream', observed, expected);

      var repeats = 0;
      for (var k = 1; k < observed.length; k++) {
        if (observed[k] === observed[k - 1]) { repeats++; }
      }
      t.ok('consecutive repeats do occur (' + repeats + ' in ' + observed.length + ')',
        repeats > 0, 'no repeats at all would mean the sequence is not i.i.d.');
      t.eq('the session counted the same repeats', s.repeatCount, repeats);
      // Expected repeat count is len/25 ~= 80; anything remotely near 0 or 2x is wrong.
      t.ok('repeat rate is near 1/N', repeats > 40 && repeats < 140,
        'saw ' + repeats + ', expected about ' + Math.round(observed.length / 25));
    });

    t.describe('session: countdown and timing authority', function (t) {
      var s = makeSession({ countdownMs: 3000 });
      s.start(5000);
      t.eq('start enters COUNTDOWN', s.state, 'COUNTDOWN');
      t.eq('no target during the countdown', s.targetCell, -1);
      t.eq('the clock is not running during the countdown', s.elapsedMs(7000), 0);
      t.eq('countdown remaining', s.countdownRemainingMs(6000), 2000);

      t.eq('presses during the countdown are ignored', s.press(0, 6000), 'ignored');
      t.eq('no score accrued during the countdown', s.correct + s.incorrect, 0);

      s.tick(8000);
      t.eq('RUNNING after the countdown elapses', s.state, 'RUNNING');
      t.eq('t0 is the target presentation, not the START command', s.t0Ms, 8000);
      t.eq('elapsed measured from t0', s.elapsedMs(9000), 1000);
    });

    t.describe('session: the 60-second boundary', function (t) {
      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      s.press(s.targetCell, 1000);
      t.eq('one hit recorded', s.correct, 1);

      t.eq('a press at 59.999 s still scores', s.press(s.targetCell, 59999), 'hit');
      t.eq('two hits recorded', s.correct, 2);

      t.eq('a press at exactly 60.000 s does not score', s.press(s.targetCell, 60000),
        'ignored');
      t.eq('the run ended', s.state, 'ENDED');
      t.eq('Sc frozen', s.correct, 2);

      t.eq('elapsed is frozen at exactly the scored duration', s.elapsedMs(75000), 60000);
      t.eq('further presses are ignored', s.press(s.targetCell, 61000), 'ignored');
      t.eq('Sc still frozen', s.correct, 2);

      var report = s.report(75000);
      t.eq('report elapsed is exactly 60 s', report.elapsedS, 60);
      t.eq('report N', report.n, 25);
      t.eq('report Sc', report.correct, 2);
      t.eq('report Si', report.incorrect, 0);
      t.close('report B', report.bitRate, Math.log2(24) * 2 / 60, 1e-12);
    });

    t.describe('session: tick-driven expiry', function (t) {
      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      s.tick(59999);
      t.eq('still running just before the boundary', s.state, 'RUNNING');
      s.tick(60001);
      t.eq('tick ends the run at the boundary', s.state, 'ENDED');
      t.eq('elapsed does not overrun past 60 s', s.elapsedMs(60001), 60000);
    });

    t.describe('session: scoring integration', function (t) {
      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      // 10 hits and 20 misses: Si > Sc, so the official rate must be exactly 0.
      var now = 0;
      for (var i = 0; i < 10; i++) {
        now += 100;
        s.press(s.targetCell, now);
      }
      for (var j = 0; j < 20; j++) {
        now += 100;
        s.press((s.targetCell + 1) % 25, now);
      }
      t.eq('Sc', s.correct, 10);
      t.eq('Si', s.incorrect, 20);
      t.eq('a net-negative run scores exactly 0', s.bitRate(now), 0);
      t.eq('and its report agrees', s.report(now).bitRate, 0);
      t.eq('and the wire form is 0', s.report(now).bitRateMbps, 0);
      t.close('accuracy', s.accuracy(), 10 / 30, 1e-12);
    });

    t.describe('session: focus interruption', function (t) {
      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      s.press(s.targetCell, 1000);
      t.ok('not flagged before any interruption', !s.focusInterrupted);

      s.pause(2000);
      t.eq('the scored clock stops while paused', s.elapsedMs(12000), 2000);
      t.ok('wall clock keeps running', s.wallElapsedMs(12000) === 12000);
      t.eq('presses while paused are ignored', s.press(s.targetCell, 5000), 'ignored');

      s.resume(12000);
      t.eq('paused time is excluded from t', s.elapsedMs(13000), 3000);
      t.eq('paused total recorded', s.pausedTotalMs, 10000);
      t.ok('the run is flagged as interrupted', s.focusInterrupted);

      var report = s.report(13000);
      t.eq('report carries pausedMs', report.pausedMs, 10000);
      t.eq('report elapsed excludes the pause', report.elapsedS, 3);
      t.eq('report wall elapsed includes it', report.wallElapsedS, 13);
      t.ok('the wall-clock rate is the lower of the two',
        report.bitRateWallclock < report.bitRate,
        'pausing must never be able to make the audited figure look worse than real');

      // An uninterrupted run must report the two rates as identical, so the presence
      // of a discrepancy is itself the signal.
      var clean = makeSession({ countdownMs: 0 });
      clean.start(0);
      clean.press(clean.targetCell, 500);
      var cleanReport = clean.report(10000);
      t.eq('an uninterrupted run has identical rates',
        cleanReport.bitRate, cleanReport.bitRateWallclock);
      t.ok('and is not flagged', !cleanReport.focusInterrupted);
    });

    t.describe('session: reduced alphabet (self-test exclusions)', function (t) {
      // A dead switch or LED in Mode A removes its cell from the alphabet forever.
      var healthy = [];
      for (var c = 0; c < 25; c++) { if (c !== 12 && c !== 7) { healthy.push(c); } }
      var s = makeSession({ alphabet: healthy });
      t.eq('N reflects the healthy cells', s.n, 23);

      s.start(0);
      var now = 0;
      var everTargetedDead = false;
      for (var i = 0; i < 3000; i++) {
        if (s.targetCell === 12 || s.targetCell === 7) { everTargetedDead = true; }
        now += 5;
        s.press(s.targetCell, now);
      }
      t.ok('excluded cells are never targeted', !everTargetedDead);
      t.eq('pressing an excluded cell is ignored', s.press(12, now + 5), 'ignored');
      t.close('bits per selection uses the reduced N',
        s.report(now).bitsPerSelection, Math.log2(22), 1e-12);

      t.throws('an alphabet below 3 is refused', function () {
        makeSession({ alphabet: [0, 1] });
      });
      t.throws('a duplicate cell is refused', function () {
        makeSession({ alphabet: [0, 1, 1, 2] });
      });
      var tiny = makeSession({ alphabet: [0, 1, 2] });
      t.eq('N = 3 is allowed', tiny.n, 3);
    });

    t.describe('session: percentiles and reaction times', function (t) {
      var p = GP.session.percentile;
      t.eq('percentile of an empty set is 0', p([], 50), 0);
      t.eq('p50 nearest-rank', p([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 50), 5);
      t.eq('p95 nearest-rank', p([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 95), 10);
      t.eq('p99 nearest-rank', p([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 99), 10);
      t.eq('p100', p([1, 2, 3], 100), 3);
      t.eq('single value', p([42], 99), 42);
      t.eq('sorts before ranking', p([9, 1, 5, 3, 7], 50), 5);

      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      s.press(s.targetCell, 200);   // 200 ms reaction
      s.press(s.targetCell, 500);   // 300 ms reaction
      var r = s.report(1000);
      t.eq('two reaction times recorded', r.reactionMs.count, 2);
      t.eq('minimum reaction time', r.reactionMs.min, 200);
      t.eq('p50 reaction time', r.reactionMs.p50, 200);

      // A miss must not record a reaction time - only hits do.
      s.press((s.targetCell + 1) % 25, 600);
      t.eq('a miss adds no reaction time', s.report(1000).reactionMs.count, 2);
    });

    t.describe('session: the cumulative rate uses ALL elapsed time', function (t) {
      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      // Ten fast hits, then a long idle stretch. The official rate must keep falling
      // as time passes, because t keeps growing while Sc - Si does not. That is the
      // whole point of it being cumulative rather than windowed.
      var now = 0;
      for (var i = 0; i < 10; i++) { now += 100; s.press(s.targetCell, now); }
      var early = s.bitRate(1000);
      t.ok('positive while playing', early > 0);
      var late = s.bitRate(31000);
      t.ok('decays over idle time', late < early);
      t.close('and equals log2(N-1) * net / t exactly',
        late, Math.log2(24) * 10 / 31, 1e-9);
      t.ok('there is no windowed variant on the session',
        typeof s.rollingBitRate === 'undefined');
      t.ok('nor on the scoring module',
        typeof GP.scoring.rollingBitRate === 'undefined');
    });

    t.describe('session: lifecycle guards', function (t) {
      var s = makeSession({ countdownMs: 0 });
      t.eq('presses before start are ignored', s.press(0, 0), 'ignored');
      t.eq('nothing scored', s.correct + s.incorrect, 0);
      s.start(0);
      t.throws('a session cannot be started twice', function () { s.start(100); });
      s.end(1000, 'ABORT');
      t.eq('ended', s.state, 'ENDED');
      t.eq('end is idempotent', (s.end(2000, 'ABORT'), s.endedAtMs), 1000);
      t.eq('no target while ended', s.targetCell, -1);
    });

    t.describe('session: event stream', function (t) {
      var events = [];
      var s = new GP.session.Session({
        seed: 1, countdownMs: 0, durationMs: 60000,
        onEvent: function (e) { events.push(e); }
      });
      s.start(0);
      var target = s.targetCell;
      s.press((target + 1) % 25, 100);
      s.press(target, 200);

      var types = events.map(function (e) { return e.type; });
      t.arrayEq('event order', types, ['MODE', 'TARGET', 'MISS', 'HIT', 'TARGET']);
      var hit = events[3];
      t.eq('HIT carries the reaction time', hit.reactionMs, 200);
      t.eq('HIT carries the running Sc', hit.sc, 1);
      t.eq('HIT carries the running Si', hit.si, 1);
      var miss = events[2];
      t.eq('MISS names the pressed cell', miss.pressed, (target + 1) % 25);
      t.eq('MISS names the unchanged target', miss.target, target);
    });

    t.describe('session: report completeness', function (t) {
      // The assignment requires B, N, Sc and Si to be reported. Assert they are all
      // present and well-formed, so a UI refactor cannot quietly drop one.
      var s = makeSession({ countdownMs: 0 });
      s.start(0);
      s.press(s.targetCell, 300);
      var r = s.report(60000);
      t.ok('B present and finite', typeof r.bitRate === 'number' && isFinite(r.bitRate));
      t.ok('N present', r.n === 25);
      t.ok('Sc present', r.correct === 1);
      t.ok('Si present', r.incorrect === 0);
      t.ok('seed present for reproducibility', typeof r.seed === 'number');
      t.eq('spec version', r.specVersion, '1.0.0');
      t.eq('input mode is stated', r.inputMode, 'keyboard');
      t.ok('per-cell target histogram is full width', r.perCellTargets.length === 25);
    });

    return t;
  }

  // --- reporting -----------------------------------------------------------

  function summarise(t) {
    var lines = [];
    var lastGroup = null;
    t.results.forEach(function (r) {
      if (!r.ok) {
        if (r.group !== lastGroup) { lines.push('  in ' + r.group + ':'); lastGroup = r.group; }
        lines.push('    FAIL ' + r.name + (r.detail ? ' -- ' + r.detail : ''));
      }
    });
    return lines;
  }

  var api = { run: run, summarise: summarise };

  if (isBrowser) {
    globalThis.GridPulseTests = api;
  } else {
    var result = run();
    var failures = summarise(result);
    if (failures.length) {
      failures.forEach(function (line) {
        if (isNode) { console.error(line); } else { print(line); }
      });
    }
    var verdict = result.failed === 0 ? 'PASS' : 'FAIL';
    var line = 'GRIDPULSE_JS_TESTS: ' + verdict + ' (' + result.passed +
      ' passed, ' + result.failed + ' failed)';
    var note = 'note: CRC16 and wire framing are covered by the native C++ and ' +
      'Python suites; Mode B has no wire.';
    if (isNode) {
      console.log(note);
      console.log(line);
      process.exit(result.failed === 0 ? 0 : 1);
    } else {
      // jsc: quit() does not set a shell exit code, so the sentinel line above is
      // what run.sh greps for.
      print(note);
      print(line);
    }
  }
})();
