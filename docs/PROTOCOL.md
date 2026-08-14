# GRID PULSE — Wire Protocol

**Protocol version: 1**

The USB CDC link between the RP2040 keypad and the Python host bridge. Line-oriented
ASCII, versioned, self-describing, and CRC-protected in both directions.

## Why this shape

The device is the authority. It runs the game core, draws the targets, classifies
every press, and timestamps everything from the RP2040 hardware timer. The host
displays what it is told and issues commands. Nothing on this wire lets the host
influence the target sequence — see §6.

ASCII rather than a packed binary struct: the whole link runs at a few hundred bytes
per second even during vigorous play, so compactness buys nothing, while being able
to `cat /dev/cu.usbmodem*` and read the game as it happens is worth a great deal when
something is wrong at 2am with unfamiliar hardware.

---

## 1. Framing

One message per line, terminated by a single `\n` (0x0A). A bare `\r` is tolerated
before the `\n` and stripped. Maximum line length including the terminator is
**320 bytes** (`kMaxLineBytes`); anything longer is a framing error and is dropped
with a warning rather than silently truncated.

The longest message is `END`, which carries the
run's entire result, and with every field at its maximum width it needs 297 bytes.
320 leaves headroom for one more field. `tests/native/test_protocol.cpp` asserts the
worst case actually fits, so a future field that would overflow the line fails the
test suite rather than silently truncating a result.

Every line has the form:

```
<payload> <crc16>\n
```

`<crc16>` is exactly four uppercase hex digits. The CRC is **CRC-16/CCITT-FALSE**
(polynomial `0x1021`, initial value `0xFFFF`, no input or output reflection, no final
XOR) computed over `<payload>` — every byte of the line *before* the single space
that separates the payload from the CRC field. The canonical check value is
`CRC16("123456789") = 0x29B1`.

Characters are restricted to printable ASCII (0x20–0x7E). No escaping, no quoting: no
field value may contain a space or an `=`.

---

## 2. Device → host: `EV`

```
EV <seq> <t_us> <TYPE> [key=value ...] <crc16>
```

| Field | Type | Meaning |
|---|---|---|
| `seq` | uint32 decimal | monotonic counter from device boot, wrapping at 2³². Increments by exactly 1 per emitted event. |
| `t_us` | uint64 decimal | device hardware timer, microseconds since boot. Not since run start — see `MODE`. |
| `TYPE` | token | one of the types in §3. |

A gap in `seq` means the host lost bytes. The host **must** surface this in the UI
rather than silently mis-scoring; the device's `END` tally remains correct regardless,
because it is computed on-device from state the host never touched.

---

## 3. Event types

### `HELLO`
Emitted on connect and in response to `CMD PING`. The host uses it to confirm the
protocol version before doing anything else.

| Key | Type | Meaning |
|---|---|---|
| `proto` | uint | protocol version (currently `1`) |
| `fw` | token | firmware version, semver |
| `board` | token | board identifier, e.g. `gridpulse-5x5` |
| `n` | uint | current alphabet size, after self-test exclusions |
| `pins_ok` | 0/1 | board map passed its structural self-check |

### `MODE`
Every state transition. `seed` is present on entry to `RUNNING` and is the value the
device drew from its ring oscillator — logged so any run is reproducible and auditable.

| Key | Type | Meaning |
|---|---|---|
| `mode` | token | `IDLE` \| `SELFTEST` \| `PRACTICE` \| `EVAL` |
| `state` | token | `IDLE` \| `COUNTDOWN` \| `RUNNING` \| `ENDED` |
| `seed` | uint32 | RNG seed for this run |
| `n` | uint | alphabet size locked in for this run |

### `SELFTEST`
One per cell, per pass. A cell that fails the first pass gets a second confirmation
pass before being declared dead, so a momentarily slow finger cannot permanently
shrink the alphabet.

| Key | Type | Meaning |
|---|---|---|
| `cell` | 0–24 | grid cell, row-major |
| `gpio` | uint | switch GPIO for that cell, from the wiring table |
| `pixel` | 0–24 | LED strip index for that cell, from the serpentine table |
| `result` | token | `OK` \| `NO_KEY` \| `STUCK` |
| `pass` | 1/2 | which pass produced this result |

