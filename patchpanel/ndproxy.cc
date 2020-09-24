// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/ndproxy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <linux/in6.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <string>
#include <utility>

#include <base/bind.h>

#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/net_util.h"

namespace patchpanel {
namespace {
const unsigned char kBroadcastMacAddress[] = {0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff};

sock_filter kNDFrameBpfInstructions[] = {
    // Load ethernet type.
    BPF_STMT(BPF_LD | BPF_H | BPF_ABS, offsetof(ether_header, ether_type)),
    // Check if it equals IPv6, if not, then goto return 0.
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETHERTYPE_IPV6, 0, 9),
    // Move index to start of IPv6 header.
    BPF_STMT(BPF_LDX | BPF_IMM, sizeof(ether_header)),
    // Load IPv6 next header.
    BPF_STMT(BPF_LD | BPF_B | BPF_IND, offsetof(ip6_hdr, ip6_nxt)),
    // Check if equals ICMPv6, if not, then goto return 0.
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_ICMPV6, 0, 6),
    // Move index to start of ICMPv6 header.
    BPF_STMT(BPF_LDX | BPF_IMM, sizeof(ether_header) + sizeof(ip6_hdr)),
    // Load ICMPv6 type.
    BPF_STMT(BPF_LD | BPF_B | BPF_IND, offsetof(icmp6_hdr, icmp6_type)),
    // Check if is ND ICMPv6 message.
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_ROUTER_SOLICIT, 4, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_ROUTER_ADVERT, 3, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_NEIGHBOR_SOLICIT, 2, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ND_NEIGHBOR_ADVERT, 1, 0),
    // Return 0.
    BPF_STMT(BPF_RET | BPF_K, 0),
    // Return MAX.
    BPF_STMT(BPF_RET | BPF_K, IP_MAXPACKET),
};
const sock_fprog kNDFrameBpfProgram = {
    .len = sizeof(kNDFrameBpfInstructions) / sizeof(sock_filter),
    .filter = kNDFrameBpfInstructions};

}  // namespace

constexpr ssize_t NDProxy::kTranslateErrorNotICMPv6Frame;
constexpr ssize_t NDProxy::kTranslateErrorNotNDFrame;
constexpr ssize_t NDProxy::kTranslateErrorInsufficientLength;
constexpr ssize_t NDProxy::kTranslateErrorBufferMisaligned;

NDProxy::NDProxy()
    : in_frame_buffer_(AlignFrameBuffer(in_frame_buffer_extended_)),
      out_frame_buffer_(AlignFrameBuffer(out_frame_buffer_extended_)) {}

base::ScopedFD NDProxy::PreparePacketSocket() {
  base::ScopedFD fd(
      socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_IPV6)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "socket() failed";
    return base::ScopedFD();
  }
  if (setsockopt(fd.get(), SOL_SOCKET, SO_ATTACH_FILTER, &kNDFrameBpfProgram,
                 sizeof(kNDFrameBpfProgram))) {
    PLOG(ERROR) << "setsockopt(SO_ATTACH_FILTER) failed";
    return base::ScopedFD();
  }
  return fd;
}

bool NDProxy::Init() {
  rtnl_fd_ = base::ScopedFD(
      socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
  if (!rtnl_fd_.is_valid()) {
    PLOG(ERROR) << "socket() failed for rtnetlink socket";
    return false;
  }
  sockaddr_nl local = {
      .nl_family = AF_NETLINK,
      .nl_groups = 0,
  };
  if (bind(rtnl_fd_.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local)) <
      0) {
    PLOG(ERROR) << "bind() failed on rtnetlink socket";
    return false;
  }

  dummy_fd_ = base::ScopedFD(socket(AF_INET6, SOCK_DGRAM, 0));
  if (!dummy_fd_.is_valid()) {
    PLOG(ERROR) << "socket() failed for dummy socket";
    return false;
  }
  return true;
}

