#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <optional>
#include <queue>

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

//! This module is the lowest layer of a TCP/IP stack
//! (connecting IP with the lower-layer network protocol,
//! e.g. Ethernet). But the same module is also used repeatedly
//! as part of a router: a router generally has many network
//! interfaces, and the router's job is to route Internet datagrams
//! between the different interfaces.

//! The network interface translates datagrams (coming from the
//! "customer," e.g. a TCP/IP stack or router) into Ethernet
//! frames. To fill in the Ethernet destination address, it looks up
//! the Ethernet address of the next IP hop of each datagram, making
//! requests with the [Address Resolution Protocol](\ref rfc::rfc826).
//! In the opposite direction, the network interface accepts Ethernet
//! frames, checks if they are intended for it, and if so, processes
//! the the payload depending on its type. If it's an IPv4 datagram,
//! the network interface passes it up the stack. If it's an ARP
//! request or reply, the network interface processes the frame
//! and learns or replies as necessary.
class NetworkInterface {
  private:
    // 参数常量
    static constexpr size_t ARP_CACHE_TTL = 30000;        // ARP缓存30s
    static constexpr size_t ARP_REQUEST_TIMEOUT = 5000;   // ARP请求超时5s
    static constexpr int ARP_MAX_RETRIES = 3;             // 最大重试次数
    
    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};
    
    // ARP缓存表
    struct ARPEntry {
      EthernetAddress mac_addr; // 记录端口IP地址对应的mac地址
      size_t timestamp;   // 记录老化时间
    };
    std::unordered_map<uint32_t, ARPEntry> _arp_cache{};

    // 由于等待ARP回复未能及时发出的数据包
    struct PendingPacket {
      InternetDatagram datagram;  // 数据包
      Address next_hop;   // 下一跳地址
      size_t timestamp;   // 排队时间
    };
    std::unordered_map<uint32_t, std::queue<PendingPacket>> _pending_packets{};

    // 已发送的ARP请求记录，避免重复发送
    struct ARPRequest {
      size_t timestamp;   // 发送时间
      int retry_count{0}; // 重试次数
      ARPRequest() : timestamp(0), retry_count(0) {}
      ARPRequest(size_t ts, int retry) 
        : timestamp(ts), retry_count(retry) {}
    };
    std::unordered_map<uint32_t, ARPRequest> _already_requests{};

    size_t _current_time{0};  // 当前时间(ms)


  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).

    //! Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next hop
    //! ("Sending" is accomplished by pushing the frame onto the frames_out queue.)
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.

    //! If type is IPv4, returns the datagram.
    //! If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! If type is ARP reply, learn a mapping from the "sender" fields.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
