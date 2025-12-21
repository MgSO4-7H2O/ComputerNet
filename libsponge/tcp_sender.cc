#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) 
    , _cur_rto(retx_timeout){}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    // 检测远程窗口大小。窗口为0视为1
    uint64_t window = (_receiver_window_size == 0)? 1 : _receiver_window_size;

    while (true) {
        // 判断是否还有剩余窗口
        if (_bytes_in_flight >= window) {
            return;  // 窗口已经被占满，不能再发
        }

        uint64_t remaining = window - _bytes_in_flight;

        TCPSegment seg;
        TCPHeader &hdr = seg.header();

        // 如果尚未发送SYN数据包，设置header并准备发送
        if (!_syn_sent) {
            hdr.syn = true;
            hdr.seqno = wrap(_next_seqno, _isn);
            _syn_sent = true;
            _next_seqno += 1;
            remaining -= 1;
        } else {
            hdr.seqno = wrap(_next_seqno, _isn);
        }

        // 计算payload余量
        size_t max_payload = TCPConfig::MAX_PAYLOAD_SIZE;
        size_t to_send = min<size_t>(remaining, max_payload);

        string data = _stream.read(to_send);
        seg.payload() = Buffer(std::move(data));
        remaining -= seg.payload().size();
        _next_seqno += seg.payload().size();

        // 从来没发送过 FIN 且 输入字节流处于 EOF 且 可存放下 FIN
        bool can_send_fin = !_fin_sent
                        && _stream.eof()
                        && (remaining > 0);
        if (can_send_fin) {
            hdr.fin = true;
            _fin_sent = true;
            _next_seqno += 1;
            remaining -= 1;
        }

        // 没有数据，停止数据包发送
        if (!hdr.syn && !hdr.fin && seg.payload().size() == 0) {
            break;
        }

        // 放入outstanding列表
        _outstanding.push_back(seg);
        _bytes_in_flight += seg.length_in_sequence_space();

        // 如果没有正在等待的数据包，则重设更新时间
        if (!_timer_running) {
            _timer_running = true;
            _time_since_last_tick = 0;
            _cur_rto = _initial_retransmission_timeout;
        }

        // 输出
        _segments_out.push(seg);

        // FIN输出后，停止发送
        if (hdr.fin) break;
    }
    
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ack = unwrap(ackno, _isn, _next_seqno);
    // 无效ack, 丢弃
    if (abs_ack > _next_seqno) {
        return;
    }
    // 更新窗口大小
    _receiver_window_size = window_size;

    // 遍历outstanding寻找ACK段
    bool new_ack = false;

    while (!_outstanding.empty()) {
        TCPSegment &front = _outstanding.front();
        uint64_t seg_start = unwrap(front.header().seqno, _isn, abs_ack);
        uint64_t seg_end = seg_start + front.length_in_sequence_space();

        if (seg_end <= abs_ack) {
            // ACK，移除
            _bytes_in_flight -= front.length_in_sequence_space();
            _outstanding.pop_front();
            new_ack = true;
        } else {
            break;
        }
    }
    // 收到ACK，重置重传计时器、RTO
    if (new_ack) {
        _consecutive_retransmissions = 0;
        _cur_rto = _initial_retransmission_timeout;
        _time_since_last_tick = 0;

        // 全部ACK，停止计时器
        if (_outstanding.empty()) {
            _timer_running = false;
        } else {
            _timer_running = true;
        }
    }

    // 继续fill
    fill_window();

}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_running || _outstanding.empty()) {
        // 没有发送中的数据包
        return;
    }
    _time_since_last_tick += ms_since_last_tick;

    // 存在超时
    if (_time_since_last_tick >= _cur_rto) {
        // 重传第一个outstanding段
        _segments_out.push(_outstanding.front());
        _consecutive_retransmissions++;

        // 对方接收窗口大小不为0，指数退避
        if (_receiver_window_size > 0) {
            _cur_rto *= 2;
        }

        // 重置计时器
        _time_since_last_tick = 0;
    }

}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.push(seg);
}
