/**
 * GRID PULSE - grid geometry and the Mode B keyboard alphabet.
 *
 * Cells are indexed 0..24 in row-major grid order (cell = row * 5 + col, row 0 top,
 * col 0 left). That is the only coordinate system the game core uses; GPIO numbers
 * and LED strip indices live in web/core/boardmap.js and firmware/include/board_map.h
 * and never leak in here.
 *
 * LETTER LAYOUT
 * -------------
 * 25 of the 26 letters, dropping M, laid out so each grid row is a contiguous run of
 * a QWERTY keyboard row:
 *
 *     row 0:  Q W E R T          row 3:  H J K L Z
 *     row 1:  Y U I O P          row 4:  X C V B N
 *     row 2:  A S D F G
 *
 * Alphabetical placement would force a lookup step - see lit cell, read its letter,
 * recall where that letter lives on the keyboard - which inserts a translation stage
 * into every single selection and roughly halves throughput. Keyboard-ordered
 * placement lets existing typing muscle memory carry the spatial mapping, so the
 * on-screen grid and the physical keyboard have the same shape and Mode B measures
 * the same reaction-and-targeting skill as the hardware keypad does.
 *
 * PHYSICAL KEYS, NOT CHARACTERS
 * -----------------------------
 * Matching is on event.code (KeyQ, KeyW, ...), which names the physical key position
 * and is layout-independent. event.key would break for anyone not on US QWERTY. Where
 * navigator.keyboard.getLayoutMap() is available we relabel the on-screen letters to
 * whatever that physical key actually produces for the player, so a Dvorak or AZERTY
 * grader sees correct labels on the same physical keys.
 *
 * Classic script, no module system - see web/core/rng.js for why.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
  if (typeof module !== 'undefined' && module.exports) { module.exports = GP; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  var K_GRID_ROWS = 5;
  var K_GRID_COLS = 5;
  var K_CELL_COUNT = K_GRID_ROWS * K_GRID_COLS;
  var K_DEFAULT_ALPHABET_SIZE = 25;

  /** Cell index -> default US QWERTY letter. Index is row-major grid order. */
  var DEFAULT_LABELS = [
    'Q', 'W', 'E', 'R', 'T',
    'Y', 'U', 'I', 'O', 'P',
    'A', 'S', 'D', 'F', 'G',
    'H', 'J', 'K', 'L', 'Z',
    'X', 'C', 'V', 'B', 'N'
  ];

  /** Cell index -> KeyboardEvent.code. This is the authoritative input mapping. */
  var CELL_TO_CODE = DEFAULT_LABELS.map(function (letter) { return 'Key' + letter; });

  /** KeyboardEvent.code -> cell index. Anything absent is not a game key. */
  var CODE_TO_CELL = (function () {
    var map = Object.create(null);
    for (var cell = 0; cell < CELL_TO_CODE.length; cell++) {
      map[CELL_TO_CODE[cell]] = cell;
    }
    return map;
  })();

  function rowOf(cell) { return Math.floor(cell / K_GRID_COLS); }
  function colOf(cell) { return cell % K_GRID_COLS; }

  /**
   * Resolve a keyboard event to a cell index.
   *
   * @returns {number} cell index, or -1 if this key is not part of the alphabet.
   *
   * A key that is not one of the 25 is neither a hit nor a miss - it is ignored
   * entirely and never reaches the scorer. See docs/GAME_CORE.md section 5.
   */
  function cellForCode(code) {
    var cell = CODE_TO_CELL[code];
    return (cell === undefined) ? -1 : cell;
  }

  function isGameKey(code) {
    return CODE_TO_CELL[code] !== undefined;
  }

  /**
   * Produce display labels for the 25 cells.
   *
   * Uses the Keyboard Map API when the browser exposes it (Chromium at the time of
   * writing; absent in Safari and Firefox) so the labels match the player's actual
   * layout. Falls back to US QWERTY labels, which the UI then flags as an assumption
   * rather than silently presenting as fact.
   *
   * @returns {Promise<{labels: Array<string>, source: string}>}
   */
  function resolveLabels() {
    var fallback = { labels: DEFAULT_LABELS.slice(), source: 'assumed-us-qwerty' };
    var nav = (typeof navigator !== 'undefined') ? navigator : null;
    if (!nav || !nav.keyboard || typeof nav.keyboard.getLayoutMap !== 'function') {
      return Promise.resolve(fallback);
    }
    return nav.keyboard.getLayoutMap().then(function (layoutMap) {
      var labels = CELL_TO_CODE.map(function (code, cell) {
        var mapped = layoutMap.get(code);
        if (typeof mapped !== 'string' || mapped.length === 0) {
          return DEFAULT_LABELS[cell];
        }
        return mapped.toUpperCase();
      });
      return { labels: labels, source: 'layout-map' };
    }, function () {
      // getLayoutMap can reject (permissions policy, detached document). A wrong
      // label is a cosmetic problem; a thrown error would break the launch screen.
      return fallback;
    });
  }

  GP.alphabet = {
    K_GRID_ROWS: K_GRID_ROWS,
    K_GRID_COLS: K_GRID_COLS,
    K_CELL_COUNT: K_CELL_COUNT,
    K_DEFAULT_ALPHABET_SIZE: K_DEFAULT_ALPHABET_SIZE,
    DEFAULT_LABELS: DEFAULT_LABELS,
    CELL_TO_CODE: CELL_TO_CODE,
    CODE_TO_CELL: CODE_TO_CELL,
    rowOf: rowOf,
    colOf: colOf,
    cellForCode: cellForCode,
    isGameKey: isGameKey,
    resolveLabels: resolveLabels
  };
});
