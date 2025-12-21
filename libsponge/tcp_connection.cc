#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    // 当前停止连接
    if (!_is_active) {
        return;
    }
    _time_since_last_segment_received = 0;

    // you code here.
    //你需要考虑到ACK包、RST包等多种情况

    // closed, listen
    if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0) {
        if (!seg.header().syn) return;
        // 收到一个SYN，准备建立连接，返回一个SYN+ACK
        _receiver.segment_received(seg);
        connect();
        return;
    }

    // syn sent
    if (_sender.next_seqno_absolute() > 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute() &&
    !_receiver.ackno().has_value()) {
        // 收到SYN + ACK进入establish
        if (!seg.header().ack) {
            // 对方同时发送了一个syn
            if (seg.header().syn) {
                _receiver.segment_received(seg);
                _sender.send_empty_segment();
            }
            // 非ack,syn无效
            return;
        }
        if (seg.header().rst) {
            // 暴力退出
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _is_active = false;
            return;
        }
    }

    // establish
    // 交给_sender和_receiver处理
    _receiver.segment_received(seg);
    _sender.ack_received(seg.header().ackno, seg.header().win);

    // 有效包，至少回复一个空白ack
    if (seg.length_in_sequence_space() > 0 && _sender.segments_out().empty()) {
        _sender.send_empty_segment();
    }
    
    // keep-alive: length=0 且 seqno == ackno - 1，也要回 ACK
    if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0) {
        const WrappingInt32 expected = _receiver.ackno().value() - 1;
        if (seg.header().seqno == expected && _sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }
    // RST 包，立即终止
    if (seg.header().rst) {
        _sender.send_empty_segment();
        unclean_shutdown();
        return;
    }
    send_segments_from_sender();
}

// 将sender的内部seg转换为完整seg
void TCPConnection::send_segments_from_sender() {
    while (!_sender.segments_out().empty()) {
        // 遍历sender segment列表
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();

        // 检查ackno，增加ACK信息
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            // size_t转换到uint_16防止溢出
            const size_t win = _receiver.window_size();
            seg.header().win = win > std::numeric_limits<uint16_t>::max()
                     ? std::numeric_limits<uint16_t>::max()
                     : static_cast<uint16_t>(win);
        }

        // 发送
        _segments_out.push(seg);
    }
    clean_shutdown();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    if (!data.size()) return 0;
    // 将data放入sender组装
    size_t written = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments_from_sender();
    // 返回成功组装的字符数
    return written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_is_active) return;
    // 更新计时器
    _sender.tick(ms_since_last_tick);
    _time_since_last_segment_received += ms_since_last_tick;

    // 连续重传次数超过上限，终止连接，发送RST
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // 暴力退出
        unclean_shutdown();
    }

    // 重传
    send_segments_from_sender();
}

void TCPConnection::end_input_stream() {
    // 通知sender结束
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segments_from_sender();
}

void TCPConnection::connect() {
    // 发送SYN
    _sender.fill_window();
    send_segments_from_sender();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _sender.send_empty_segment();
            unclean_shutdown();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
void TCPConnection::unclean_shutdown() {
    if (!_is_active) return;
    // 出站流还没完全结束发送时，调用以暴力终止连接
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _is_active = false;
    
    // 构造一个rst段并发送
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    seg.header().ack = true;
    if (_receiver.ackno().has_value())
        seg.header().ackno = _receiver.ackno().value();
    seg.header().win = _receiver.window_size();
    seg.header().rst = true;
    _segments_out.push(seg);
}

void TCPConnection::clean_shutdown() {
    // 完全发送，自然终止连接
    if (_receiver.stream_out().input_ended()) {
        if (!_sender.stream_in().eof())
            // 入站流结束，出站流还在发送
            _linger_after_streams_finish = false;
        else if (_sender.bytes_in_flight() == 0) {
            // 确认全部接受
            if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
                _is_active = false;
            }
        }
    }
}