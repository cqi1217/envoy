#include "test/test_common/network_utility.h"

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/runtime/runtime_impl.h"

namespace Envoy {
namespace Network {
namespace Test {

Address::InstanceConstSharedPtr findOrCheckFreePort(Address::InstanceConstSharedPtr addr_port,
                                                    Socket::Type type) {
  if (addr_port == nullptr || addr_port->type() != Address::Type::Ip) {
    ADD_FAILURE() << "Not an internet address: "
                  << (addr_port == nullptr ? "nullptr" : addr_port->asString());
    return nullptr;
  }
  SocketImpl sock(type, addr_port, nullptr, {});
  // Not setting REUSEADDR, therefore if the address has been recently used we won't reuse it here.
  // However, because we're going to use the address while checking if it is available, we'll need
  // to set REUSEADDR on listener sockets created by tests using an address validated by this means.
  Api::SysCallIntResult result = sock.bind(addr_port);
  const char* failing_fn = nullptr;
  if (result.return_value_ != 0) {
    failing_fn = "bind";
  } else if (type == Socket::Type::Stream) {
    // Try listening on the port also, if the type is TCP.
    result = sock.listen(1);
    if (result.return_value_ != 0) {
      failing_fn = "listen";
    }
  }
  if (failing_fn != nullptr) {
    if (result.errno_ == SOCKET_ERROR_ADDR_IN_USE) {
      // The port is already in use. Perfectly normal.
      return nullptr;
    } else if (result.errno_ == SOCKET_ERROR_ACCESS) {
      // A privileged port, and we don't have privileges. Might want to log this.
      return nullptr;
    }
    // Unexpected failure.
    ADD_FAILURE() << failing_fn << " failed for '" << addr_port->asString()
                  << "' with error: " << errorDetails(result.errno_) << " (" << result.errno_
                  << ")";
    return nullptr;
  }
  return sock.connectionInfoProvider().localAddress();
}

Address::InstanceConstSharedPtr findOrCheckFreePort(const std::string& addr_port,
                                                    Socket::Type type) {
  auto instance = Utility::parseInternetAddressAndPortNoThrow(addr_port);
  if (instance != nullptr) {
    instance = findOrCheckFreePort(instance, type);
  } else {
    ADD_FAILURE() << "Unable to parse as an address and port: " << addr_port;
  }
  return instance;
}

std::string getLoopbackAddressUrlString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return {"[::1]"};
  }
  return {"127.0.0.1"};
}

std::string getLoopbackAddressString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return {"::1"};
  }
  return {"127.0.0.1"};
}

std::string getAnyAddressUrlString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return {"[::]"};
  }
  return {"0.0.0.0"};
}

std::string getAnyAddressString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v6) {
    return {"::"};
  }
  return {"0.0.0.0"};
}

std::string addressVersionAsString(const Address::IpVersion version) {
  if (version == Address::IpVersion::v4) {
    return {"v4"};
  }
  return {"v6"};
}

Address::InstanceConstSharedPtr getCanonicalLoopbackAddress(Address::IpVersion version) {
  if (version == Address::IpVersion::v4) {
    return Network::Utility::getCanonicalIpv4LoopbackAddress();
  }
  return Network::Utility::getIpv6LoopbackAddress();
}

namespace {

// There is no portable way to initialize sockaddr_in6 with a static initializer, do it with a
// helper function instead.
sockaddr_in6 sockaddrIn6Any() {
  sockaddr_in6 v6any = {};
  v6any.sin6_family = AF_INET6;
  v6any.sin6_addr = in6addr_any;

  return v6any;
}

} // namespace

Address::InstanceConstSharedPtr getAnyAddress(const Address::IpVersion version, bool v4_compat) {
  if (version == Address::IpVersion::v4) {
    return Network::Utility::getIpv4AnyAddress();
  }
  if (v4_compat) {
    // This will return an IPv6 ANY address ("[::]:0") like the getIpv6AnyAddress() below, but
    // with the internal 'v6only' member set to false. This will allow a socket created from this
    // address to accept IPv4 connections. IPv4 connections received on IPv6 sockets will have
    // Ipv4-mapped IPv6 addresses, which we will then internally interpret as IPv4 addresses so
    // that, for example, access logging will show IPv4 address format for IPv4 connections even
    // if they were received on an IPv6 socket.
    static Address::InstanceConstSharedPtr any(new Address::Ipv6Instance(sockaddrIn6Any(), false));
    return any;
  }
  return Network::Utility::getIpv6AnyAddress();
}

bool supportsIpVersion(const Address::IpVersion version) {
  return Network::SocketInterfaceSingleton::get().ipFamilySupported(
      version == Address::IpVersion::v4 ? AF_INET : AF_INET6);
}

std::string ipVersionToDnsFamily(Network::Address::IpVersion version) {
  switch (version) {
  case Network::Address::IpVersion::v4:
    return "V4_ONLY";
  case Network::Address::IpVersion::v6:
    return "V6_ONLY";
  }

  // This seems to be needed on the coverage build for some reason.
  PANIC("reached unexpected code");
}

