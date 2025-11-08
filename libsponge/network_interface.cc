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
    auto dst_entry = _arp_cache.find(next_hop_ip);
    if (dst_entry != _arp_cache.end()) {
        // 在缓存中找到了对应的MAC地址
        // 组装帧
        EthernetFrame frame;
        frame.header().dst = dst_entry->second.mac_addr;
        frame.header().src = _ethernet_address;
        frame.header().type = EthernetHeader::TYPE_IPv4;
        frame.payload() = BufferList(dgram.serialize());

        // 发送
        _frames_out.push(frame);
        return;
    }
    // 没有找到MAC地址，准备广播ARP请求
    // 查找是否已经发送过请求，如果未发送或已过期，发送
    auto request = _already_requests.find(next_hop_ip);
    if (request == _already_requests.end()) {
        // 组装ARP请求分组
        ARPMessage arp_request;
        arp_request.sender_ip_address = _ip_address.ipv4_numeric();
        arp_request.sender_ethernet_address = _ethernet_address;
        arp_request.target_ip_address = next_hop_ip;
        arp_request.target_ethernet_address = {00, 00, 00, 00, 00, 00};  //未知MAC地址设为全0
        arp_request.opcode = arp_request.OPCODE_REQUEST;

        EthernetFrame frame;
        frame.header().dst = ETHERNET_BROADCAST;
        frame.header().src = _ethernet_address;
        frame.header().type = EthernetHeader::TYPE_ARP;
        frame.payload() = BufferList(arp_request.serialize());

        _frames_out.push(frame);

        // 将请求放入记录
        _already_requests[next_hop_ip] = ARPRequest(_current_time, 0);
    }

    // 将数据包放入等待队列
    PendingPacket packet{dgram, next_hop, _current_time};
    _pending_packets[next_hop_ip].push(packet);

    return;

}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 检查MAC地址
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) 
        return nullopt;
    // 如果MAC帧是IPv4，解析负载，如果成功，将结果返回
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        string payload = frame.payload().concatenate();
        Buffer payload_buffer(move(payload));
        // 解析数据报
        InternetDatagram dgram;
        ParseResult result = dgram.parse(payload_buffer);
        if (result != ParseResult::NoError) {
            // 解析出错
            return nullopt;
        }
        
        return dgram;
    }
    else if (frame.header().type == EthernetHeader::TYPE_ARP) {
        // ARP，解析ARPMessage
        string arp = frame.payload().concatenate();
        Buffer arp_buffer(move(arp));
        
        ARPMessage arp_msg;
        if (arp_msg.parse(arp_buffer) != ParseResult::NoError) {
            // 解析出错
            return nullopt;
        }

        // 处理ARP
        // 符合本地ip的ARP请求分组，发送ARP回复
        if (arp_msg.opcode == arp_msg.OPCODE_REQUEST && arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
            // 组装ARP回复分组
            ARPMessage arp_reply;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.target_ip_address = arp_msg.sender_ip_address;
            arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
            arp_reply.opcode = arp_reply.OPCODE_REPLY;

            EthernetFrame out_frame;
            out_frame.header().dst = arp_msg.sender_ethernet_address;
            out_frame.header().src = _ethernet_address;
            out_frame.header().type = EthernetHeader::TYPE_ARP;
            out_frame.payload() = BufferList(arp_reply.serialize());

            _frames_out.push(out_frame);

            // 记住发送者的IP地址和以太网地址之间的关系30秒
            _arp_cache[arp_msg.sender_ip_address] = {arp_msg.sender_ethernet_address, _current_time};
            // 清除ARP请求记录
            _already_requests.erase(arp_msg.sender_ip_address);
            return nullopt;
        }

        // ARP回复分组，将之前保存的IP数据报依次发送
        else if (arp_msg.opcode == arp_msg.OPCODE_REPLY && arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
            auto pending_queue = _pending_packets.find(arp_msg.sender_ip_address);
            if (pending_queue != _pending_packets.end()) {
                while (!pending_queue->second.empty()) {
                    auto& packet = pending_queue->second.front();
                    // 组装帧
                    EthernetFrame out_frame;
                    out_frame.header().dst = arp_msg.sender_ethernet_address;
                    out_frame.header().src = _ethernet_address;
                    out_frame.header().type = EthernetHeader::TYPE_IPv4;
                    out_frame.payload() = packet.datagram.serialize();

                    _frames_out.push(out_frame);
                    pending_queue->second.pop();
                }
                _pending_packets.erase(arp_msg.sender_ip_address);
            }
            // 记住发送者的IP地址和以太网地址之间的关系30秒
            _arp_cache[arp_msg.sender_ip_address] = {arp_msg.sender_ethernet_address, _current_time};
            // 清除ARP请求记录
            _already_requests.erase(arp_msg.sender_ip_address);
        }
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // 更新当前时间戳
    _current_time += ms_since_last_tick;

    // 清理过期ARP缓存
    for (auto it = _arp_cache.begin(); it != _arp_cache.end();) {
        if (_current_time - it->second.timestamp > ARP_CACHE_TTL) {
            it = _arp_cache.erase(it);
        } else ++it;
    }

    // 检查各项ARP请求，如果已过期，则清除，下次需要重新请求，其余请求的剩余时间减少ms_since_last_tick
    for (auto it = _already_requests.begin(); it != _already_requests.end();) {
        auto &req = it->second;
        if (_current_time - req.timestamp >= ARP_REQUEST_TIMEOUT) {
            if (req.retry_count >= ARP_MAX_RETRIES) {
                // 超过重试次数，放弃
                _pending_packets.erase(it->first);
                it = _already_requests.erase(it);
                continue;
            }
            // 重发 ARP request
            ARPMessage arp_req;
            arp_req.sender_ip_address = _ip_address.ipv4_numeric();
            arp_req.sender_ethernet_address = _ethernet_address;
            arp_req.target_ip_address = it->first;
            arp_req.target_ethernet_address = {};
            arp_req.opcode = arp_req.OPCODE_REQUEST;

            EthernetFrame frame;
            frame.header().dst = ETHERNET_BROADCAST;
            frame.header().src = _ethernet_address;
            frame.header().type = EthernetHeader::TYPE_ARP;
            frame.payload() = BufferList(arp_req.serialize());
            _frames_out.push(frame);

            // 更新时间戳与计数
            req.timestamp = _current_time;
            req.retry_count++;
        }
        ++it;
    }
}
