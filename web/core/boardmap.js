/**
 * GRID PULSE - the two hardware mapping tables, mirrored from the firmware.
 *
 * MUST STAY IN SYNC WITH firmware/include/board_map.h. That header is the single
 * source of truth for the device; this file exists so the browser can label a
 * diagnostics view with real GPIO and pixel numbers, which is what makes a mapping
 * bug visible to the eye instead of merely producing a low score.
 *
 * THERE ARE TWO INDEPENDENT MAPPINGS AND THEY ARE NOT THE SAME.
 *
 *   1. cell -> switch GPIO   follows the physical wiring, which is scrambled. It
 *                            cannot be derived arithmetically from anything and is
 *                            written out explicitly here and nowhere else.
 *   2. cell -> LED strip index  follows the serpentine layout of the WS2812B strip:
 *                            row 0 left-to-right, row 1 right-to-left, and so on.
 *
 * Conflating the two is the single most likely source of a subtle bug in this
 * project, which is why they are separate named tables, why the round trip is unit
 * tested in both directions, and why the device self-test walks the grid in GRID
 * order rather than strip order - a swap then shows up immediately as a zig-zag
 * instead of a sweep.
 *
 * Cells are indexed 0..24 in row-major grid order: cell = row * 5 + col.
 */
(function (root, factory) {
  'use strict';
  var GP = root.GridPulse || (root.GridPulse = {});
  factory(GP);
  if (typeof module !== 'undefined' && module.exports) { module.exports = GP; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (GP) {
  'use strict';

  /*
   * Cell -> switch GPIO. Wiring order, NOT derivable.
   *
   *   row 0:   16  17  18  15  14
   *   row 1:   19  10  11  12  13
   *   row 2:   20  21   6   9   8
   *   row 3:    2   3   4   5   7
   *   row 4:   22  26  27   1   0
   */
  var CELL_TO_GPIO = [
    16, 17, 18, 15, 14,
    19, 10, 11, 12, 13,
    20, 21,  6,  9,  8,
     2,  3,  4,  5,  7,
    22, 26, 27,  1,  0
  ];

  /*
   * Cell -> WS2812B strip index. Serpentine from the top-left.
   *
   *   row 0:    0   1   2   3   4
   *   row 1:    9   8   7   6   5
   *   row 2:   10  11  12  13  14
   *   row 3:   19  18  17  16  15
   *   row 4:   20  21  22  23  24
   */
  var CELL_TO_PIXEL = [
     0,  1,  2,  3,  4,
     9,  8,  7,  6,  5,
    10, 11, 12, 13, 14,
    19, 18, 17, 16, 15,
    20, 21, 22, 23, 24
  ];

  /** LED data line. Not a switch; deliberately excluded from CELL_TO_GPIO. */
  var LED_DATA_GPIO = 28;

  function invert(table, size) {
    var out = new Array(size).fill(-1);
    for (var cell = 0; cell < table.length; cell++) { out[table[cell]] = cell; }
    return out;
  }

  var PIXEL_TO_CELL = invert(CELL_TO_PIXEL, 25);

  /**
   * Structural self-check, run at load.
   *
   * These are the invariants the whole hardware mapping rests on. Checking them here
   * costs microseconds once and turns a silent mis-wire into a console error naming
   * the exact problem. The equivalent checks in the firmware are static_asserts.
   *
   * @returns {Array<string>} problems found; empty means healthy.
   */
  function validate() {
    var problems = [];

    if (CELL_TO_GPIO.length !== 25) { problems.push('CELL_TO_GPIO is not 25 entries'); }
    if (CELL_TO_PIXEL.length !== 25) { problems.push('CELL_TO_PIXEL is not 25 entries'); }

    var seenGpio = Object.create(null);
    for (var i = 0; i < CELL_TO_GPIO.length; i++) {
      var gpio = CELL_TO_GPIO[i];
      if (seenGpio[gpio]) { problems.push('GPIO ' + gpio + ' is assigned to two cells'); }
      seenGpio[gpio] = true;
      if (gpio === LED_DATA_GPIO) {
        problems.push('GPIO ' + gpio + ' is the LED data line and cannot be a switch');
      }
      // GP23, GP24 and GP25 are wired to internal functions on a Pico and are not
      // exposed on the header.
      if (gpio >= 23 && gpio <= 25) {
        problems.push('GPIO ' + gpio + ' is internal to the Pico and unusable');
      }
      if (gpio < 0 || gpio > 28) { problems.push('GPIO ' + gpio + ' is out of range'); }
    }

    // CELL_TO_PIXEL must be a permutation of 0..24, or some LED is unreachable.
    var sortedPixels = CELL_TO_PIXEL.slice().sort(function (a, b) { return a - b; });
    for (var p = 0; p < 25; p++) {
      if (sortedPixels[p] !== p) {
        problems.push('CELL_TO_PIXEL is not a permutation of 0..24 (missing ' + p + ')');
        break;
      }
    }

    // Round trip both ways.
    for (var c = 0; c < 25; c++) {
      if (PIXEL_TO_CELL[CELL_TO_PIXEL[c]] !== c) {
        problems.push('cell ' + c + ' does not round trip through the pixel map');
      }
    }

    return problems;
  }

  var problems = validate();
  if (problems.length && typeof console !== 'undefined') {
    console.error('GRID PULSE board map is invalid:\n  ' + problems.join('\n  '));
  }

  GP.boardmap = {
    CELL_TO_GPIO: CELL_TO_GPIO,
    CELL_TO_PIXEL: CELL_TO_PIXEL,
    PIXEL_TO_CELL: PIXEL_TO_CELL,
    LED_DATA_GPIO: LED_DATA_GPIO,
    validate: validate
  };
});
