#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame frame;
    frame.header().src = _ethernet_address;
    frame.header().type = EthernetHeader::TYPE_IPv4;
    frame.payload() = dgram.serialize();
    if (!_table.count(next_hop_ip) || _timer > _table[next_hop_ip].t) {
        if (!_pending_arp.count(next_hop_ip) || _timer - _pending_arp[next_hop_ip] >= 5000) {
            EthernetFrame arpFrame;
            arpFrame.header().src = _ethernet_address;
            arpFrame.header().dst = ETHERNET_BROADCAST;
            arpFrame.header().type = EthernetHeader::TYPE_ARP;
            ARPMessage arp;
            arp.opcode = ARPMessage::OPCODE_REQUEST;
            arp.sender_ip_address = _ip_address.ipv4_numeric();
            arp.sender_ethernet_address = _ethernet_address;
            arp.target_ip_address = next_hop_ip;
            arpFrame.payload() = arp.serialize();
            _frames_out.push(arpFrame);
            _frames_waiting.push({frame, next_hop_ip});
            _pending_arp[next_hop_ip] = _timer;
        }
    } else {
        frame.header().dst = _table[next_hop_ip].mac;
        _frames_out.push(frame);
    }
}

bool ethernet_address_equal(EthernetAddress addr1, EthernetAddress addr2) {
    for (int i = 0; i < 6; i++) {
        if (addr1[i] != addr2[i]) {
            return false;
        }
    }
    return true;
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (!ethernet_address_equal(frame.header().dst, ETHERNET_BROADCAST) &&
        !ethernet_address_equal(frame.header().dst, _ethernet_address)) {
        return nullopt;
    } else if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError) {
            return dgram;
        }
    } else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp;
        if (arp.parse(frame.payload()) == ParseResult::NoError) {
            uint32_t ip = arp.sender_ip_address;
            _table[ip].mac = arp.sender_ethernet_address;
            _table[ip].t = _timer + 30 * 1000;
            _pending_arp.erase(ip);
            if (arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == _ip_address.ipv4_numeric()) {
                ARPMessage reply;
                reply.opcode = ARPMessage::OPCODE_REPLY;
                reply.sender_ethernet_address = _ethernet_address;
                reply.sender_ip_address = _ip_address.ipv4_numeric();
                reply.target_ethernet_address = arp.sender_ethernet_address;
                reply.target_ip_address = arp.sender_ip_address;

                EthernetFrame arp_frame;
                arp_frame.header().type = EthernetHeader::TYPE_ARP;
                arp_frame.header().src = _ethernet_address;
                arp_frame.header().dst = arp.sender_ethernet_address;
                arp_frame.payload() = move(reply.serialize());
                _frames_out.push(arp_frame);
            }
            while (!_frames_waiting.empty()) {
                WaitingFrame node = _frames_waiting.front();
                if (_table.count(node.ip) && _timer <= _table[node.ip].t) {
                    node.frame.header().dst = _table[node.ip].mac;
                    _frames_waiting.pop();
                    _frames_out.push(move(node.frame));
                } else {
                    break;
                }
            }
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _timer += ms_since_last_tick;
    for (auto &it : _pending_arp) {
        if (_timer - it.second >= 5000) {
            EthernetFrame arpFrame;
            arpFrame.header().src = _ethernet_address;
            arpFrame.header().dst = ETHERNET_BROADCAST;
            arpFrame.header().type = EthernetHeader::TYPE_ARP;
            ARPMessage arp;
            arp.opcode = ARPMessage::OPCODE_REQUEST;
            arp.sender_ip_address = _ip_address.ipv4_numeric();
            arp.sender_ethernet_address = _ethernet_address;
            arp.target_ip_address = it.first;
            arpFrame.payload() = arp.serialize();
            _frames_out.push(arpFrame);
            _pending_arp[it.first] = _timer;
        }
    }
}
