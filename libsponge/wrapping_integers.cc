#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint64_t seqno;
    seqno = isn.raw_value() + static_cast<uint32_t>(n);
    return WrappingInt32(seqno);
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t offset = static_cast<uint32_t>(n - isn);
    const uint64_t round = 1ull << 32;
    // 寻找距离checkpoint最近的绝对序列号
    uint64_t base = (checkpoint / round) * round;
    // 三个候选绝对序列号
    uint64_t a0 = base + static_cast<uint64_t>(offset);
    uint64_t a1 = a0 + round;
    uint64_t a2 = (a0 >= round)? a0 - round : UINT64_MAX;
    // 选距离最近的
    uint64_t best = a0;
    uint64_t dist0 = (a0 > checkpoint)? a0 - checkpoint : checkpoint - a0;
    uint64_t dist1 = (a1 > checkpoint)? a1 - checkpoint : checkpoint - a1;
    uint64_t dist2 = (a2 > checkpoint)? a2 - checkpoint : checkpoint - a2;
    uint64_t best_dist = dist0;
    if (dist1 < best_dist) {
        best = a1;
        best_dist = dist1;
    }
    if (dist2 < best_dist) {
        best = a2;
        best_dist = dist2;
    }
    return best;
}
