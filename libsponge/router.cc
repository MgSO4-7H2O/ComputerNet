#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // 构造路由表                    
    RouteEntry entry {
        route_prefix,
        prefix_length,
        next_hop.has_value()? std::optional<uint32_t>{next_hop->ipv4_numeric()} : std::nullopt,
        interface_num
    };
    _routes.push_back(entry);
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // 检查ttl是否满足
    if (dgram.header().ttl <= 1) {
        // ttl不满足，丢弃
        return ;
    }
    dgram.header().ttl--;
    // 重新计算校验和
    uint16_t new_cksum = 0;
    string head = dgram.header().serialize();
    InternetChecksum check;
    check.add(head);
    new_cksum = check.value();
    dgram.header().cksum = new_cksum;

    uint32_t dst = dgram.header().dst;
    RouteEntry* longest = nullptr;
    uint8_t longest_prefix = 0;
    // 查找地址匹配的路由
    for (auto &r : _routes) {
        // 构造网络掩码
        uint32_t mask = r._prefix_length == 0 ? 0 : 0xFFFFFFFF << (32 - r._prefix_length);
        // 最长前缀匹配
        if ((dst & mask) == (r._route_prefix & mask)) {
            if (r._prefix_length > longest_prefix) {
                longest_prefix = r._prefix_length;
                longest = &r;
            }
        }
    }
    // 没找到匹配项
    if (!longest) return ;

    // 发送到下一跳
    uint32_t next_hop_ip = longest->_next_hop.value_or(dst);
    Address next_hop_addr = Address::from_ipv4_numeric(next_hop_ip);
    _interfaces[longest->_interface_num].send_datagram(dgram, next_hop_addr);

}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
