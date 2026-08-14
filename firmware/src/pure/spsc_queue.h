// GRID PULSE - a lock-free single-producer/single-consumer ring buffer.
//
// PURPOSE
//   Carries game events from core 1 (key scanning, game logic, LEDs) to core 0
//   (TinyUSB CDC). Core 1 must never block: a stall there delays a key press, which
//   lands directly on the player's measured reaction time and therefore on the
//   score.
//
// WHY NOT queue.h FROM THE PICO SDK
//   The SDK's queue_t is correct and multicore-safe, but it takes a hardware spinlock
//   on both push and pop. That means the producer's worst-case latency depends on the
//   consumer, and core 0 is the core that talks to USB - the one thing in this system
//   with unbounded, host-controlled timing. A single-producer/single-consumer ring
//   needs no lock at all: with one writer of `head` and one writer of `tail`, an
//   acquire/release pair is sufficient, and the producer's push is wait-free.
//
//   The RP2040's Cortex-M0+ implements ARMv6-M, which has DMB and lock-free 32-bit
//   atomic load/store. std::atomic<uint32_t> compiles to plain loads and stores plus
//   barriers here - no library calls, no allocation.
//
// INVARIANTS
//   - Exactly one thread/core calls Push, exactly one calls Pop. Violating this
//     breaks the algorithm; it is not defensive against misuse.
//   - Capacity is a power of two, so the wrap is a mask rather than a modulo.
//   - One slot is always left empty to distinguish full from empty, so a queue of
//     Capacity holds Capacity - 1 items.
//   - Push never blocks, never allocates, and never overwrites unread data. When the
//     queue is full it returns false and the caller decides what to drop; the
//     firmware drops presentation frames and never scoring events.
//
// No Pico SDK dependency: this is header-only and host-testable.

#ifndef GRIDPULSE_SPSC_QUEUE_H_
#define GRIDPULSE_SPSC_QUEUE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gridpulse {

template <typename T, std::size_t Capacity>
class SpscQueue {
  static_assert(Capacity >= 2, "capacity must leave room for the empty slot");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "capacity must be a power of two so the wrap is a mask");

 public:
  // constexpr so the queue is CONSTANT-initialised: no dynamic initialiser runs at
  // startup, and the object's contents are fixed at link time. If a future edit adds
  // a member that is not constant-initialisable, this line stops compiling - which
  // is the point.
  //
  // Note this does NOT put the object in .bss. A queue of Events lands in .data
  // because Event's defaults are not all-zero (its `type` defaults to kLog so an
  // uninitialised event cannot be mistaken for a HELLO). That costs the image about
  // 19 kB of flash out of 2 MB and roughly 100 us of memcpy at boot, which is a
  // better trade than making an uninitialised event look like a valid one.
  constexpr SpscQueue() = default;

  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  // Producer side. Returns false if the queue is full; the item is not stored.
  bool Push(const T& item) {
    const std::uint32_t head = head_.load(std::memory_order_relaxed);
    const std::uint32_t next = (head + 1) & kMask;
    // Acquire so that the slot write below cannot be reordered before the consumer's
    // release of the slot it just freed.
    if (next == tail_.load(std::memory_order_acquire)) {
      ++dropped_;
      return false;
    }
    slots_[head] = item;
    // Release so the item is fully visible to the consumer before the index moves.
    head_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side. Returns false if the queue is empty; `out` is untouched.
  bool Pop(T* out) {
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    *out = slots_[tail];
    tail_.store((tail + 1) & kMask, std::memory_order_release);
    return true;
  }

  bool Empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  std::size_t Size() const {
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    const std::uint32_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & kMask;
  }

  static constexpr std::size_t MaxSize() { return Capacity - 1; }

  // How many pushes have been refused since boot. Surfaced on the wire so a host can
  // tell "the player stopped pressing keys" from "the link fell behind".
  std::uint32_t dropped() const { return dropped_; }

  void Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    dropped_ = 0;
  }

 private:
  static constexpr std::uint32_t kMask = static_cast<std::uint32_t>(Capacity - 1);

  T slots_[Capacity] = {};
  std::atomic<std::uint32_t> head_{0};
  std::atomic<std::uint32_t> tail_{0};
  // Written only by the producer, read for diagnostics; exactness under a concurrent
  // read is not required.
  std::uint32_t dropped_ = 0;
};

}  // namespace gridpulse

#endif  // GRIDPULSE_SPSC_QUEUE_H_