// In an ICMPv6 Ethernet Frame *frame with length frame_len, replace the mac
// address in option opt_type into *target_mac. nd_hdr_len indicates the length
// of ICMPv6 ND message headers (so the first option starts after nd_hdr_len.)
void NDProxy::ReplaceMacInIcmpOption(uint8_t* frame,
                                     ssize_t frame_len,
                                     size_t nd_hdr_len,
                                     uint8_t opt_type,
                                     const MacAddress& target_mac) {
  nd_opt_hdr* opt;
  nd_opt_hdr* end = reinterpret_cast<nd_opt_hdr*>(&frame[frame_len]);
  for (opt = reinterpret_cast<nd_opt_hdr*>(frame + ETHER_HDR_LEN +
                                           sizeof(ip6_hdr) + nd_hdr_len);
       opt < end && opt->nd_opt_len > 0;
       opt = reinterpret_cast<nd_opt_hdr*>(reinterpret_cast<uint64_t*>(opt) +
                                           opt->nd_opt_len)) {
    if (opt->nd_opt_type == opt_type) {
      uint8_t* mac_in_opt =
          reinterpret_cast<uint8_t*>(opt) + sizeof(nd_opt_hdr);
      memcpy(mac_in_opt, target_mac.data(), ETHER_ADDR_LEN);
    }
  }
}

// RFC 4389
// Read the input ICMPv6 frame and determine whether it should be proxied. If
// so, fill out_frame buffer with proxied frame and return the length of proxied
// frame (usually same with input frame length). Return a negative value if
// proxy is not needed or error occured.
// in_frame: buffer containing input ethernet frame; needs special alignment
//           so that IP header is 4-bytes aligned;
// frame_len: the length of input frame;
// local_mac_addr: MAC address of interface that will be used to send frame;
// out_frame: buffer for output frame; should have at least space of frame_len;
//            needs special alignment so that IP header is 4-bytes aligned.
ssize_t NDProxy::TranslateNDFrame(const uint8_t* in_frame,
                                  ssize_t frame_len,
                                  const MacAddress& local_mac_addr,
                                  uint8_t* out_frame) {
  if ((reinterpret_cast<uintptr_t>(in_frame + ETHER_HDR_LEN) & 0x3) != 0 ||
      (reinterpret_cast<uintptr_t>(out_frame + ETHER_HDR_LEN) & 0x3) != 0) {
    return kTranslateErrorBufferMisaligned;
  }
  if (frame_len < ETHER_HDR_LEN + sizeof(ip6_hdr) + sizeof(icmp6_hdr)) {
    return kTranslateErrorInsufficientLength;
  }
  if (reinterpret_cast<const ethhdr*>(in_frame)->h_proto != htons(ETH_P_IPV6) ||
      reinterpret_cast<const ip6_hdr*>(in_frame + ETHER_HDR_LEN)->ip6_nxt !=
          IPPROTO_ICMPV6) {
    return kTranslateErrorNotICMPv6Frame;
  }

  memcpy(out_frame, in_frame, frame_len);
  ethhdr* eth = reinterpret_cast<ethhdr*>(out_frame);
  ip6_hdr* ip6 = reinterpret_cast<ip6_hdr*>(out_frame + ETHER_HDR_LEN);
  icmp6_hdr* icmp6 =
      reinterpret_cast<icmp6_hdr*>(out_frame + ETHER_HDR_LEN + sizeof(ip6_hdr));

  // If destination MAC is unicast (Individual/Group bit in MAC address == 0),
  // it needs to be modified so guest OS L3 stack can see it.
  // For proxy cascading case, we also need to recheck if destination MAC is
  // ff:ff:ff:ff:ff:ff (which must have been filled by an upstream proxy).
  if (!(eth->h_dest[0] & 0x1) ||
      memcmp(eth->h_dest, kBroadcastMacAddress, ETHER_ADDR_LEN) == 0) {
    MacAddress neighbor_mac;
    if (GetNeighborMac(ip6->ip6_dst, &neighbor_mac)) {
      memcpy(eth->h_dest, neighbor_mac.data(), ETHER_ADDR_LEN);
    } else {
      // If we can't resolve the destination IP into MAC from kernel neighbor
      // table, fill destination MAC with broadcast MAC instead.
      memcpy(eth->h_dest, kBroadcastMacAddress, ETHER_ADDR_LEN);
    }
  }

  switch (icmp6->icmp6_type) {
    case ND_ROUTER_SOLICIT:
      ReplaceMacInIcmpOption(out_frame, frame_len, sizeof(nd_router_solicit),
                             ND_OPT_SOURCE_LINKADDR, local_mac_addr);
      break;
    case ND_ROUTER_ADVERT: {
      // RFC 4389 Section 4.1.3.3 - Set Proxy bit
      nd_router_advert* ra = reinterpret_cast<nd_router_advert*>(icmp6);
      if (ra->nd_ra_flags_reserved & 0x04) {
        // According to RFC 4389, an RA packet with 'Proxy' bit set already
        // should not be proxied again, in order to avoid loop. However, we'll
        // need this form of proxy cascading in Crostini (Host->VM->Container)
        // so we are ignoring the check here. Note that we know we are doing RA
        // proxy in only one direction so there should be no loop.
      }
      ra->nd_ra_flags_reserved |= 0x04;

      ReplaceMacInIcmpOption(out_frame, frame_len, sizeof(nd_router_advert),
                             ND_OPT_SOURCE_LINKADDR, local_mac_addr);
      break;
    }
    case ND_NEIGHBOR_SOLICIT:
      ReplaceMacInIcmpOption(out_frame, frame_len, sizeof(nd_neighbor_solicit),
                             ND_OPT_SOURCE_LINKADDR, local_mac_addr);
      break;
    case ND_NEIGHBOR_ADVERT:
      ReplaceMacInIcmpOption(out_frame, frame_len, sizeof(nd_neighbor_advert),
                             ND_OPT_TARGET_LINKADDR, local_mac_addr);
      break;
    default:
      return kTranslateErrorNotNDFrame;
  }

  // We need to clear the old checksum first so checksum calculation does not
  // wrongly take old checksum into account.
  icmp6->icmp6_cksum = 0;
  icmp6->icmp6_cksum = Icmpv6Checksum(ip6, icmp6);

  memcpy(eth->h_source, local_mac_addr.data(), ETHER_ADDR_LEN);
  return frame_len;
}

