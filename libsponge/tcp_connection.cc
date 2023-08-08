#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

TCPSegment sendAck(TCPReceiver &_receiver, TCPSender &_sender) {
    TCPSegment ack;
    ack.header().seqno = _sender.next_seqno();
    if (_receiver.ackno().has_value()) {
        ack.header().ackno = _receiver.ackno().value();
        ack.header().ack = true;
    }
    ack.header().win = _receiver.window_size();
    return ack;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        _linger_after_streams_finish = false;
        _time_since_last_segment_received = 0;
        return;
    }
    if (state() == TCPState{TCPState::State::LISTEN} && seg.header().syn) {
        _receiver.segment_received(seg);
        connect();
        if (seg.payload().size()) {
            _segments_out.push(sendAck(_receiver, _sender));
        }
    } else if (state() == TCPState{TCPState::State::SYN_SENT} && seg.header().syn) {
        _receiver.segment_received(seg);
        if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
        }
        if (state() == TCPState{TCPState::State::ESTABLISHED} || state() == TCPState{TCPState::State::SYN_RCVD} ||
            seg.length_in_sequence_space() > 0) {
            _segments_out.push(sendAck(_receiver, _sender));
        }
    } else if (state() == TCPState{TCPState::State::SYN_RCVD}) {
        _receiver.segment_received(seg);
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (seg.payload().size()) {
            _segments_out.push(sendAck(_receiver, _sender));
        }
    } else if (state() == TCPState{TCPState::State::ESTABLISHED}) {
        if (seg.header().fin) {
            _receiver.segment_received(seg);
            _sender.ack_received(seg.header().ackno, seg.header().win);
            _linger_after_streams_finish = false;
            _segments_out.push(sendAck(_receiver, _sender));
        } else if (seg.length_in_sequence_space() > 0) {
            _receiver.segment_received(seg);
            if (seg.header().ack) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
            }
            _segments_out.push(sendAck(_receiver, _sender));
        } else if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
        }
    } else if (state() == TCPState{TCPState::State::LAST_ACK}) {
        _receiver.segment_received(seg);
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (seg.length_in_sequence_space()) {
            _segments_out.push(sendAck(_receiver, _sender));
        }
        if (state() != TCPState{TCPState::State::LAST_ACK}) {
            _active = false;
        }
    } else if (state() == TCPState{TCPState::State::FIN_WAIT_1} || state() == TCPState{TCPState::State::CLOSING} ||
               state() == TCPState{TCPState::State::FIN_WAIT_2}) {
        _receiver.segment_received(seg);
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (seg.length_in_sequence_space()) {
            _segments_out.push(sendAck(_receiver, _sender));
        }
    } else if (state() == TCPState{TCPState::State::TIME_WAIT} || state() == TCPState{TCPState::State::CLOSE_WAIT}) {
        if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
        }
        if (seg.length_in_sequence_space() > 0) {
            if (seg.payload().size()) {
                _receiver.segment_received(seg);
            }
            _segments_out.push(sendAck(_receiver, _sender));
        }
    } else {
        if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
        }
        if (seg.length_in_sequence_space() > 0) {
            _receiver.segment_received(seg);
            _segments_out.push(sendAck(_receiver, _sender));
        }
    }
    if (state() == TCPState(TCPState::State::TIME_WAIT)) {
        _rt_timeout = _cfg.rt_timeout * 10;
    }
    _time_since_last_segment_received = 0;
    if (state() != TCPState{TCPState::State::LISTEN}) {
        _sender.fill_window();
        while (not _sender.segments_out().empty()) {
            TCPSegment data = _sender.segments_out().front();
            data.header().win = _receiver.window_size();
            if (_receiver.ackno().has_value()) {
                data.header().ack = true;
                data.header().ackno = _receiver.ackno().value();
            }
            _segments_out.push(data);
            _sender.segments_out().pop();
        }
    }
    if (_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0) and
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
        _segments_out.push(_sender.segments_out().front());
        _sender.segments_out().pop();
    }
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t len = data.size();
    int times = 0;
    while (times * _cfg.send_capacity < len) {
        _sender.stream_in().write(data.substr(times * _cfg.send_capacity, _cfg.send_capacity));
        _sender.fill_window();
        while (not _sender.segments_out().empty()) {
            TCPSegment seg = _sender.segments_out().front();
            seg.header().win = _receiver.window_size();
            if (_receiver.ackno().has_value()) {
                seg.header().ack = true;
                seg.header().ackno = _receiver.ackno().value();
            }
            _segments_out.push(seg);
            _sender.segments_out().pop();
        }
        times++;
    }
    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // printf("%s\n", state().name().c_str());
    // printf("%s\n", TCPState{TCPState::State::TIME_WAIT}.name().c_str());
    _time_since_last_segment_received += ms_since_last_tick;
    if (state() == TCPState(TCPState::State::TIME_WAIT)) {
        if (_rt_timeout <= ms_since_last_tick) {
            _active = false;
            _linger_after_streams_finish = false;
        } else {
            _rt_timeout -= ms_since_last_tick;
        }
    } else {
        _sender.tick(ms_since_last_tick);
        // _sender.fill_window();
        // printf("%ld", _sender.segments_out().size());
        if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _active = false;
            _linger_after_streams_finish = false;
            _time_since_last_segment_received = 0;
            TCPSegment s;
            s.header().rst = true;
            _segments_out.push(s);
            return;
        }
        while (not _sender.segments_out().empty()) {
            TCPSegment seg = _sender.segments_out().front();
            seg.header().win = _receiver.window_size();
            if (_receiver.ackno().has_value()) {
                seg.header().ack = true;
                seg.header().ackno = _receiver.ackno().value();
            }
            _segments_out.push(seg);
            _sender.segments_out().pop();
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    while (not _sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        if (seg.header().fin && _receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
        }
        seg.header().win = _receiver.window_size();
        _segments_out.push(seg);
        _sender.segments_out().pop();
    }
}

void TCPConnection::connect() {
    _sender.fill_window();
    while (not _sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        if (seg.header().syn && state() == TCPState{TCPState::State::SYN_RCVD} && _receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
        }
        seg.header().win = _receiver.window_size();
        _segments_out.push(seg);
        _sender.segments_out().pop();
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _receiver.stream_out().end_input();
            TCPSegment s;
            s.header().rst = true;
            _segments_out.push(s);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
