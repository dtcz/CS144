#include "byte_stream.hh"

#include <algorithm>
// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _capacity(capacity), _bytes(), _writtenBytes(0), _readBytes(0), _input_ended(false) {}

size_t ByteStream::write(const string &data) {
    size_t len = min(data.length(), remaining_capacity());
    for (size_t i = 0; i < len; i++) {
        _bytes.push_back(data[i]);
    }
    _writtenBytes += len;
    return len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    return string(_bytes.begin(), _bytes.begin() + min(len, buffer_size()));
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    for (size_t i = 0; i < min(len, buffer_size()); i++) {
        _bytes.pop_front();
    }
    _readBytes += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string s = peek_output(len);
    pop_output(len);
    return s;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _writtenBytes - _readBytes; }

bool ByteStream::buffer_empty() const { return buffer_size() == 0; }

bool ByteStream::eof() const { return buffer_empty() && input_ended(); }

size_t ByteStream::bytes_written() const { return _writtenBytes; }

size_t ByteStream::bytes_read() const { return _readBytes; }

size_t ByteStream::remaining_capacity() const { return _capacity - buffer_size(); }
