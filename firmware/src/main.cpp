// GRID PULSE - core 0: USB, framing, and nothing else.
//
// PURPOSE
//   Core 0 runs TinyUSB, turns queued events into protocol lines, and turns received
//   lines into validated commands. It never touches the game.
//
//   That separation is the whole architecture. USB has unbounded, host-controlled
//   timing: a host that stops reading, a terminal that disconnects, a browser that
//   stalls. None of it can reach core 1, so none of it can affect what the device
//   measures. See docs/ARCHITECTURE.md.
//
// BACKPRESSURE POLICY
//   If the host stops draining the CDC endpoint, the transmit FIFO fills. When that
//   happens core 0 drops PRESENTATION events - ticks, logs, histogram chunks - and
//   never drops SCORING events. A dropped tick makes the live display briefly stale;
//   a dropped HIT would make it wrong. The device's own END tally is computed on core
//   1 from state the host never touches, so the reported score is correct regardless
//   of what happens on this side.
//
// ALLOCATION
//   None. One line buffer in, one line buffer out, both fixed size.

#include <cstddef>
#include <cstdint>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"

#include "core1_game.h"
#include "pure/config.h"
#include "pure/event.h"
#include "pure/protocol.h"

namespace gridpulse {
namespace {

// Monotonic from boot. A gap tells the host it lost bytes, which it must surface
// rather than silently mis-displaying.
std::uint32_t g_sequence = 0;

// Set from the USB interrupt when the host asserts DTR, i.e. when something opens
// the port. The main loop notices and asks core 1 to announce itself.
//
// A flag rather than a queue push, because the callback runs in interrupt context on
// this same core: pushing there could preempt the main loop mid-Push and break the
// single-producer assumption the lock-free ring buffer is built on.
volatile bool g_host_attached = false;
bool g_hello_sent_for_this_connection = false;

// Inbound line assembly.
char g_rx_line[kMaxLineBytes];
std::size_t g_rx_length = 0;
bool g_rx_overflow = false;

// One outbound line, held across iterations when the FIFO is too full to take it.
char g_tx_line[kMaxLineBytes];
std::size_t g_tx_length = 0;
std::size_t g_tx_sent = 0;

// A pending END whose histogram still has chunks to emit.
RunReport g_pending_histogram;
std::size_t g_histogram_offset = kCellCount;
std::uint64_t g_histogram_t_us = 0;

// Whether an event may be discarded under backpressure. Scoring events may not.
bool IsDroppable(EventType type) {
  switch (type) {
    case EventType::kTick:
    case EventType::kLog:
    case EventType::kHist:
      return true;
    case EventType::kHello:
    case EventType::kMode:
    case EventType::kSelfTest:
    case EventType::kTarget:
    case EventType::kHit:
    case EventType::kMiss:
    case EventType::kEnd:
      return false;
  }
  return false;
}

// Pushes whatever is in the outbound buffer that the FIFO will take.
// Returns true when the buffer has been fully handed over.
bool FlushOutbound() {
  if (g_tx_length == 0) {
    return true;
  }
  if (!tud_cdc_connected()) {
    // Nothing is listening. Discard rather than accumulate: the device's own tally
    // is unaffected, and holding a line forever would stall every event behind it.
    g_tx_length = 0;
    g_tx_sent = 0;
    return true;
  }

  const std::size_t available = tud_cdc_write_available();
  if (available == 0) {
    return false;
  }

  std::size_t remaining = g_tx_length - g_tx_sent;
  if (remaining > available) {
    remaining = available;
  }
  g_tx_sent += tud_cdc_write(g_tx_line + g_tx_sent, static_cast<uint32_t>(remaining));

  if (g_tx_sent >= g_tx_length) {
    tud_cdc_write_flush();
    g_tx_length = 0;
    g_tx_sent = 0;
    return true;
  }
  tud_cdc_write_flush();
  return false;
}

// Emits the next histogram chunk, if one is outstanding.
bool PumpHistogram() {
  if (g_histogram_offset >= kCellCount) {
    return false;
  }
  const std::size_t written =
      FormatHistogramChunk(g_pending_histogram, g_sequence, g_histogram_t_us,
                           &g_histogram_offset, g_tx_line, sizeof(g_tx_line));
  if (written == 0) {
    g_histogram_offset = kCellCount;
    return false;
  }
  ++g_sequence;
  g_tx_length = written;
  g_tx_sent = 0;
  return true;
}

void PumpOutbound() {
  if (!FlushOutbound()) {
    return;  // still handing over the previous line
  }

  if (PumpHistogram()) {
    FlushOutbound();
    return;
  }

  Event event;
  if (!g_event_queue.Pop(&event)) {
    return;
  }

  // Under backpressure, presentation events are discarded so scoring events keep
  // moving. The threshold is one full line: if the FIFO cannot take a whole line,
  // it is congested.
  const bool congested = tud_cdc_connected() && tud_cdc_write_available() < kMaxLineBytes;
  if (congested && IsDroppable(event.type)) {
    return;
  }

  if (event.type == EventType::kHist) {
    g_pending_histogram = event.report;
    g_histogram_offset = 0;
    g_histogram_t_us = event.t_us;
    if (PumpHistogram()) {
      FlushOutbound();
    }
    return;
  }

  const std::size_t written =
      FormatEvent(event, g_sequence, g_tx_line, sizeof(g_tx_line));
  if (written == 0) {
    // kMaxLineBytes is derived from the worst case, so this is a bug rather than a
    // runtime condition. Do not advance the sequence number for a line never sent.
    return;
  }
  ++g_sequence;
  g_tx_length = written;
  g_tx_sent = 0;
  FlushOutbound();
}

// Hands a completed inbound line to the command parser.
void HandleInboundLine() {
  if (g_rx_overflow) {
    // A line longer than kMaxLineBytes is a framing error. Dropped whole, never
    // truncated: a truncated line could carry a plausible CRC over its own prefix.
    g_event_queue.Push(MakeLog(time_us_64(), LogLevel::kWarn, "rx_line_too_long"));
    g_rx_overflow = false;
    g_rx_length = 0;
    return;
  }
  if (g_rx_length == 0) {
    return;
  }

  Command command;
  const ParseError error = ParseCommand(g_rx_line, g_rx_length, &command);
  if (error == ParseError::kOk) {
    if (!g_command_queue.Push(command)) {
      g_event_queue.Push(MakeLog(time_us_64(), LogLevel::kWarn, "command_queue_full"));
    }
  } else {
    // Rejected whole. Nothing is acted on partially.
    g_event_queue.Push(MakeLog(time_us_64(), LogLevel::kWarn, ParseErrorText(error)));
  }
  g_rx_length = 0;
}

// Announces the device when a host attaches.
//
// Without this a freshly-booted board is completely silent: HELLO is emitted at boot,
// when nothing is listening yet and it is therefore discarded, and an IDLE device
// produces no other traffic. Anyone opening the port would see nothing at all and
// reasonably conclude the board was dead. docs/PROTOCOL.md section 3 specifies HELLO
// "on connect", and this is what makes that true.
void PumpConnectionState() {
  const bool attached = g_host_attached;
  if (attached && !g_hello_sent_for_this_connection) {
    g_hello_sent_for_this_connection = true;
    // Routed through core 1 because it owns the alphabet size and the health mask
    // that HELLO reports.
    Command ping;
    ping.name = CommandName::kPing;
    g_command_queue.Push(ping);
  } else if (!attached) {
    if (g_hello_sent_for_this_connection) {
      // The host just closed the port. Tell core 1, which will stop a self-test walk
      // and leave a run in progress untouched.
      //
      // A walk with nobody watching is destructive rather than merely useless: five
      // seconds per cell, two passes, then the result is written to flash - so an
      // abandoned walk condemns the whole board in about four minutes and the device
      // afterwards refuses to start a run. Guarding the start of a walk was never
      // enough on its own, because the operator can leave in the middle of one.
      Command abort;
      abort.name = CommandName::kAbort;
      abort.host_detached = true;
      g_command_queue.Push(abort);
    }
    g_hello_sent_for_this_connection = false;
  }
}

void PumpInbound() {
  while (tud_cdc_available() > 0) {
    const int byte = tud_cdc_read_char();
    if (byte < 0) {
      break;
    }
    const char c = static_cast<char>(byte);
    if (c == '\n') {
      HandleInboundLine();
      continue;
    }
    if (g_rx_length + 1 >= sizeof(g_rx_line)) {
      // Keep consuming to the newline so the next line is not corrupted by this
      // one's tail.
      g_rx_overflow = true;
      continue;
    }
    g_rx_line[g_rx_length++] = c;
  }
}

}  // namespace
}  // namespace gridpulse

// TinyUSB calls this from interrupt context when the host changes the control line
// state. DTR asserted means a program has opened the port.
extern "C" void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
  (void)itf;
  (void)rts;
  gridpulse::g_host_attached = dtr;
}

int main() {
  // The whole game - key scanning, target selection, scoring, LEDs - runs on core 1.
  // Core 0 does USB and nothing else, so no amount of host misbehaviour can add
  // latency to a keypress.
  multicore_launch_core1(gridpulse::Core1Main);

  tusb_init();

  for (;;) {
    tud_task();
    gridpulse::PumpConnectionState();
    gridpulse::PumpInbound();
    gridpulse::PumpOutbound();
  }
}