Emitting `gpio` and `pixel` alongside `cell` is deliberate: it makes the two
independent mappings visible on the wire, so a swap is diagnosable from a log alone.

### `TARGET`
A new target has been drawn and lit. Exactly one cell is lit at any moment.

| Key | Type | Meaning |
|---|---|---|
| `cell` | 0–24 | the lit cell |
| `idx` | uint | draw ordinal within the run, from 1 |
| `repeat` | 0/1 | this target equals the previous one |

`repeat=1` is normal and expected roughly once every `N` draws. The sampler never
resamples to avoid a repeat — doing so would break i.i.d. sampling.

### `HIT`
| Key | Type | Meaning |
|---|---|---|
| `cell` | 0–24 | cell that was hit (equals the target) |
| `rt_us` | uint32 | microseconds from target presentation to key-down |
| `sc` | uint32 | running correct count |
| `si` | uint32 | running incorrect count |
| `streak` | uint32 | current streak |

A `TARGET` for the next draw always follows immediately.

### `MISS`
| Key | Type | Meaning |
|---|---|---|
| `pressed` | 0–24 | the cell actually pressed |
| `target` | 0–24 | the target, which does **not** change |
| `sc` | uint32 | running correct count |
| `si` | uint32 | running incorrect count |

### `TICK`
Heartbeat at **≥ 4 Hz**, unconditionally, including while the player is idle. The
assignment requires the displayed bit rate to update at least once per second; ticking
at four times that means the display stays live even if a tick is dropped.

| Key | Type | Meaning |
|---|---|---|
| `t_run_us` | uint64 | microseconds since the run's t₀ |
| `sc` | uint32 | correct count |
| `si` | uint32 | incorrect count |
| `b_mbps` | uint32 | bit rate in milli-bits per second |

### `END`
The authoritative end-of-run tally, computed on-device. This is the number that gets
reported. The host reconciles its own display counters against it and, on any
mismatch, shows the device's figures and logs the discrepancy.

| Key | Type | Meaning |
|---|---|---|
| `n` | uint | alphabet size used |
| `sc` | uint32 | **Sc** — correct selections |
| `si` | uint32 | **Si** — incorrect selections |
| `t_us` | uint64 | **t** — scored window length, microseconds |
| `b_mbps` | uint32 | **B** in milli-bits/second |
| `reason` | token | `COMPLETE` \| `ABORT` |
| `mode` | token | `EVAL` \| `PRACTICE` |
| `seed` | uint32 | RNG seed, for reproduction |
| `draws` | uint32 | targets presented |
| `repeats` | uint32 | consecutive-repeat targets |
| `max_streak` | uint32 | best streak |
| `min_us` | uint32 | fastest reaction |
| `p50_us`, `p95_us`, `p99_us` | uint32 | reaction-time percentiles, nearest-rank |

### `HIST`
The per-cell target histogram, sent immediately after `END`. Separate from `END`
because 25 counts plus the tally would exceed `kMaxLineBytes`.

| Key | Type | Meaning |
|---|---|---|
| `off` | uint | index of the first cell in this chunk |
| `v` | csv | comma-separated counts, no spaces |

Chunked so each line stays within the limit. The host concatenates chunks by `off`.

### `LOG`
Diagnostics. Never affects scoring; a host may discard these entirely.

| Key | Type | Meaning |
|---|---|---|
| `level` | `I`/`W`/`E` | severity |
| `msg` | token | message with spaces replaced by `_` |

---

## 4. Host → device: `CMD`

```
CMD <NAME> [key=value ...] <crc16>
```

The device validates the CRC before acting. A command with a bad CRC is discarded and
answered with a `LOG level=W` — never acted on partially.

