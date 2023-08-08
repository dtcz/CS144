#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _data(capacity + 1), _readed(0), _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // int _updated = max(_readed, index);
    size_t j;
    if (_readed >= index) {
        j = 0;
        for (size_t i = _readed - index; i < data.length() && j < _capacity; i++) {
            _data[j++] = data[i];
        }
    } else {
        j = index - _readed;
        for (size_t i = 0; i < data.length() && j < _capacity; i++) {
            _data[j++] = data[i];
        }
    }

    if (eof) {
        _data[j] = "EOF";
    }
    string s = "";
    while (!_data.empty() && _data.front() != "") {
        if (_data.front() == "EOF") {
            if (s.size()) {
                _readed += _output.write(s);
            }
            _output.end_input();
            break;
        }
        if (_output.remaining_capacity() - s.size() == 0) {
            break;
        }
        s += _data.front();
        _data.pop_front();
    }
    if (s.size() && _data.front() != "EOF") {
        _readed += _output.write(s);
    }
    _data.resize(_capacity + 1);
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t i = 0;
    size_t count = 0;
    for (; i < _data.size(); i++) {
        if (_data[i] == "EOF") {
            break;
        }
        if (_data[i] != "") {
            count++;
        }
    }
    return count;
}

bool StreamReassembler::empty() const { return _output.input_ended(); }
