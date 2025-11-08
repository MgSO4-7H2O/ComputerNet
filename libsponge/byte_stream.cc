#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : 
    _buffer(capacity),
    _capacity(capacity),
    _front(0),
    _rear(0),
    _size(0),
    _written_bytes(0),
    _read_bytes(0),
    _input_ended(0),
    _error(false)   
    { 
}

size_t ByteStream::write(const string &data) {
    // 计算可写字符数
    size_t can_input = _capacity - _size;
    size_t to_write = min(can_input, data.size());
    if (to_write == 0) {
        return 0;
    }
    
    // 计算循环写入的分段
    size_t to_tail = min(_capacity - _rear, to_write);
    size_t to_front = max(to_write - to_tail, 0UL);
    
    // 写vector后段
    for (size_t i = 0UL; i < to_tail; i++) {
        _buffer[_rear + i] = data[i];
    }
    // 更新rear flag
    _rear = (_rear + to_tail) % _capacity;

    // 写vector前段
    if (to_front > 0UL) {
        for (size_t i = 0UL; i < to_front; i++) {
            _buffer[i] = data[to_tail + i];
        }
        _rear += to_front;
    }

    // 更新计数器
    _size += to_write;
    _written_bytes += to_write;
    return to_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t to_peek = min(_size, len);
    string res;
    res.reserve(to_peek);

    size_t first = min(to_peek, _capacity - _front);
    res.append(_buffer.begin() + _front, _buffer.begin() + _front + first);

    if (first < to_peek) {
        size_t second = to_peek - first;
        res.append(_buffer.begin(), _buffer.begin() + second);
    }
    return res;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t to_pop = min(_size, len);
    _front = (_front + to_pop) % _capacity;
    _size -= to_pop;
    _read_bytes += to_pop;
    return;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string res = peek_output(len);
    pop_output(len);
    return res;
}

void ByteStream::end_input() {
    this->_input_ended = true;
    return;
}

bool ByteStream::input_ended() const { return this->_input_ended; }

size_t ByteStream::buffer_size() const { return this->_size; }

bool ByteStream::buffer_empty() const { return (this->_size == 0); }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return this->_written_bytes; }

size_t ByteStream::bytes_read() const { return this->_read_bytes; }

size_t ByteStream::remaining_capacity() const { return this->_capacity - this->_size; }
