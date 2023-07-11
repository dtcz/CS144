#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();
    if (header.syn) {
        _isn = header.seqno;
        _ackno = _isn + 1;
        if (seg.payload().size()) {
            size_t curr = _reassembler.stream_out().bytes_written();
            _reassembler.push_substring(seg.payload().copy(), header.seqno - _isn, header.fin);
            _ackno = _ackno.value() + _reassembler.stream_out().bytes_written() - curr;
        }
    } else if (_ackno.has_value() && seg.payload().size()) {
        size_t curr = _reassembler.stream_out().bytes_written();
        _reassembler.push_substring(seg.payload().copy(), header.seqno - _isn - 1, header.fin);
        _ackno = _ackno.value() + _reassembler.stream_out().bytes_written() - curr;
        if (_reassembler.stream_out().input_ended() && !header.fin) {
            _ackno = _ackno.value() + 1;
        }
    }
    if (_ackno.has_value() && header.fin) {
        if (unassembled_bytes() == 0) {
            _ackno = _ackno.value() + 1;
            _reassembler.stream_out().end_input();
        }
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ackno; }

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