void NDProxy::ReadAndProcessOneFrame(int fd) {
  sockaddr_ll dst_addr;
  struct iovec iov = {
      .iov_base = in_frame_buffer_,
      .iov_len = IP_MAXPACKET,
  };
  msghdr hdr = {
      .msg_name = &dst_addr,
      .msg_namelen = sizeof(dst_addr),
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = nullptr,
      .msg_controllen = 0,
      .msg_flags = 0,
  };

  ssize_t len;
  if ((len = recvmsg(fd, &hdr, 0)) < 0) {
    PLOG(ERROR) << "recvmsg() failed";
    return;
  }
  ip6_hdr* ip6 = reinterpret_cast<ip6_hdr*>(in_frame_buffer_ + ETH_HLEN);
  icmp6_hdr* icmp6 = reinterpret_cast<icmp6_hdr*>(
      in_frame_buffer_ + ETHER_HDR_LEN + sizeof(ip6_hdr));

  if (ip6->ip6_nxt != IPPROTO_ICMPV6 || icmp6->icmp6_type < ND_ROUTER_SOLICIT ||
      icmp6->icmp6_type > ND_NEIGHBOR_ADVERT)
    return;

  // Notify DeviceManager on receiving NA from guest, so a /128 route to the
  // guest can be added on the host.
  if (icmp6->icmp6_type == ND_NEIGHBOR_ADVERT &&
      IsGuestInterface(dst_addr.sll_ifindex) &&
      !guest_discovery_handler_.is_null()) {
    nd_neighbor_advert* na = reinterpret_cast<nd_neighbor_advert*>(icmp6);
    if (((na->nd_na_target.s6_addr[0] & 0xe0) == 0x20)        // Global Unicast
        || ((na->nd_na_target.s6_addr[0] & 0xfe) == 0xfc)) {  // Unique Local
      char ifname[IFNAMSIZ];
      if_indextoname(dst_addr.sll_ifindex, ifname);
      char ipv6_addr_str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &(na->nd_na_target.s6_addr), ipv6_addr_str,
                INET6_ADDRSTRLEN);
      guest_discovery_handler_.Run(std::string(ifname),
                                   std::string(ipv6_addr_str));
    }
  }

  // On receiving RA from router, generate an address for each guest-facing
  // interface, and sent it to DeviceManager so it can be assigned. This address
  // will be used when directly communicating with guest OS through IPv6.
  if (icmp6->icmp6_type == ND_ROUTER_ADVERT &&
      IsRouterInterface(dst_addr.sll_ifindex) &&
      !router_discovery_handler_.is_null()) {
    const nd_opt_prefix_info* prefix_info =
        GetPrefixInfoOption(in_frame_buffer_, len);
    if (prefix_info != nullptr && prefix_info->nd_opt_pi_prefix_len <= 64) {
      // Generate an EUI-64 address from virtual interface MAC. A prefix
      // larger that /64 is required.
      for (int target_if : if_map_ra_[dst_addr.sll_ifindex]) {
        MacAddress local_mac;
        if (!GetLocalMac(target_if, &local_mac))
          continue;
        in6_addr eui64_ip;
        GenerateEUI64Address(&eui64_ip, prefix_info->nd_opt_pi_prefix,
                             local_mac);
        char eui64_addr_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &eui64_ip, eui64_addr_str, INET6_ADDRSTRLEN);
        char target_ifname[IFNAMSIZ];
        if_indextoname(target_if, target_ifname);
        router_discovery_handler_.Run(std::string(target_ifname),
                                      std::string(eui64_addr_str));
      }
    }
  }

  // Translate the NDP frame and send it through proxy interface
  auto map_entry = MapForType(icmp6->icmp6_type)->find(dst_addr.sll_ifindex);
  if (map_entry == MapForType(icmp6->icmp6_type)->end())
    return;
  const auto& target_ifs = map_entry->second;
  for (int target_if : target_ifs) {
    MacAddress local_mac;
    if (!GetLocalMac(target_if, &local_mac))
      continue;
    int result =
        TranslateNDFrame(in_frame_buffer_, len, local_mac, out_frame_buffer_);
    if (result < 0) {
      switch (result) {
        case kTranslateErrorNotICMPv6Frame:
          LOG(DFATAL) << "Attempt to TranslateNDFrame on a non-ICMPv6 frame";
          return;
        case kTranslateErrorNotNDFrame:
          LOG(DFATAL) << "Attempt to TranslateNDFrame on a non-NDP frame, "
                         "icmpv6 type = "
                      << static_cast<int>(reinterpret_cast<icmp6_hdr*>(
                                              in_frame_buffer_ + ETHER_HDR_LEN +
                                              sizeof(ip6_hdr))
                                              ->icmp6_type);
          return;
        case kTranslateErrorInsufficientLength:
          LOG(DFATAL) << "TranslateNDFrame failed: frame_len = " << len
                      << " is too small";
          return;
        default:
          LOG(DFATAL) << "Unknown error in TranslateNDFrame";
          return;
      }
    }

    struct iovec iov_out = {
        .iov_base = out_frame_buffer_,
        .iov_len = static_cast<size_t>(len),
    };
    sockaddr_ll addr = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETH_P_IPV6),
        .sll_ifindex = target_if,
        .sll_halen = ETHER_ADDR_LEN,
    };
    memcpy(addr.sll_addr, reinterpret_cast<ethhdr*>(out_frame_buffer_)->h_dest,
           ETHER_ADDR_LEN);
    msghdr hdr = {
        .msg_name = &addr,
        .msg_namelen = sizeof(addr),
        .msg_iov = &iov_out,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
    };
    if (sendmsg(fd, &hdr, 0) < 0) {
      PLOG(ERROR) << "sendmsg() failed on interface " << target_if;
    }
  }
}