std::pair<Address::InstanceConstSharedPtr, Network::SocketPtr>
bindFreeLoopbackPort(Address::IpVersion version, Socket::Type type, bool reuse_port) {
  Address::InstanceConstSharedPtr addr = getCanonicalLoopbackAddress(version);
  SocketPtr sock = std::make_unique<SocketImpl>(type, addr, nullptr, SocketCreationOptions{});
  if (reuse_port) {
    sock->addOptions(SocketOptionFactory::buildReusePortOptions());
    Socket::applyOptions(sock->options(), *sock,
                         envoy::config::core::v3::SocketOption::STATE_PREBIND);
  }
  Api::SysCallIntResult result = sock->bind(addr);
  if (0 != result.return_value_) {
    sock->close();
    std::string msg = fmt::format("bind failed for address {} with error: {} ({})",
                                  addr->asString(), errorDetails(result.errno_), result.errno_);
    ADD_FAILURE() << msg;
    throwEnvoyExceptionOrPanic(msg);
  }

  return std::make_pair(sock->connectionInfoProvider().localAddress(), std::move(sock));
}

TransportSocketPtr createRawBufferSocket() { return std::make_unique<RawBufferSocket>(); }

UpstreamTransportSocketFactoryPtr createRawBufferSocketFactory() {
  return std::make_unique<RawBufferSocketFactory>();
}

DownstreamTransportSocketFactoryPtr createRawBufferDownstreamSocketFactory() {
  return std::make_unique<RawBufferSocketFactory>();
}

const Network::FilterChainSharedPtr
createEmptyFilterChain(DownstreamTransportSocketFactoryPtr&& transport_socket_factory) {
  return std::make_shared<Network::Test::EmptyFilterChain>(std::move(transport_socket_factory));
}

const Network::FilterChainSharedPtr createEmptyFilterChainWithRawBufferSockets() {
  return createEmptyFilterChain(createRawBufferDownstreamSocketFactory());
}

namespace {
struct SyncPacketProcessor : public Network::UdpPacketProcessor {
  SyncPacketProcessor(std::list<Network::UdpRecvData>& data, uint64_t max_rx_datagram_size)
      : data_(data), max_rx_datagram_size_(max_rx_datagram_size) {}

  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                     Buffer::OwnedImpl saved_cmsg) override {
    Network::UdpRecvData datagram{{std::move(local_address), std::move(peer_address)},
                                  std::move(buffer),
                                  receive_time,
                                  tos,
                                  std::move(saved_cmsg)};
    data_.push_back(std::move(datagram));
  }
  uint64_t maxDatagramSize() const override { return max_rx_datagram_size_; }
  void onDatagramsDropped(uint32_t) override {}
  size_t numPacketsExpectedPerEventLoop() const override {
    return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
  }
  const IoHandle::UdpSaveCmsgConfig& saveCmsgConfig() const override {
    static const IoHandle::UdpSaveCmsgConfig empty_config{};
    return empty_config;
  }

  std::list<Network::UdpRecvData>& data_;
  const uint64_t max_rx_datagram_size_;
};
} // namespace

Api::IoCallUint64Result readFromSocket(IoHandle& handle, const Address::Instance& local_address,
                                       std::list<UdpRecvData>& data,
                                       uint64_t max_rx_datagram_size) {
  SyncPacketProcessor processor(data, max_rx_datagram_size);
  UdpRecvMsgMethod recv_msg_method = UdpRecvMsgMethod::RecvMsg;
  if (Api::OsSysCallsSingleton::get().supportsMmsg()) {
    recv_msg_method = UdpRecvMsgMethod::RecvMmsg;
  }
  return Network::Utility::readFromSocket(handle, local_address, processor,
                                          MonotonicTime(std::chrono::seconds(0)), recv_msg_method,
                                          nullptr, nullptr);
}

UdpSyncPeer::UdpSyncPeer(Network::Address::IpVersion version, uint64_t max_rx_datagram_size)
    : socket_(
          std::make_unique<UdpListenSocket>(getCanonicalLoopbackAddress(version), nullptr, true)),
      max_rx_datagram_size_(max_rx_datagram_size) {
  RELEASE_ASSERT(socket_->setBlockingForTest(true).return_value_ != -1, "");
}

void UdpSyncPeer::write(const std::string& buffer, const Network::Address::Instance& peer) {
  const auto rc = Network::Utility::writeToSocket(socket_->ioHandle(), Buffer::OwnedImpl(buffer),
                                                  nullptr, peer);
  ASSERT_EQ(rc.return_value_, buffer.length());
}

void UdpSyncPeer::recv(Network::UdpRecvData& datagram) {
  if (received_datagrams_.empty()) {
    const auto rc = Network::Test::readFromSocket(socket_->ioHandle(),
                                                  *socket_->connectionInfoProvider().localAddress(),
                                                  received_datagrams_, max_rx_datagram_size_);
    ASSERT_TRUE(rc.ok());
  }
  datagram = std::move(received_datagrams_.front());
  received_datagrams_.pop_front();
}

sockaddr_storage getV6SockAddr(const std::string& ip, uint32_t port) {
  sockaddr_storage ss;
  auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
  memset(ipv6_addr, 0, sizeof(sockaddr_in6));
  ipv6_addr->sin6_family = AF_INET6;
  inet_pton(AF_INET6, ip.c_str(), &ipv6_addr->sin6_addr);
  ipv6_addr->sin6_port = htons(port);
  return ss;
}

sockaddr_storage getV4SockAddr(const std::string& ip, uint32_t port) {
  sockaddr_storage ss;
  auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
  memset(ipv4_addr, 0, sizeof(sockaddr_in));
  ipv4_addr->sin_family = AF_INET;
  inet_pton(AF_INET, ip.c_str(), &ipv4_addr->sin_addr);
  ipv4_addr->sin_port = htons(port);
  return ss;
}

socklen_t getSockAddrLen(const sockaddr_storage& ss) {
  if (ss.ss_family == AF_INET6) {
    return sizeof(sockaddr_in6);
  }
  return sizeof(sockaddr_in);
}

} // namespace Test
} // namespace Network
} // namespace Envoy