| Name | Args | Effect |
|---|---|---|
| `START` | `mode=EVAL\|PRACTICE` | begin a run; ignored unless the device is `IDLE` |
| `ABORT` | — | end the current run with `reason=ABORT` |
| `SELFTEST` | `force=0\|1` | run the self-test; `force=1` re-tests cells already marked dead |
| `PING` | — | device replies `HELLO` |
| `BRIGHT` | `pct=0..100` | set global LED brightness cap |
| `PROTO` | — | device replies `HELLO` (protocol discovery) |

Note there is no command to set `N`, to choose a seed, or to select a target. That is
not an oversight — see §6.

---

## 5. Error handling

| Condition | Device behaviour | Host behaviour |
|---|---|---|
| line longer than `kMaxLineBytes` | drop, emit `LOG level=W` | drop, count as a framing error, surface in UI |
| bad CRC | drop, emit `LOG level=W` | drop, count, surface in UI |
| unknown `TYPE` | n/a | ignore the event but keep the `seq` accounting |
| unknown `CMD` | drop, emit `LOG level=W` | n/a |
| `seq` gap | n/a | surface a visible warning; do not attempt to interpolate |
| unparseable field | drop the whole message | drop the whole message |

Partial application is never allowed in either direction. A message either parses
completely and is acted on, or it is discarded whole.

---

## 6. Integrity: the host cannot influence the sequence

This matters because the score depends on the target sequence being genuinely i.i.d.
and unpredictable, and because a reviewer should be able to confirm that from the
protocol alone rather than by trusting the firmware.

- There is **no command that supplies, seeds, or biases the RNG**. The seed comes from
  the RP2040 ring-oscillator random bit register, on-device, at run start.
- There is **no command that sets a target**. `TARGET` is device → host only.
- There is **no command that adjusts `N`**, `Sc`, `Si`, or the clock. `N` is fixed at
  25 and reduced only by the device's own self-test.
- The device emits the seed in `MODE` and `END` *after* the run is under way, so it is
  auditable and reproducible without being controllable.
- All timestamps originate from the device's hardware timer. The host never supplies a
  time value in any command.

The strongest statement available is structural: the command set in §4 has no argument
that reaches the sampler.

---

## 7. Worked example

A short `EVAL` run, three presses, one of them wrong:

```
EV 1 118293 HELLO proto=1 fw=1.0.0 board=gridpulse-5x5 n=25 pins_ok=1 4F2A
EV 2 5012994 MODE mode=EVAL state=COUNTDOWN seed=3735928559 n=25 91C7
EV 3 8013001 MODE mode=EVAL state=RUNNING seed=3735928559 n=25 2B6E
EV 4 8013042 TARGET cell=13 idx=1 repeat=0 7D10
EV 5 8231688 HIT cell=13 rt_us=218646 sc=1 si=0 streak=1 A305
EV 6 8231701 TARGET cell=2 idx=2 repeat=0 C48B
EV 7 8462203 MISS pressed=7 target=2 sc=1 si=1 0E9F
EV 8 8598114 HIT cell=2 rt_us=366413 sc=2 si=1 streak=1 55D2
EV 9 8598126 TARGET cell=2 idx=3 repeat=1 B771
EV 10 8763001 TICK t_run_us=749959 sc=2 si=1 b_mbps=6113 1A44
...
EV 812 68013042 END n=25 sc=241 si=12 t_us=60000000 b_mbps=17494 reason=COMPLETE mode=EVAL seed=3735928559 draws=242 repeats=9 max_streak=38 min_us=141002 p50_us=203118 p95_us=310447 p99_us=402881 6C39
EV 813 68013055 HIST off=0 v=9,11,8,10,9,12,7,10,11,9,8,10,13,9,7 D2E0
EV 814 68013061 HIST off=15 v=11,8,9,10,12,8,9,11,10,10 91B5
```

Note event 9: `cell=2` twice in a row with `repeat=1`. That is the sampler working
correctly, not a stuck key.

---

## 8. Versioning

`HELLO.proto` carries the version. A host that sees a `proto` it does not recognise
must refuse to score a run and say so plainly, rather than guessing at field meanings.
Any change to an existing message's field set requires a version bump; adding a wholly
new `TYPE` does not, because unknown types are already required to be ignored.