const nd_opt_prefix_info* NDProxy::GetPrefixInfoOption(const uint8_t* in_frame,
                                                       ssize_t frame_len) {
  for (const uint8_t* ptr =
           in_frame + ETH_HLEN + sizeof(ip6_hdr) + sizeof(nd_router_advert);
       ptr + offsetof(nd_opt_hdr, nd_opt_len) < in_frame + frame_len &&
       (reinterpret_cast<const nd_opt_hdr*>(ptr))->nd_opt_len > 0;
       ptr += (reinterpret_cast<const nd_opt_hdr*>(ptr))->nd_opt_len
              << 3) /* nd_opt_len is in 8 bytes */ {
    const nd_opt_hdr* opt = reinterpret_cast<const nd_opt_hdr*>(ptr);
    if (opt->nd_opt_type == ND_OPT_PREFIX_INFORMATION &&
        opt->nd_opt_len << 3 == sizeof(nd_opt_prefix_info)) {
      return reinterpret_cast<const nd_opt_prefix_info*>(opt);
    }
  }
  return nullptr;
}

bool NDProxy::GetLocalMac(int if_id, MacAddress* mac_addr) {
  ifreq ifr = {
      .ifr_ifindex = if_id,
  };
  if (ioctl(dummy_fd_.get(), SIOCGIFNAME, &ifr) < 0) {
    PLOG(ERROR) << "ioctl() failed to get interface name on interface "
                << if_id;
    return false;
  }
  if (ioctl(dummy_fd_.get(), SIOCGIFHWADDR, &ifr) < 0) {
    PLOG(ERROR) << "ioctl() failed to get MAC address on interface " << if_id;
    return false;
  }
  memcpy(mac_addr->data(), ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
  return true;
}

bool NDProxy::GetNeighborMac(const in6_addr& ipv6_addr, MacAddress* mac_addr) {
  sockaddr_nl kernel = {
      .nl_family = AF_NETLINK,
      .nl_groups = 0,
  };
  struct nl_req {
    nlmsghdr hdr;
    rtgenmsg gen;
  } req = {
      .hdr =
          {
              .nlmsg_len = NLMSG_LENGTH(sizeof(rtgenmsg)),
              .nlmsg_type = RTM_GETNEIGH,
              .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
              .nlmsg_seq = 1,
          },
      .gen =
          {
              .rtgen_family = AF_INET6,
          },
  };
  iovec io_req = {
      .iov_base = &req,
      .iov_len = req.hdr.nlmsg_len,
  };
  msghdr rtnl_req = {
      .msg_name = &kernel,
      .msg_namelen = sizeof(kernel),
      .msg_iov = &io_req,
      .msg_iovlen = 1,
  };
  if (sendmsg(rtnl_fd_.get(), &rtnl_req, 0) < 0) {
    PLOG(ERROR) << "sendmsg() failed on rtnetlink socket";
    return false;
  }

  static constexpr size_t kRtnlReplyBufferSize = 32768;
  char reply_buffer[kRtnlReplyBufferSize];
  iovec io_reply = {
      .iov_base = reply_buffer,
      .iov_len = kRtnlReplyBufferSize,
  };
  msghdr rtnl_reply = {
      .msg_name = &kernel,
      .msg_namelen = sizeof(kernel),
      .msg_iov = &io_reply,
      .msg_iovlen = 1,
  };

  bool any_entry_matched = false;
  bool done = false;
  while (!done) {
    ssize_t len;
    if ((len = recvmsg(rtnl_fd_.get(), &rtnl_reply, 0)) < 0) {
      PLOG(ERROR) << "recvmsg() failed on rtnetlink socket";
      return false;
    }
    for (nlmsghdr* msg_ptr = reinterpret_cast<nlmsghdr*>(reply_buffer);
         NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
      switch (msg_ptr->nlmsg_type) {
        case NLMSG_DONE: {
          done = true;
          break;
        }
        case RTM_NEWNEIGH: {
          // Bitmap - 0x1: Found IP match; 0x2: found MAC address;
          uint8_t current_entry_status = 0x0;
          uint8_t current_mac[ETHER_ADDR_LEN];
          ndmsg* nd_msg = reinterpret_cast<ndmsg*>(NLMSG_DATA(msg_ptr));
          rtattr* rt_attr = reinterpret_cast<rtattr*>(RTM_RTA(nd_msg));
          size_t rt_attr_len = RTM_PAYLOAD(msg_ptr);
          for (; RTA_OK(rt_attr, rt_attr_len);
               rt_attr = RTA_NEXT(rt_attr, rt_attr_len)) {
            if (rt_attr->rta_type == NDA_DST &&
                memcmp(&ipv6_addr, RTA_DATA(rt_attr), sizeof(in6_addr)) == 0) {
              current_entry_status |= 0x1;
            } else if (rt_attr->rta_type == NDA_LLADDR) {
              current_entry_status |= 0x2;
              memcpy(current_mac, RTA_DATA(rt_attr), ETHER_ADDR_LEN);
            }
          }
          if (current_entry_status == 0x3) {
            memcpy(mac_addr->data(), current_mac, ETHER_ADDR_LEN);
            any_entry_matched = true;
          }
          break;
        }
        default: {
          LOG(WARNING) << "received unexpected rtnetlink message type "
                       << msg_ptr->nlmsg_type << ", length "
                       << msg_ptr->nlmsg_len;
          break;
        }
      }
    }
  }
  return any_entry_matched;
}

void NDProxy::RegisterOnGuestIpDiscoveryHandler(
    const base::Callback<void(const std::string&, const std::string&)>&
        handler) {
  guest_discovery_handler_ = handler;
}

void NDProxy::RegisterOnRouterDiscoveryHandler(
    const base::Callback<void(const std::string&, const std::string&)>&
        handler) {
  router_discovery_handler_ = handler;
}

NDProxy::interface_mapping* NDProxy::MapForType(uint8_t type) {
  switch (type) {
    case ND_ROUTER_SOLICIT:
      return &if_map_rs_;
    case ND_ROUTER_ADVERT:
      return &if_map_ra_;
    case ND_NEIGHBOR_SOLICIT:
      return &if_map_ns_na_;
    case ND_NEIGHBOR_ADVERT:
      return &if_map_ns_na_;
    default:
      LOG(DFATAL) << "Attempt to get interface map on illegal icmpv6 type "
                  << static_cast<int>(type);
      return nullptr;
  }
}

bool NDProxy::AddInterfacePair(const std::string& ifname_physical,
                               const std::string& ifname_guest) {
  LOG(INFO) << "Adding interface pair between physical: " << ifname_physical
            << ", guest: " << ifname_guest;
  int ifid_physical = if_nametoindex(ifname_physical.c_str());
  if (ifid_physical == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_physical;
    return false;
  }
  int ifid_guest = if_nametoindex(ifname_guest.c_str());
  if (ifid_guest == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_guest;
    return false;
  }
  if (ifid_physical == ifid_guest) {
    LOG(ERROR) << "Rejected attempt to forward between same interface "
               << ifname_physical << " and " << ifname_guest;
    return false;
  }
  if_map_rs_[ifid_guest].insert(ifid_physical);
  if_map_ra_[ifid_physical].insert(ifid_guest);
  if_map_ns_na_[ifid_physical].insert(ifid_guest);
  if_map_ns_na_[ifid_guest].insert(ifid_physical);
  for (int ifid_other_guest : if_map_ra_[ifid_physical]) {
    if (ifid_other_guest != ifid_guest) {
      if_map_ns_na_[ifid_other_guest].insert(ifid_guest);
      if_map_ns_na_[ifid_guest].insert(ifid_other_guest);
    }
  }
  return true;
}

bool NDProxy::RemoveInterfacePair(const std::string& ifname_physical,
                                  const std::string& ifname_guest) {
  LOG(INFO) << "Removing interface pair between physical: " << ifname_physical
            << ", guest: " << ifname_guest;
  int ifid_physical = if_nametoindex(ifname_physical.c_str());
  if (ifid_physical == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_physical;
    return false;
  }
  int ifid_guest = if_nametoindex(ifname_guest.c_str());
  if (ifid_guest == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname_guest;
    return false;
  }
  if (ifid_physical == ifid_guest) {
    LOG(ERROR) << "Rejected attempt to forward between same interface "
               << ifname_physical << " and " << ifname_guest;
    return false;
  }
  if_map_rs_.erase(ifid_guest);
  if_map_ra_[ifid_physical].erase(ifid_guest);
  if_map_ns_na_.erase(ifid_guest);
  if_map_ns_na_[ifid_physical].erase(ifid_guest);
  for (int ifid_other_guest : if_map_ra_[ifid_physical]) {
    if_map_ns_na_[ifid_other_guest].erase(ifid_guest);
  }
  return true;
}

bool NDProxy::RemoveInterface(const std::string& ifname) {
  LOG(INFO) << "Removing physical interface " << ifname;
  int ifindex = if_nametoindex(ifname.c_str());
  if (ifindex == 0) {
    PLOG(ERROR) << "Get interface index failed on " << ifname;
    return false;
  }
  for (int ifid_guest : if_map_ra_[ifindex]) {
    if_map_rs_.erase(ifid_guest);
    if_map_ns_na_.erase(ifid_guest);
  }
  if_map_ra_.erase(ifindex);
  if_map_ns_na_.erase(ifindex);
  return true;
}

bool NDProxy::IsGuestInterface(int ifindex) {
  return if_map_rs_.find(ifindex) != if_map_rs_.end();
}

bool NDProxy::IsRouterInterface(int ifindex) {
  return if_map_ra_.find(ifindex) != if_map_ra_.end();
}

std::vector<std::string> NDProxy::GetGuestInterfaces(
    const std::string& ifname_physical) {
  std::vector<std::string> result;
  int ifid_physical = if_nametoindex(ifname_physical.c_str());
  if (ifid_physical == 0)
    return result;
  for (int ifid_guest : if_map_ra_[ifid_physical]) {
    char ifname[IFNAMSIZ];
    if_indextoname(ifid_guest, ifname);
    result.push_back(ifname);
  }
  return result;
}

NDProxyDaemon::NDProxyDaemon(base::ScopedFD control_fd)
    : msg_dispatcher_(
          std::make_unique<MessageDispatcher>(std::move(control_fd))) {}

NDProxyDaemon::~NDProxyDaemon() {}

int NDProxyDaemon::OnInit() {
  // Prevent the main process from sending us any signals.
  if (setsid() < 0) {
    PLOG(ERROR) << "Failed to created a new session with setsid: exiting";
    return EX_OSERR;
  }

  EnterChildProcessJail();

  // Register control fd callbacks
  if (msg_dispatcher_) {
    msg_dispatcher_->RegisterFailureHandler(base::Bind(
        &NDProxyDaemon::OnParentProcessExit, weak_factory_.GetWeakPtr()));
    msg_dispatcher_->RegisterDeviceMessageHandler(base::Bind(
        &NDProxyDaemon::OnDeviceMessage, weak_factory_.GetWeakPtr()));
  }

  // Initialize NDProxy and register guest IP discovery callback
  if (!proxy_.Init()) {
    PLOG(ERROR) << "Failed to initialize NDProxy internal state";
    return EX_OSERR;
  }
  proxy_.RegisterOnGuestIpDiscoveryHandler(base::Bind(
      &NDProxyDaemon::OnGuestIpDiscovery, weak_factory_.GetWeakPtr()));
  proxy_.RegisterOnRouterDiscoveryHandler(base::Bind(
      &NDProxyDaemon::OnRouterDiscovery, weak_factory_.GetWeakPtr()));

  // Initialize data fd
  fd_ = NDProxy::PreparePacketSocket();
  if (!fd_.is_valid()) {
    return EX_OSERR;
  }

  // Start watching on data fd
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd_.get(), base::Bind(&NDProxyDaemon::OnDataSocketReadReady,
                            weak_factory_.GetWeakPtr()));
  LOG(INFO) << "Started watching on packet fd...";

  return Daemon::OnInit();
}

