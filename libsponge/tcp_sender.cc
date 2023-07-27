#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <algorithm>
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
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    if (_next_seqno == 0) {
        TCPSegment seg;
        seg.header().syn = true;
        seg.header().seqno = _isn;
        _segments_out.emplace(seg);
        _bytes_in_flight += seg.length_in_sequence_space();
        _next_seqno = _next_seqno + seg.length_in_sequence_space();
        _outstanding.push(OutstandingData{
            seg.payload().copy(), _initial_retransmission_timeout, seg.header().seqno, seg.length_in_sequence_space()});
    } else if (_latest_window_size >= _bytes_in_flight) {
        bool isWillFin = _stream.input_ended() && _fin_seqno == 0;
        uint64_t size = _latest_window_size;
        if (_latest_window_size == 0 && _bytes_in_flight == 0) {
            size = 1;
        } else if (_latest_window_size == _bytes_in_flight) {
            return;
        }
        if (_stream.buffer_size() > 0 || isWillFin) {
            TCPSegment seg;
            seg.header().seqno = _isn + _next_seqno;
            seg.payload() =
                _stream.read(min(TCPConfig::MAX_PAYLOAD_SIZE,
                                 min(size - _bytes_in_flight, static_cast<uint64_t>(_stream.buffer_size()))));
            if (isWillFin && _stream.buffer_size() == 0 && size > _bytes_in_flight + seg.length_in_sequence_space()) {
                seg.header().fin = true;
                _fin_seqno = _next_seqno;
            }
            _outstanding.push(OutstandingData{seg.payload().copy(),
                                              _initial_retransmission_timeout,
                                              seg.header().seqno,
                                              seg.length_in_sequence_space()});
            _segments_out.emplace(seg);
            _bytes_in_flight += seg.length_in_sequence_space();
            _next_seqno = _next_seqno + seg.length_in_sequence_space();
            if (_latest_window_size > _bytes_in_flight) {
                fill_window();
            }
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    if (unwrap(ackno, _isn, 0) == _next_seqno) {
        _bytes_in_flight = 0;
        while (_outstanding.size()) {
            _outstanding.pop();
        }
    } else if (_outstanding.size()) {
        auto &tmp = _outstanding.front();
        if (tmp.seqno + tmp.len == ackno) {
            _bytes_in_flight -= tmp.len;
            _outstanding.pop();
        }
    }
    _latest_window_size = window_size;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_outstanding.size()) {
        auto &tmp = _outstanding.front();
        tmp.tick -= ms_since_last_tick;
        if (tmp.tick <= 0) {
            TCPSegment seg;
            seg.header().seqno = tmp.seqno;
            if (tmp.seqno == _isn) {
                seg.header().syn = true;
            }
            if (_fin_seqno != 0 && tmp.seqno == _isn + _fin_seqno) {
                seg.header().fin = true;
            }
            seg.payload() = static_cast<string>(tmp.data);
            _segments_out.emplace(seg);
            tmp.retx += 1;
            if (_latest_window_size == 0 && _bytes_in_flight == 1) {
                tmp.tick = _initial_retransmission_timeout;
            } else {
                tmp.tick = _initial_retransmission_timeout << tmp.retx;
            }
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const {
    if (_outstanding.size()) {
        return _outstanding.front().retx;
    }
    return 0;
}

void TCPSender::send_empty_segment() {}
