#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // 解析header
    const TCPHeader &hdr = seg.header();
    const std::string payload = seg.payload().copy();

    if (!_isn.has_value()) {    // Listen状态
        if(!hdr.syn) {
            // 传输未开始，丢弃
            return;
        }
        // 设置_isn
        _isn = hdr.seqno;
        _checkpoint = 0;
    }
    // 计算绝对序列号
    uint64_t abs_seq = unwrap(hdr.seqno, _isn.value(), _checkpoint);
    uint64_t index = abs_seq;
    // 去除SYN
    if (hdr.syn) {
        index = 0;
    } else {
        index -= 1;
    }
    _reassembler.push_substring(payload, index, hdr.fin);
    // 更新checkpoint
    _checkpoint = abs_seq + seg.length_in_sequence_space();
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (!_isn.has_value()) {
        // 没有收到SYN设置isn, Listen状态
        return {};
    }
    // 获取序列号
    uint64_t abs_index = _reassembler.stream_out().bytes_written();
    uint64_t ack_abs = 1 + abs_index; // + SYN
    // 如果收到FIN, + FIN
    if (_reassembler.stream_out().input_ended()) {
        ack_abs += 1;
    }
    // 返回序列号
    return wrap(ack_abs, _isn.value());
}

size_t TCPReceiver::window_size() const {
    size_t t = _reassembler.stream_out().buffer_size();
    if (t >= _capacity) return 0;
    return _capacity - t;
}