void NDProxyDaemon::OnDataSocketReadReady() {
  proxy_.ReadAndProcessOneFrame(fd_.get());
}

void NDProxyDaemon::OnParentProcessExit() {
  LOG(ERROR) << "Quitting because the parent process died";
  Quit();
}

void NDProxyDaemon::OnDeviceMessage(const DeviceMessage& msg) {
  const std::string& dev_ifname = msg.dev_ifname();
  LOG_IF(DFATAL, dev_ifname.empty())
      << "Received DeviceMessage w/ empty dev_ifname";
  if (msg.has_teardown()) {
    if (msg.has_br_ifname()) {
      proxy_.RemoveInterfacePair(dev_ifname, msg.br_ifname());
      if (guest_if_addrs_.find(msg.br_ifname()) != guest_if_addrs_.end()) {
        SendMessage(NDProxyMessage::DEL_ADDR, msg.br_ifname(),
                    guest_if_addrs_[msg.br_ifname()]);
        guest_if_addrs_.erase(msg.br_ifname());
      }

    } else {
      auto guest_ifs = proxy_.GetGuestInterfaces(dev_ifname);
      proxy_.RemoveInterface(dev_ifname);
      for (const auto& guest_if : guest_ifs) {
        if (guest_if_addrs_.find(guest_if) != guest_if_addrs_.end()) {
          SendMessage(NDProxyMessage::DEL_ADDR, guest_if,
                      guest_if_addrs_[guest_if]);
          guest_if_addrs_.erase(guest_if);
        }
      }
    }
  } else if (msg.has_br_ifname()) {
    proxy_.AddInterfacePair(dev_ifname, msg.br_ifname());
  }
}

void NDProxyDaemon::OnGuestIpDiscovery(const std::string& ifname,
                                       const std::string& ip6addr) {
  SendMessage(NDProxyMessage::ADD_ROUTE, ifname, ip6addr);
}

void NDProxyDaemon::OnRouterDiscovery(const std::string& ifname,
                                      const std::string& ip6addr) {
  std::string current_addr = guest_if_addrs_[ifname];
  if (current_addr == ip6addr)
    return;
  if (!current_addr.empty()) {
    SendMessage(NDProxyMessage::DEL_ADDR, ifname, current_addr);
  }
  SendMessage(NDProxyMessage::ADD_ADDR, ifname, ip6addr);
  guest_if_addrs_[ifname] = ip6addr;
}

void NDProxyDaemon::SendMessage(NDProxyMessage::NDProxyEventType type,
                                const std::string& ifname,
                                const std::string& ip6addr) {
  if (!msg_dispatcher_)
    return;
  NDProxyMessage msg;
  msg.set_type(type);
  msg.set_ifname(ifname);
  msg.set_ip6addr(ip6addr);
  IpHelperMessage ipm;
  *ipm.mutable_ndproxy_message() = msg;
  msg_dispatcher_->SendMessage(ipm);
}

}  // namespace patchpanel
