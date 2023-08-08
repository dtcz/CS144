#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::send_ack() {
    TCPSegment ack;
    ack.header().seqno = _sender.next_seqno();
    if (_receiver.ackno().has_value()) {
        ack.header().ackno = _receiver.ackno().value();
        ack.header().ack = true;
    }
    ack.header().win = _receiver.window_size();
    _segments_out.push(ack);
}

void TCPConnection::fill_window(std::function<bool(TCPSegment &seg, TCPConnection &c)> ack) {
    _sender.fill_window();
    auto &q = _sender.segments_out();
    while (not q.empty()) {
        TCPSegment &seg = q.front();
        if (_receiver.ackno().has_value() && (ack == nullptr || ack(seg, *this))) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
        }
        seg.header().win = _receiver.window_size();
        _segments_out.push(seg);
        q.pop();
    }
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();
    if (header.rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        _linger_after_streams_finish = false;
        _time_since_last_segment_received = 0;
        return;
    }
    TCPState curr = state();

    if (curr == TCPState{TCPState::State::LISTEN} && header.syn) {
        _receiver.segment_received(seg);
        connect();
    } else if (curr == TCPState{TCPState::State::SYN_SENT} && header.syn) {
        _receiver.segment_received(seg);
        if (header.ack) {
            _sender.ack_received(header.ackno, header.win);
        }
        if (seg.length_in_sequence_space() > 0) {
            send_ack();
        }
    } else if (curr == TCPState{TCPState::State::SYN_RCVD}) {
        _receiver.segment_received(seg);
        _sender.ack_received(header.ackno, header.win);
        if (seg.payload().size()) {
            send_ack();
        }
    } else if (curr == TCPState{TCPState::State::ESTABLISHED}) {
        if (header.fin) {
            _linger_after_streams_finish = false;
        }
        if (header.ack) {
            _sender.ack_received(header.ackno, header.win);
        }
        if (seg.length_in_sequence_space() > 0) {
            _receiver.segment_received(seg);
            send_ack();
        }
    } else if (curr == TCPState{TCPState::State::LAST_ACK}) {
        _receiver.segment_received(seg);
        _sender.ack_received(header.ackno, header.win);
        if (seg.length_in_sequence_space()) {
            send_ack();
        }
        if (state() != TCPState{TCPState::State::LAST_ACK}) {
            _active = false;
        }
    } else if (curr == TCPState{TCPState::State::TIME_WAIT} || curr == TCPState{TCPState::State::CLOSE_WAIT}) {
        if (header.ack) {
            _sender.ack_received(header.ackno, header.win);
        }
        if (seg.length_in_sequence_space() > 0) {
            if (seg.payload().size()) {
                _receiver.segment_received(seg);
            }
            send_ack();
        }
    } else {
        if (header.ack) {
            _sender.ack_received(header.ackno, header.win);
        }
        if (seg.length_in_sequence_space() > 0) {
            _receiver.segment_received(seg);
            send_ack();
        }
    }
    if (state() == TCPState(TCPState::State::TIME_WAIT)) {
        _rt_timeout = _cfg.rt_timeout * 10;
    }
    _time_since_last_segment_received = 0;
    if (state() != TCPState{TCPState::State::LISTEN}) {
        fill_window();
    }
    if (_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0) and
        header.seqno == _receiver.ackno().value() - 1) {
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
        fill_window();
        times++;
    }
    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
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
    fill_window([](TCPSegment &seg, TCPConnection &c) -> bool { return seg.header().fin && (true || c.active()); });
}

void TCPConnection::connect() {
    fill_window([](TCPSegment &seg, TCPConnection &c) -> bool {
        return seg.header().syn && c.state() == TCPState{TCPState::State::SYN_RCVD};
    });
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
