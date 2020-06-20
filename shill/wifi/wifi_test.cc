// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi.h"

#include <linux/if.h>
#include <linux/netlink.h>  // Needs typedefs from sys/socket.h.
#include <netinet/ether.h>
#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/ref_counted.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dhcp/mock_dhcp_config.h"
#include "shill/dhcp/mock_dhcp_provider.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/geolocation_info.h"
#include "shill/ip_address_store.h"
#include "shill/key_value_store.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_device_info.h"
#include "shill/mock_eap_credentials.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_ipconfig.h"
#include "shill/mock_link_monitor.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_power_manager.h"
#include "shill/mock_profile.h"
#include "shill/mock_store.h"
#include "shill/net/ieee80211.h"
#include "shill/net/ip_address.h"
#include "shill/net/mock_netlink_manager.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/mock_time.h"
#include "shill/net/netlink_message_matchers.h"
#include "shill/net/netlink_packet.h"
#include "shill/net/nl80211_attribute.h"
#include "shill/net/nl80211_message.h"
#include "shill/property_store_test.h"
#include "shill/supplicant/mock_supplicant_bss_proxy.h"
#include "shill/supplicant/mock_supplicant_eap_state_handler.h"
#include "shill/supplicant/mock_supplicant_interface_proxy.h"
#include "shill/supplicant/mock_supplicant_network_proxy.h"
#include "shill/supplicant/mock_supplicant_process_proxy.h"
#include "shill/supplicant/supplicant_manager.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/wifi/mock_mac80211_monitor.h"
#include "shill/wifi/mock_tdls_manager.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi_provider.h"
#include "shill/wifi/mock_wifi_service.h"
#include "shill/wifi/wake_on_wifi.h"
#include "shill/wifi/wifi_endpoint.h"
#include "shill/wifi/wifi_service.h"

using base::StringPrintf;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::ContainsRegex;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Test;

namespace shill {

namespace {

const uint16_t kNl80211FamilyId = 0x13;
const uint16_t kRandomScanFrequency1 = 5600;
const uint16_t kRandomScanFrequency2 = 5560;
const uint16_t kRandomScanFrequency3 = 2422;
const int kInterfaceIndex = 1234;

// Bytes representing a NL80211_CMD_NEW_WIPHY message reporting the WiFi
// capabilities of a NIC with wiphy index |kNewWiphyNlMsg_WiphyIndex| which
// supports operating bands with the frequencies specified in
// |kNewWiphyNlMsg_UniqueFrequencies|.
const uint8_t kNewWiphyNlMsg[] = {
    0x68, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf6, 0x31, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2e, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x3d, 0x00, 0x07, 0x00, 0x00, 0x00, 0x05, 0x00, 0x3e, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x3f, 0x00, 0xff, 0xff, 0xff, 0xff,
    0x08, 0x00, 0x40, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x59, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x7b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x38, 0x00,
    0xd1, 0x08, 0x00, 0x00, 0x06, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x68, 0x00,
    0x04, 0x00, 0x8b, 0x00, 0x04, 0x00, 0x8c, 0x00, 0x18, 0x00, 0x39, 0x00,
    0x01, 0xac, 0x0f, 0x00, 0x05, 0xac, 0x0f, 0x00, 0x02, 0xac, 0x0f, 0x00,
    0x04, 0xac, 0x0f, 0x00, 0x06, 0xac, 0x0f, 0x00, 0x05, 0x00, 0x56, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x66, 0x00, 0x08, 0x00, 0x71, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x72, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x69, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x6a, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x24, 0x00, 0x20, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0x04, 0x00, 0x08, 0x00,
    0x04, 0x00, 0x09, 0x00, 0x50, 0x05, 0x16, 0x00, 0xf8, 0x01, 0x00, 0x00,
    0x14, 0x00, 0x03, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00,
    0xef, 0x11, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00, 0x28, 0x01, 0x01, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00, 0x6c, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x71, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x76, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00, 0x7b, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x80, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x05, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x85, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x06, 0x00, 0x08, 0x00, 0x01, 0x00, 0x8a, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x07, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x8f, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x94, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x09, 0x00, 0x08, 0x00, 0x01, 0x00, 0x99, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x6c, 0x07, 0x00, 0x00, 0x14, 0x00, 0x0a, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x9e, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6c, 0x07, 0x00, 0x00, 0x18, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xa3, 0x09, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6c, 0x07, 0x00, 0x00, 0x18, 0x00, 0x0c, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xa8, 0x09, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x6c, 0x07, 0x00, 0x00, 0x18, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xb4, 0x09, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0xa0, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
    0x10, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x37, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x10, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x6e, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x06, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x78, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x07, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x08, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x09, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x68, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x0a, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x0b, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x1c, 0x02, 0x00, 0x00, 0x54, 0x03, 0x01, 0x00,
    0x14, 0x00, 0x03, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00,
    0xef, 0x11, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00, 0xc0, 0x02, 0x01, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00, 0x3c, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x1c, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x50, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x64, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x78, 0x14, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x8c, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x20, 0x00, 0x05, 0x00, 0x08, 0x00, 0x01, 0x00, 0xa0, 0x14, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x06, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xb4, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x07, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xc8, 0x14, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x20, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0x00, 0x7c, 0x15, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x09, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x90, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xa4, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x20, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x01, 0x00, 0xb8, 0x15, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x0c, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xcc, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x01, 0x00,
    0xe0, 0x15, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x20, 0x00, 0x0e, 0x00, 0x08, 0x00, 0x01, 0x00, 0xf4, 0x15, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x0f, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x08, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x10, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x1c, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x20, 0x00, 0x11, 0x00, 0x08, 0x00, 0x01, 0x00, 0x30, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x20, 0x00, 0x12, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x44, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x14, 0x00, 0x13, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x71, 0x16, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x1c, 0x00, 0x14, 0x00, 0x08, 0x00, 0x01, 0x00, 0x85, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x1c, 0x00, 0x15, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x99, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x1c, 0x00, 0x16, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xad, 0x16, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x08, 0x00, 0x06, 0x00, 0xd0, 0x07, 0x00, 0x00,
    0x1c, 0x00, 0x17, 0x00, 0x08, 0x00, 0x01, 0x00, 0xc1, 0x16, 0x00, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00, 0x08, 0x00, 0x06, 0x00,
    0xd0, 0x07, 0x00, 0x00, 0x64, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x02, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x78, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x68, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x06, 0x00,
    0x08, 0x00, 0x01, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x07, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x1c, 0x02, 0x00, 0x00, 0xd4, 0x00, 0x32, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x07, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x08, 0x00, 0x05, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x07, 0x00, 0x25, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00,
    0x26, 0x00, 0x00, 0x00, 0x08, 0x00, 0x09, 0x00, 0x27, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x0a, 0x00, 0x28, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0b, 0x00,
    0x2b, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0c, 0x00, 0x37, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x0d, 0x00, 0x39, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0e, 0x00,
    0x3b, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0f, 0x00, 0x43, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x10, 0x00, 0x31, 0x00, 0x00, 0x00, 0x08, 0x00, 0x11, 0x00,
    0x41, 0x00, 0x00, 0x00, 0x08, 0x00, 0x12, 0x00, 0x42, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x13, 0x00, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x14, 0x00,
    0x51, 0x00, 0x00, 0x00, 0x08, 0x00, 0x15, 0x00, 0x54, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x16, 0x00, 0x57, 0x00, 0x00, 0x00, 0x08, 0x00, 0x17, 0x00,
    0x55, 0x00, 0x00, 0x00, 0x08, 0x00, 0x18, 0x00, 0x2d, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x19, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x08, 0x00, 0x1a, 0x00,
    0x30, 0x00, 0x00, 0x00, 0x08, 0x00, 0x6f, 0x00, 0x88, 0x13, 0x00, 0x00,
    0x04, 0x00, 0x6c, 0x00, 0xac, 0x03, 0x63, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x01, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xe0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x02, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xe0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x03, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xe0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00,
    0x84, 0x00, 0x04, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xe0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0x84, 0x00, 0x07, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xb0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xe0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x84, 0x00, 0x08, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xb0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xe0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x84, 0x00, 0x09, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x60, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x90, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xb0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xd0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xe0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x14, 0x01, 0x64, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xd0, 0x00, 0x00, 0x00, 0x14, 0x00, 0x02, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00,
    0x3c, 0x00, 0x03, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xc0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00,
    0x3c, 0x00, 0x04, 0x00, 0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xc0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0x1c, 0x00, 0x07, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xc0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x08, 0x00, 0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x09, 0x00,
    0x06, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00,
    0xb0, 0x00, 0x00, 0x00, 0x06, 0x00, 0x65, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x65, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x79, 0x00,
    0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x50, 0x00, 0x78, 0x00,
    0x4c, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00, 0x1c, 0x00, 0x01, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x08, 0x00,
    0x18, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x02, 0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x09, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x8f, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x94, 0x00, 0x42, 0x08, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint32_t kNewWiphyNlMsg_WiphyIndex = 2;
const int kNewWiphyNlMsg_Nl80211AttrWiphyOffset = 4;
const uint16_t kNewWiphyNlMsg_UniqueFrequencies[] = {
    2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457,
    2462, 2467, 2472, 2484, 5180, 5200, 5220, 5240, 5260, 5280,
    5300, 5320, 5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640,
    5660, 5680, 5700, 5745, 5765, 5785, 5805, 5825};

const uint32_t kScanTriggerMsgWiphyIndex = 0;
const uint8_t kActiveScanTriggerNlMsg[] = {
    0x44, 0x01, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x21, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x99, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x2d, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x01, 0x2c, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x6c, 0x09, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x71, 0x09, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x76, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x03, 0x00, 0x7b, 0x09, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00,
    0x80, 0x09, 0x00, 0x00, 0x08, 0x00, 0x05, 0x00, 0x85, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x8a, 0x09, 0x00, 0x00, 0x08, 0x00, 0x07, 0x00,
    0x8f, 0x09, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x94, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x09, 0x00, 0x99, 0x09, 0x00, 0x00, 0x08, 0x00, 0x0a, 0x00,
    0x9e, 0x09, 0x00, 0x00, 0x08, 0x00, 0x0b, 0x00, 0x3c, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x0c, 0x00, 0x50, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0d, 0x00,
    0x64, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0e, 0x00, 0x78, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x0f, 0x00, 0x8c, 0x14, 0x00, 0x00, 0x08, 0x00, 0x10, 0x00,
    0xa0, 0x14, 0x00, 0x00, 0x08, 0x00, 0x11, 0x00, 0xb4, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x12, 0x00, 0xc8, 0x14, 0x00, 0x00, 0x08, 0x00, 0x13, 0x00,
    0x7c, 0x15, 0x00, 0x00, 0x08, 0x00, 0x14, 0x00, 0x90, 0x15, 0x00, 0x00,
    0x08, 0x00, 0x15, 0x00, 0xa4, 0x15, 0x00, 0x00, 0x08, 0x00, 0x16, 0x00,
    0xb8, 0x15, 0x00, 0x00, 0x08, 0x00, 0x17, 0x00, 0xcc, 0x15, 0x00, 0x00,
    0x08, 0x00, 0x18, 0x00, 0x1c, 0x16, 0x00, 0x00, 0x08, 0x00, 0x19, 0x00,
    0x30, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1a, 0x00, 0x44, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x1b, 0x00, 0x58, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1c, 0x00,
    0x71, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x85, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x1e, 0x00, 0x99, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1f, 0x00,
    0xad, 0x16, 0x00, 0x00, 0x08, 0x00, 0x20, 0x00, 0xc1, 0x16, 0x00, 0x00};

const uint8_t kPassiveScanTriggerNlMsg[] = {
    0x40, 0x01, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x21, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x99, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x2d, 0x00, 0x0c, 0x01, 0x2c, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x6c, 0x09, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00, 0x71, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x76, 0x09, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00,
    0x7b, 0x09, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00, 0x80, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x05, 0x00, 0x85, 0x09, 0x00, 0x00, 0x08, 0x00, 0x06, 0x00,
    0x8a, 0x09, 0x00, 0x00, 0x08, 0x00, 0x07, 0x00, 0x8f, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x08, 0x00, 0x94, 0x09, 0x00, 0x00, 0x08, 0x00, 0x09, 0x00,
    0x99, 0x09, 0x00, 0x00, 0x08, 0x00, 0x0a, 0x00, 0x9e, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x0b, 0x00, 0x3c, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0c, 0x00,
    0x50, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0d, 0x00, 0x64, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x0e, 0x00, 0x78, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0f, 0x00,
    0x8c, 0x14, 0x00, 0x00, 0x08, 0x00, 0x10, 0x00, 0xa0, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x11, 0x00, 0xb4, 0x14, 0x00, 0x00, 0x08, 0x00, 0x12, 0x00,
    0xc8, 0x14, 0x00, 0x00, 0x08, 0x00, 0x13, 0x00, 0x7c, 0x15, 0x00, 0x00,
    0x08, 0x00, 0x14, 0x00, 0x90, 0x15, 0x00, 0x00, 0x08, 0x00, 0x15, 0x00,
    0xa4, 0x15, 0x00, 0x00, 0x08, 0x00, 0x16, 0x00, 0xb8, 0x15, 0x00, 0x00,
    0x08, 0x00, 0x17, 0x00, 0xcc, 0x15, 0x00, 0x00, 0x08, 0x00, 0x18, 0x00,
    0x1c, 0x16, 0x00, 0x00, 0x08, 0x00, 0x19, 0x00, 0x30, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x1a, 0x00, 0x44, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1b, 0x00,
    0x58, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1c, 0x00, 0x71, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x1d, 0x00, 0x85, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1e, 0x00,
    0x99, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1f, 0x00, 0xad, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x20, 0x00, 0xc1, 0x16, 0x00, 0x00};

}  // namespace

class WiFiPropertyTest : public PropertyStoreTest {
 public:
  WiFiPropertyTest()
      : device_(new WiFi(manager(),
                         "wifi",
                         "",
                         kInterfaceIndex,
                         std::make_unique<MockWakeOnWiFi>())) {}
  ~WiFiPropertyTest() override = default;

 protected:
  MockMetrics metrics_;
  MockNetlinkManager netlink_manager_;
  WiFiRefPtr device_;
};

TEST_F(WiFiPropertyTest, Contains) {
  EXPECT_TRUE(device_->store().Contains(kNameProperty));
  EXPECT_FALSE(device_->store().Contains(""));
}

TEST_F(WiFiPropertyTest, SetProperty) {
  {
    Error error;
    EXPECT_TRUE(device_->mutable_store()->SetAnyProperty(
        kBgscanSignalThresholdProperty, PropertyStoreTest::kInt32V, &error));
  }
  {
    Error error;
    EXPECT_TRUE(device_->mutable_store()->SetAnyProperty(
        kScanIntervalProperty, PropertyStoreTest::kUint16V, &error));
  }
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  {
    Error error;
    EXPECT_FALSE(device_->mutable_store()->SetAnyProperty(
        kScanningProperty, PropertyStoreTest::kBoolV, &error));
    ASSERT_TRUE(error.IsFailure());
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }

  {
    Error error;
    EXPECT_TRUE(device_->mutable_store()->SetAnyProperty(
        kBgscanMethodProperty,
        brillo::Any(string(WPASupplicant::kNetworkBgscanMethodSimple)),
        &error));
  }

  {
    Error error;
    EXPECT_FALSE(device_->mutable_store()->SetAnyProperty(
        kBgscanMethodProperty, brillo::Any(string("not a real scan method")),
        &error));
  }
}

TEST_F(WiFiPropertyTest, BgscanMethodProperty) {
  EXPECT_NE(WPASupplicant::kNetworkBgscanMethodLearn,
            WiFi::kDefaultBgscanMethod);
  EXPECT_TRUE(device_->bgscan_method_.empty());

  string method;
  Error unused_error;
  EXPECT_TRUE(device_->store().GetStringProperty(kBgscanMethodProperty, &method,
                                                 &unused_error));
  EXPECT_EQ(WiFi::kDefaultBgscanMethod, method);
  EXPECT_EQ(WPASupplicant::kNetworkBgscanMethodSimple, method);

  Error error;
  EXPECT_TRUE(device_->mutable_store()->SetAnyProperty(
      kBgscanMethodProperty,
      brillo::Any(string(WPASupplicant::kNetworkBgscanMethodLearn)), &error));
  EXPECT_EQ(WPASupplicant::kNetworkBgscanMethodLearn, device_->bgscan_method_);
  EXPECT_TRUE(device_->store().GetStringProperty(kBgscanMethodProperty, &method,
                                                 &unused_error));
  EXPECT_EQ(WPASupplicant::kNetworkBgscanMethodLearn, method);

  EXPECT_TRUE(
      device_->mutable_store()->ClearProperty(kBgscanMethodProperty, &error));
  EXPECT_TRUE(device_->store().GetStringProperty(kBgscanMethodProperty, &method,
                                                 &unused_error));
  EXPECT_EQ(WiFi::kDefaultBgscanMethod, method);
  EXPECT_TRUE(device_->bgscan_method_.empty());
}

MATCHER_P(EndpointMatch, endpoint, "") {
  return arg->ssid() == endpoint->ssid() &&
         arg->network_mode() == endpoint->network_mode() &&
         arg->security_mode() == endpoint->security_mode();
}

class WiFiObjectTest : public ::testing::TestWithParam<string> {
 public:
  explicit WiFiObjectTest(std::unique_ptr<EventDispatcher> dispatcher)
      : event_dispatcher_(std::move(dispatcher)),
        manager_(&control_interface_, event_dispatcher_.get(), &metrics_),
        power_manager_(new MockPowerManager(control_interface())),
        device_info_(&manager_),
        wifi_(new WiFi(&manager_,
                       kDeviceName,
                       kDeviceAddress,
                       kInterfaceIndex,
                       std::make_unique<MockWakeOnWiFi>())),
        bss_counter_(0),
        mac80211_monitor_(new StrictMock<MockMac80211Monitor>(
            event_dispatcher_.get(),
            kDeviceName,
            WiFi::kStuckQueueLengthThreshold,
            base::Closure(),
            &metrics_)),
        supplicant_process_proxy_(new NiceMock<MockSupplicantProcessProxy>()),
        supplicant_bss_proxy_(new NiceMock<MockSupplicantBSSProxy>()),
        dhcp_config_(new MockDHCPConfig(&control_interface_, kDeviceName)),
        adaptor_(new DeviceMockAdaptor()),
        eap_state_handler_(new NiceMock<MockSupplicantEAPStateHandler>()),
        supplicant_interface_proxy_(
            new NiceMock<MockSupplicantInterfaceProxy>()),
        supplicant_network_proxy_(new NiceMock<MockSupplicantNetworkProxy>()) {
    wifi_->mac80211_monitor_.reset(mac80211_monitor_);
    manager_.supplicant_manager()->set_proxy(supplicant_process_proxy_);
    ON_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(RpcIdentifier("/default/path")),
                             Return(true)));
    ON_CALL(*supplicant_process_proxy_, GetInterface(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(RpcIdentifier("/default/path")),
                             Return(true)));
    ON_CALL(*supplicant_interface_proxy_, AddNetwork(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(RpcIdentifier("/default/path")),
                             Return(true)));
    ON_CALL(*supplicant_interface_proxy_, Disconnect())
        .WillByDefault(Return(true));
    ON_CALL(*supplicant_interface_proxy_, RemoveNetwork(_))
        .WillByDefault(Return(true));
    ON_CALL(*supplicant_interface_proxy_, Scan(_)).WillByDefault(Return(true));
    ON_CALL(*supplicant_interface_proxy_, EnableMacAddressRandomization(_, _))
        .WillByDefault(Return(true));
    ON_CALL(*supplicant_interface_proxy_, DisableMacAddressRandomization())
        .WillByDefault(Return(true));
    ON_CALL(*supplicant_network_proxy_, SetEnabled(_))
        .WillByDefault(Return(true));

    EXPECT_CALL(*mac80211_monitor_, UpdateConnectedState(_)).Times(AnyNumber());

    ON_CALL(dhcp_provider_, CreateIPv4Config(_, _, _, _))
        .WillByDefault(Return(dhcp_config_));
    ON_CALL(*dhcp_config_, RequestIP()).WillByDefault(Return(true));
    ON_CALL(*manager(), IsSuspending()).WillByDefault(Return(false));

    ON_CALL(control_interface_, CreateSupplicantInterfaceProxy(_, _))
        .WillByDefault(
            Invoke(this, &WiFiObjectTest::CreateSupplicantInterfaceProxy));
    ON_CALL(control_interface_, CreateSupplicantBSSProxy(_, _))
        .WillByDefault(Invoke(this, &WiFiObjectTest::CreateSupplicantBSSProxy));
    ON_CALL(control_interface_, CreateSupplicantNetworkProxy(_))
        .WillByDefault(
            Invoke(this, &WiFiObjectTest::CreateSupplicantNetworkProxy));
    Nl80211Message::SetMessageType(kNl80211FamilyId);

    // Transfers ownership.
    wifi_->eap_state_handler_.reset(eap_state_handler_);

    wifi_->provider_ = &wifi_provider_;
    wifi_->time_ = &time_;
    wifi_->netlink_manager_ = &netlink_manager_;
    wifi_->adaptor_.reset(adaptor_);  // Transfers ownership.

    manager_.set_power_manager(power_manager_);  // Transfers ownership.

    // The following is only useful when a real |ScanSession| is used; it is
    // ignored by |MockScanSession|.
    wifi_->all_scan_frequencies_.insert(kRandomScanFrequency1);
    wifi_->all_scan_frequencies_.insert(kRandomScanFrequency2);
    wifi_->all_scan_frequencies_.insert(kRandomScanFrequency3);

    wake_on_wifi_ = static_cast<MockWakeOnWiFi*>(wifi_->wake_on_wifi_.get());
  }

  void SetUp() override {
    // EnableScopes... so that we can EXPECT_CALL for scoped log messages.
    ScopeLogger::GetInstance()->EnableScopesByName("wifi");
    ScopeLogger::GetInstance()->set_verbose_level(3);
    static_cast<Device*>(wifi_.get())->rtnl_handler_ = &rtnl_handler_;
    wifi_->set_dhcp_provider(&dhcp_provider_);
    ON_CALL(manager_, device_info()).WillByDefault(Return(&device_info_));
    EXPECT_CALL(manager_, UpdateEnabledTechnologies()).Times(AnyNumber());
    EXPECT_CALL(*supplicant_bss_proxy_, Die()).Times(AnyNumber());
  }

  void TearDown() override {
    EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(_))
        .WillRepeatedly(Return(nullptr));
    wifi_->SelectService(nullptr);
    if (supplicant_bss_proxy_) {
      EXPECT_CALL(*supplicant_bss_proxy_, Die());
    }
    EXPECT_CALL(*mac80211_monitor_, Stop());
    // must Stop WiFi instance, to clear its list of services.
    // otherwise, the WiFi instance will not be deleted. (because
    // services reference a WiFi instance, creating a cycle.)
    wifi_->Stop(nullptr, ResultCallback());
    wifi_->set_dhcp_provider(nullptr);
    // Reset scope logging, to avoid interfering with other tests.
    ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
    ScopeLogger::GetInstance()->set_verbose_level(0);
  }

  // Needs to be public since it is called via Invoke().
  void StopWiFi() {
    EXPECT_CALL(*mac80211_monitor_, Stop());
    wifi_->SetEnabled(false);  // Stop(nullptr, ResultCallback());
  }

  void ResetPendingService() { SetPendingService(nullptr); }

  void SetScanState(WiFi::ScanState new_state,
                    WiFi::ScanMethod new_method,
                    const char* reason) {
    wifi_->SetScanState(new_state, new_method, reason);
  }

  void VerifyScanState(WiFi::ScanState state, WiFi::ScanMethod method) const {
    EXPECT_EQ(state, wifi_->scan_state_);
    EXPECT_EQ(method, wifi_->scan_method_);
  }

 protected:
  using MockWiFiServiceRefPtr = scoped_refptr<MockWiFiService>;

  // Simulate the course of events when the last endpoint of a service is
  // removed.
  class EndpointRemovalHandler {
   public:
    EndpointRemovalHandler(WiFiRefPtr wifi, const WiFiServiceRefPtr& service)
        : wifi_(wifi), service_(service) {}
    virtual ~EndpointRemovalHandler() = default;

    WiFiServiceRefPtr OnEndpointRemoved(
        const WiFiEndpointConstRefPtr& endpoint) {
      wifi_->DisassociateFromService(service_);
      return service_;
    }

   private:
    WiFiRefPtr wifi_;
    WiFiServiceRefPtr service_;
  };

  unique_ptr<EndpointRemovalHandler> MakeEndpointRemovalHandler(
      const WiFiServiceRefPtr& service) {
    return std::make_unique<EndpointRemovalHandler>(wifi_, service);
  }

  void CancelScanTimer() { wifi_->scan_timer_callback_.Cancel(); }
  // This function creates a new endpoint.  We synthesize new |path| and
  // |bssid| values, since we don't really care what they are for unit tests.
  // If "use_ssid" is true, we used the passed-in ssid, otherwise we create a
  // synthesized value for it as well.
  WiFiEndpointRefPtr MakeNewEndpoint(bool use_ssid,
                                     string* ssid,
                                     RpcIdentifier* path,
                                     string* bssid) {
    bss_counter_++;
    if (!use_ssid) {
      *ssid = StringPrintf("ssid%d", bss_counter_);
    }
    *path = RpcIdentifier(StringPrintf("/interface/bss%d", bss_counter_));
    *bssid = StringPrintf("00:00:00:00:00:%02x", bss_counter_);
    WiFiEndpointRefPtr endpoint = MakeEndpoint(*ssid, *bssid);
    EXPECT_CALL(wifi_provider_, OnEndpointAdded(EndpointMatch(endpoint)))
        .Times(1);
    return endpoint;
  }
  WiFiEndpointRefPtr MakeEndpoint(const string& ssid, const string& bssid) {
    return MakeEndpointWithMode(ssid, bssid, kNetworkModeInfrastructure);
  }
  WiFiEndpointRefPtr MakeEndpointWithMode(const string& ssid,
                                          const string& bssid,
                                          const string& mode) {
    return WiFiEndpoint::MakeOpenEndpoint(&control_interface_, nullptr, ssid,
                                          bssid, mode, 0, 0);
  }
  MockWiFiServiceRefPtr MakeMockServiceWithSSID(vector<uint8_t> ssid,
                                                const std::string& security) {
    return new NiceMock<MockWiFiService>(&manager_, &wifi_provider_, ssid,
                                         kModeManaged, security, false);
  }
  MockWiFiServiceRefPtr MakeMockService(const std::string& security) {
    return MakeMockServiceWithSSID(vector<uint8_t>(1, 'a'), security);
  }
  RpcIdentifier MakeNewEndpointAndService(int16_t signal_strength,
                                          uint16_t frequency,
                                          WiFiEndpointRefPtr* endpoint_ptr,
                                          MockWiFiServiceRefPtr* service_ptr) {
    string ssid;
    RpcIdentifier path;
    string bssid;
    WiFiEndpointRefPtr endpoint = MakeNewEndpoint(false, &ssid, &path, &bssid);
    MockWiFiServiceRefPtr service =
        MakeMockServiceWithSSID(endpoint->ssid(), endpoint->security_mode());
    EXPECT_CALL(wifi_provider_, FindServiceForEndpoint(EndpointMatch(endpoint)))
        .WillRepeatedly(Return(service));
    ON_CALL(*service, GetEndpointCount()).WillByDefault(Return(1));
    ReportBSS(path, ssid, bssid, signal_strength, frequency,
              kNetworkModeInfrastructure);
    if (service_ptr) {
      *service_ptr = service;
    }
    if (endpoint_ptr) {
      *endpoint_ptr = endpoint;
    }
    return path;
  }
  RpcIdentifier AddEndpointToService(WiFiServiceRefPtr service,
                                     int16_t signal_strength,
                                     uint16_t frequency,
                                     WiFiEndpointRefPtr* endpoint_ptr) {
    string ssid(service->ssid().begin(), service->ssid().end());
    RpcIdentifier path;
    string bssid;
    WiFiEndpointRefPtr endpoint = MakeNewEndpoint(true, &ssid, &path, &bssid);
    EXPECT_CALL(wifi_provider_, FindServiceForEndpoint(EndpointMatch(endpoint)))
        .WillRepeatedly(Return(service));
    ReportBSS(path, ssid, bssid, signal_strength, frequency,
              kNetworkModeInfrastructure);
    if (endpoint_ptr) {
      *endpoint_ptr = endpoint;
    }
    return path;
  }
  void InitiateConnect(WiFiServiceRefPtr service) {
    Error error;
    wifi_->ConnectTo(service.get(), &error);
  }
  void InitiateDisconnect(WiFiServiceRefPtr service) {
    wifi_->DisconnectFrom(service.get());
  }
  void InitiateDisconnectIfActive(WiFiServiceRefPtr service) {
    wifi_->DisconnectFromIfActive(service.get());
  }
  MockWiFiServiceRefPtr SetupConnectingService(
      const RpcIdentifier& network_path,
      WiFiEndpointRefPtr* endpoint_ptr,
      RpcIdentifier* bss_path_ptr) {
    MockWiFiServiceRefPtr service;
    WiFiEndpointRefPtr endpoint;
    RpcIdentifier bss_path(
        MakeNewEndpointAndService(0, 0, &endpoint, &service));
    if (!network_path.value().empty()) {
      EXPECT_CALL(*service, GetSupplicantConfigurationParameters());
      EXPECT_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _))
          .WillOnce(DoAll(SetArgPointee<1>(network_path), Return(true)));
      EXPECT_CALL(*GetSupplicantInterfaceProxy(), SelectNetwork(network_path));
    }
    EXPECT_CALL(*service, SetState(Service::kStateAssociating));
    InitiateConnect(service);
    Mock::VerifyAndClearExpectations(service.get());
    EXPECT_FALSE(GetPendingTimeout().IsCancelled());
    if (endpoint_ptr) {
      *endpoint_ptr = endpoint;
    }
    if (bss_path_ptr) {
      *bss_path_ptr = bss_path;
    }
    return service;
  }

  MockWiFiServiceRefPtr SetupConnectedService(const RpcIdentifier& network_path,
                                              WiFiEndpointRefPtr* endpoint_ptr,
                                              RpcIdentifier* bss_path_ptr) {
    WiFiEndpointRefPtr endpoint;
    RpcIdentifier bss_path;
    MockWiFiServiceRefPtr service =
        SetupConnectingService(network_path, &endpoint, &bss_path);
    if (endpoint_ptr) {
      *endpoint_ptr = endpoint;
    }
    if (bss_path_ptr) {
      *bss_path_ptr = bss_path;
    }
    EXPECT_CALL(*service, NotifyCurrentEndpoint(EndpointMatch(endpoint)));
    ReportCurrentBSSChanged(bss_path);
    EXPECT_TRUE(GetPendingTimeout().IsCancelled());
    Mock::VerifyAndClearExpectations(service.get());

    EXPECT_CALL(*service, SetState(Service::kStateConfiguring));
    EXPECT_CALL(*service, ResetSuspectedCredentialFailures());
    EXPECT_CALL(*dhcp_provider(), CreateIPv4Config(_, _, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(*dhcp_config_, RequestIP()).Times(AnyNumber());
    ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
    Mock::VerifyAndClearExpectations(service.get());

    EXPECT_EQ(service, GetCurrentService());
    return service;
  }

  void FireScanTimer() { wifi_->ScanTimerHandler(); }
  void TriggerScan() { wifi_->Scan(nullptr, __func__); }
  const WiFiServiceRefPtr& GetCurrentService() {
    return wifi_->current_service_;
  }
  void SetCurrentService(const WiFiServiceRefPtr& service) {
    wifi_->current_service_ = service;
  }
  const WiFi::EndpointMap& GetEndpointMap() {
    return wifi_->endpoint_by_rpcid_;
  }
  const WiFiServiceRefPtr& GetPendingService() {
    return wifi_->pending_service_;
  }
  const base::CancelableClosure& GetPendingTimeout() {
    return wifi_->pending_timeout_callback_;
  }
  const base::CancelableClosure& GetReconnectTimeoutCallback() {
    return wifi_->reconnect_timeout_callback_;
  }
  const ServiceRefPtr& GetSelectedService() {
    return wifi_->selected_service();
  }
  const RpcIdentifier& GetSupplicantBSS() { return wifi_->supplicant_bss_; }
  void SetSupplicantBSS(const RpcIdentifier& bss) {
    wifi_->supplicant_bss_ = bss;
  }
  int GetReconnectTimeoutSeconds() { return WiFi::kReconnectTimeoutSeconds; }
  const base::CancelableClosure& GetScanTimer() {
    return wifi_->scan_timer_callback_;
  }
  // note: the tests need the proxies referenced by WiFi (not the
  // proxies instantiated by WiFiObjectTest), to ensure that WiFi
  // sets up its proxies correctly.
  MockSupplicantInterfaceProxy* GetSupplicantInterfaceProxyFromWiFi() {
    return static_cast<MockSupplicantInterfaceProxy*>(
        wifi_->supplicant_interface_proxy_.get());
  }
  // This function returns the supplicant interface proxy whether
  // or not we have passed the instantiated object to the WiFi instance
  // from WiFiObjectTest, so tests don't need to worry about when they
  // set expectations relative to StartWiFi().
  MockSupplicantInterfaceProxy* GetSupplicantInterfaceProxy() {
    MockSupplicantInterfaceProxy* proxy = GetSupplicantInterfaceProxyFromWiFi();
    return proxy ? proxy : supplicant_interface_proxy_.get();
  }
  const string& GetSupplicantState() { return wifi_->supplicant_state_; }
  IEEE_80211::WiFiReasonCode GetSupplicantDisconnectReason() {
    return wifi_->supplicant_disconnect_reason_;
  }
  void ClearCachedCredentials(const WiFiService* service) {
    return wifi_->ClearCachedCredentials(service);
  }
  void NotifyEndpointChanged(const WiFiEndpointConstRefPtr& endpoint) {
    wifi_->NotifyEndpointChanged(endpoint);
  }
  bool RemoveNetwork(const RpcIdentifier& network) {
    return wifi_->RemoveNetwork(network);
  }
  KeyValueStore CreateBSSProperties(const string& ssid,
                                    const string& bssid,
                                    int16_t signal_strength,
                                    uint16_t frequency,
                                    const char* mode);
  void RemoveBSS(const RpcIdentifier& bss_path);
  void ReportBSS(const RpcIdentifier& bss_path,
                 const string& ssid,
                 const string& bssid,
                 int16_t signal_strength,
                 uint16_t frequency,
                 const char* mode);
  void ReportIPConfigComplete() {
    wifi_->OnIPConfigUpdated(dhcp_config_, true);
  }
  void ReportIPConfigCompleteGatewayArpReceived() {
    wifi_->OnIPConfigUpdated(dhcp_config_, false);
  }

  // Calls the delayed version of the BSS methods.
  void BSSAdded(const RpcIdentifier& bss_path,
                const KeyValueStore& properties) {
    wifi_->BSSAdded(bss_path, properties);
  }
  void BSSRemoved(const RpcIdentifier& bss_path) {
    wifi_->BSSRemoved(bss_path);
  }

  void ReportIPv6ConfigComplete() { wifi_->OnIPv6ConfigUpdated(); }
  void ReportIPConfigFailure() { wifi_->OnIPConfigFailure(); }
  void ReportConnected() { wifi_->OnConnected(); }
  void ReportLinkUp() { wifi_->LinkEvent(IFF_LOWER_UP, IFF_LOWER_UP); }
  void ScanDone(const bool& success) { wifi_->ScanDone(success); }
  void ReportScanFailed() { wifi_->ScanFailedTask(); }
  void ReportScanDone() { wifi_->ScanDoneTask(); }
  void ReportCurrentBSSChanged(const RpcIdentifier& new_bss) {
    wifi_->CurrentBSSChanged(new_bss);
  }
  void ReportStateChanged(const string& new_state) {
    wifi_->StateChanged(new_state);
  }
  void ReportDisconnectReasonChanged(int32_t reason) {
    wifi_->DisconnectReasonChanged(reason);
  }
  void ReportCurrentAuthModeChanged(const string& auth_mode) {
    wifi_->CurrentAuthModeChanged(auth_mode);
  }
  void ReportWiFiDebugScopeChanged(bool enabled) {
    wifi_->OnWiFiDebugScopeChanged(enabled);
  }
  void RequestStationInfo() { wifi_->RequestStationInfo(); }
  void ReportReceivedStationInfo(const Nl80211Message& nl80211_message) {
    wifi_->OnReceivedStationInfo(nl80211_message);
  }
  KeyValueStore GetLinkStatistics() {
    return wifi_->GetLinkStatistics(nullptr);
  }
  void SetPendingService(const WiFiServiceRefPtr& service) {
    wifi_->SetPendingService(service);
  }
  void SetServiceNetworkRpcId(const WiFiServiceRefPtr& service,
                              const RpcIdentifier& rpcid) {
    wifi_->rpcid_by_service_[service.get()] = rpcid;
  }
  bool RpcIdByServiceIsEmpty() { return wifi_->rpcid_by_service_.empty(); }
  bool SetScanInterval(uint16_t interval_seconds, Error* error) {
    return wifi_->SetScanInterval(interval_seconds, error);
  }
  uint16_t GetScanInterval() { return wifi_->GetScanInterval(nullptr); }
  void StartWiFi(bool supplicant_present) {
    EXPECT_CALL(netlink_manager_,
                SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                  NetlinkManager::kEventTypeConfig));
    EXPECT_CALL(netlink_manager_,
                SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                  NetlinkManager::kEventTypeScan));
    EXPECT_CALL(netlink_manager_,
                SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                  NetlinkManager::kEventTypeRegulatory));
    EXPECT_CALL(netlink_manager_,
                SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                  NetlinkManager::kEventTypeMlme));
    EXPECT_CALL(netlink_manager_,
                SendNl80211Message(
                    IsNl80211Command(kNl80211FamilyId, NL80211_CMD_GET_WIPHY),
                    _, _, _));

    wifi_->supplicant_present_ = supplicant_present;
    wifi_->SetEnabled(true);  // Start(nullptr, ResultCallback());
    if (supplicant_present)
      // Mimic the callback from |supplicant_process_proxy_|.
      wifi_->OnSupplicantPresence(true);
  }
  void StartWiFi() { StartWiFi(true); }
  void OnAfterResume() {
    if (wifi_->enabled_)
      EXPECT_CALL(*wake_on_wifi_, OnAfterResume());
    wifi_->OnAfterResume();
  }
  void OnBeforeSuspend() {
    ResultCallback callback(
        base::Bind(&WiFiObjectTest::SuspendCallback, base::Unretained(this)));
    wifi_->OnBeforeSuspend(callback);
  }
  void OnDarkResume() {
    ResultCallback callback(
        base::Bind(&WiFiObjectTest::SuspendCallback, base::Unretained(this)));
    wifi_->OnDarkResume(callback);
  }
  void RemoveSupplicantNetworks() { wifi_->RemoveSupplicantNetworks(); }
  void InitiateScan() { wifi_->InitiateScan(); }
  void InitiateScanInDarkResume(const WiFi::FreqSet& freqs) {
    wifi_->InitiateScanInDarkResume(freqs);
  }
  void TriggerPassiveScan(const WiFi::FreqSet& freqs) {
    wifi_->TriggerPassiveScan(freqs);
  }
  void OnSupplicantAppear() {
    wifi_->OnSupplicantPresence(true);
    EXPECT_TRUE(wifi_->supplicant_present_);
  }
  void OnSupplicantVanish() {
    wifi_->OnSupplicantPresence(false);
    EXPECT_FALSE(wifi_->supplicant_present_);
  }
  bool GetSupplicantPresent() { return wifi_->supplicant_present_; }
  bool GetIsRoamingInProgress() { return wifi_->is_roaming_in_progress_; }
  void SetIPConfig(const IPConfigRefPtr& ipconfig) {
    return wifi_->set_ipconfig(ipconfig);
  }
  bool SetBgscanMethod(const string& method) {
    Error error;
    return wifi_->mutable_store()->SetAnyProperty(kBgscanMethodProperty,
                                                  brillo::Any(method), &error);
  }

  void AppendBgscan(WiFiService* service, KeyValueStore* service_params) {
    wifi_->AppendBgscan(service, service_params);
  }

  void ReportCertification(const KeyValueStore& properties) {
    wifi_->CertificationTask(properties);
  }

  void ReportEAPEvent(const string& status, const string& parameter) {
    wifi_->EAPEventTask(status, parameter);
  }

  void RestartFastScanAttempts() { wifi_->RestartFastScanAttempts(); }

  void SetFastScansRemaining(int num) { wifi_->fast_scans_remaining_ = num; }

  void StartReconnectTimer() { wifi_->StartReconnectTimer(); }

  void StopReconnectTimer() { wifi_->StopReconnectTimer(); }

  void SetLinkMonitor(LinkMonitor* link_monitor) {
    wifi_->set_link_monitor(link_monitor);
  }

  bool SuspectCredentials(const WiFiServiceRefPtr& service,
                          Service::ConnectFailure* failure) {
    return wifi_->SuspectCredentials(service, failure);
  }

  void OnLinkMonitorFailure() { wifi_->OnLinkMonitorFailure(); }

  bool SetBgscanShortInterval(const uint16_t& interval, Error* error) {
    return wifi_->SetBgscanShortInterval(interval, error);
  }

  bool SetBgscanSignalThreshold(const int32_t& threshold, Error* error) {
    return wifi_->SetBgscanSignalThreshold(threshold, error);
  }

  void SetTDLSManager(TDLSManager* tdls_manager) {
    wifi_->tdls_manager_.reset(tdls_manager);
  }

  void TDLSDiscoverResponse(const string& peer_address) {
    wifi_->TDLSDiscoverResponse(peer_address);
  }

  string PerformTDLSOperation(const string& operation,
                              const string& peer,
                              Error* error) {
    return wifi_->PerformTDLSOperation(operation, peer, error);
  }

  void TimeoutPendingConnection() { wifi_->PendingTimeoutHandler(); }

  void OnNewWiphy(const Nl80211Message& new_wiphy_message) {
    wifi_->OnNewWiphy(new_wiphy_message);
  }

  bool IsConnectedToCurrentService() {
    return wifi_->IsConnectedToCurrentService();
  }

  MockControl* control_interface() { return &control_interface_; }

  MockMetrics* metrics() { return &metrics_; }

  MockManager* manager() { return &manager_; }

  MockPowerManager* power_manager() { return power_manager_; }

  MockDeviceInfo* device_info() { return &device_info_; }

  MockDHCPProvider* dhcp_provider() { return &dhcp_provider_; }

  const WiFiConstRefPtr wifi() const { return wifi_; }

  MockWiFiProvider* wifi_provider() { return &wifi_provider_; }

  MockMac80211Monitor* mac80211_monitor() { return mac80211_monitor_; }

  void ReportConnectedToServiceAfterWake() {
    wifi_->ReportConnectedToServiceAfterWake();
  }

  void StartScanTimer() { wifi_->StartScanTimer(); }

  bool ParseWiphyIndex(const Nl80211Message& nl80211_message) {
    return wifi_->ParseWiphyIndex(nl80211_message);
  }

  uint32_t GetWiphyIndex() { return wifi_->wiphy_index_; }

  void SetWiphyIndex(uint32_t index) { wifi_->wiphy_index_ = index; }

  void ParseFeatureFlags(const Nl80211Message& nl80211_message) {
    wifi_->ParseFeatureFlags(nl80211_message);
  }

  bool GetRandomMacSupported() { return wifi_->random_mac_supported_; }

  void SetRandomMacSupported(bool supported) {
    wifi_->random_mac_supported_ = supported;
  }

  bool GetRandomMacEnabled() { return wifi_->random_mac_enabled_; }

  void SetRandomMacEnabled(bool enabled) {
    Error error;
    wifi_->SetRandomMacEnabled(enabled, &error);
  }

  std::vector<unsigned char> GetRandomMacMask() { return WiFi::kRandomMacMask; }

  std::set<uint16_t>* GetAllScanFrequencies() {
    return &wifi_->all_scan_frequencies_;
  }

  void HandleNetlinkBroadcast(const NetlinkMessage& netlink_message) {
    wifi_->HandleNetlinkBroadcast(netlink_message);
  }

  bool ScanFailedCallbackIsCancelled() {
    return wifi_->scan_failed_callback_.IsCancelled();
  }

  void SetWiFiEnabled(bool enabled) { wifi_->enabled_ = enabled; }

  void OnGetReg(const Nl80211Message& msg) { wifi_->OnGetReg(msg); }

  MOCK_METHOD(void, SuspendCallback, (const Error&));

  std::unique_ptr<EventDispatcher> event_dispatcher_;
  MockWakeOnWiFi* wake_on_wifi_;  // Owned by |wifi_|.
  NiceMock<MockRTNLHandler> rtnl_handler_;
  MockTime time_;
  MockNetlinkManager netlink_manager_;

 private:
  MockControl control_interface_;
  MockMetrics metrics_;
  MockManager manager_;
  MockPowerManager* power_manager_;  // Owned by |manager_|.
  MockDeviceInfo device_info_;
  WiFiRefPtr wifi_;
  NiceMock<MockWiFiProvider> wifi_provider_;
  int bss_counter_;
  MockMac80211Monitor* mac80211_monitor_;  // Owned by |wifi_|.

  // protected fields interspersed between private fields, due to
  // initialization order
 protected:
  static const char kDeviceName[];
  static const char kDeviceAddress[];
  static const char kNetworkModeAdHoc[];
  static const char kNetworkModeInfrastructure[];
  static const RpcIdentifier kBSSName;
  static const char kSSIDName[];

  MockSupplicantProcessProxy* supplicant_process_proxy_;
  unique_ptr<MockSupplicantBSSProxy> supplicant_bss_proxy_;
  MockDHCPProvider dhcp_provider_;
  scoped_refptr<MockDHCPConfig> dhcp_config_;

  // These pointers track mock objects owned by the WiFi device instance
  // and manager so we can perform expectations against them.
  DeviceMockAdaptor* adaptor_;
  MockSupplicantEAPStateHandler* eap_state_handler_;

 private:
  std::unique_ptr<SupplicantInterfaceProxyInterface>
  CreateSupplicantInterfaceProxy(SupplicantEventDelegateInterface* delegate,
                                 const RpcIdentifier& object_path) {
    return std::move(supplicant_interface_proxy_);
  }

  std::unique_ptr<SupplicantNetworkProxyInterface> CreateSupplicantNetworkProxy(
      const RpcIdentifier& object_path) {
    return std::move(supplicant_network_proxy_);
  }

  std::unique_ptr<SupplicantBSSProxyInterface> CreateSupplicantBSSProxy(
      WiFiEndpoint* wifi_endpoint, const RpcIdentifier& object_path) {
    return std::move(supplicant_bss_proxy_);
  }

  unique_ptr<MockSupplicantInterfaceProxy> supplicant_interface_proxy_;
  unique_ptr<MockSupplicantNetworkProxy> supplicant_network_proxy_;
};

const char WiFiObjectTest::kDeviceName[] = "wlan0";
const char WiFiObjectTest::kDeviceAddress[] = "000102030405";
const char WiFiObjectTest::kNetworkModeAdHoc[] = "ad-hoc";
const char WiFiObjectTest::kNetworkModeInfrastructure[] = "infrastructure";
const RpcIdentifier WiFiObjectTest::kBSSName("bss0");
const char WiFiObjectTest::kSSIDName[] = "ssid0";

void WiFiObjectTest::RemoveBSS(const RpcIdentifier& bss_path) {
  wifi_->BSSRemovedTask(bss_path);
}

KeyValueStore WiFiObjectTest::CreateBSSProperties(const string& ssid,
                                                  const string& bssid,
                                                  int16_t signal_strength,
                                                  uint16_t frequency,
                                                  const char* mode) {
  KeyValueStore bss_properties;
  bss_properties.Set<vector<uint8_t>>(
      "SSID", vector<uint8_t>(ssid.begin(), ssid.end()));
  {
    string bssid_nosep;
    vector<uint8_t> bssid_bytes;
    base::RemoveChars(bssid, ":", &bssid_nosep);
    base::HexStringToBytes(bssid_nosep, &bssid_bytes);
    bss_properties.Set<vector<uint8_t>>("BSSID", bssid_bytes);
  }
  bss_properties.Set<int16_t>(WPASupplicant::kBSSPropertySignal,
                              signal_strength);
  bss_properties.Set<uint16_t>(WPASupplicant::kBSSPropertyFrequency, frequency);
  bss_properties.Set<string>(WPASupplicant::kBSSPropertyMode, mode);

  return bss_properties;
}

void WiFiObjectTest::ReportBSS(const RpcIdentifier& bss_path,
                               const string& ssid,
                               const string& bssid,
                               int16_t signal_strength,
                               uint16_t frequency,
                               const char* mode) {
  wifi_->BSSAddedTask(
      bss_path,
      CreateBSSProperties(ssid, bssid, signal_strength, frequency, mode));
}

// Most of our tests involve using a real EventDispatcher object.
class WiFiMainTest : public WiFiObjectTest {
 public:
  WiFiMainTest() : WiFiObjectTest(std::make_unique<EventDispatcherForTest>()) {}

 protected:
  void StartScan(WiFi::ScanMethod method) {
    VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
    EXPECT_CALL(*adaptor_, EmitBoolChanged(kPoweredProperty, _))
        .Times(AnyNumber());

    ExpectScanStart(method, false);
    StartWiFi();
    event_dispatcher_->DispatchPendingEvents();
    VerifyScanState(WiFi::kScanScanning, method);
  }

  MockWiFiServiceRefPtr AttemptConnection(WiFi::ScanMethod method,
                                          WiFiEndpointRefPtr* endpoint,
                                          RpcIdentifier* bss_path) {
    WiFiEndpointRefPtr dummy_endpoint;
    if (!endpoint) {
      endpoint = &dummy_endpoint;  // If caller doesn't care about endpoint.
    }

    RpcIdentifier dummy_bss_path;
    if (!bss_path) {
      bss_path = &dummy_bss_path;  // If caller doesn't care about bss_path.
    }

    ExpectScanStop();
    ExpectConnecting();
    MockWiFiServiceRefPtr service =
        SetupConnectingService(RpcIdentifier(""), endpoint, bss_path);
    ReportScanDone();
    event_dispatcher_->DispatchPendingEvents();
    VerifyScanState(WiFi::kScanConnecting, method);

    return service;
  }

  void ExpectScanStart(WiFi::ScanMethod method, bool is_continued) {
    EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
    if (!is_continued) {
      EXPECT_CALL(*adaptor_, EmitBoolChanged(kScanningProperty, true));
      EXPECT_CALL(*metrics(), NotifyDeviceScanStarted(_));
    }
  }

  // Scanning can stop for any reason (including transitioning to connecting).
  void ExpectScanStop() {
    EXPECT_CALL(*adaptor_, EmitBoolChanged(kScanningProperty, false));
  }

  void ExpectConnecting() {
    EXPECT_CALL(*metrics(), NotifyDeviceScanFinished(_));
    EXPECT_CALL(*metrics(), NotifyDeviceConnectStarted(_, _));
  }

  void ExpectConnected() {
    EXPECT_CALL(*metrics(), NotifyDeviceConnectFinished(_));
    ExpectScanIdle();
  }

  void ExpectFoundNothing() {
    EXPECT_CALL(*metrics(), NotifyDeviceScanFinished(_));
    EXPECT_CALL(*metrics(), ResetConnectTimer(_));
    ExpectScanIdle();
  }

  void ExpectScanIdle() {
    EXPECT_CALL(*metrics(), ResetScanTimer(_));
    EXPECT_CALL(*metrics(), ResetConnectTimer(_)).RetiresOnSaturation();
  }
};

TEST_F(WiFiMainTest, ProxiesSetUpDuringStart) {
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  StartWiFi();
  EXPECT_NE(nullptr, GetSupplicantInterfaceProxyFromWiFi());
}

TEST_F(WiFiMainTest, SupplicantPresent) {
  EXPECT_FALSE(GetSupplicantPresent());
}

TEST_F(WiFiMainTest, OnSupplicantAppearStarted) {
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  StartWiFi(false);  // No supplicant present.
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveAllNetworks());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), FlushBSS(0));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), SetFastReauth(false));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), SetScanInterval(_));

  OnSupplicantAppear();
  EXPECT_NE(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  // If supplicant reappears while the device is started, the device should be
  // restarted.
  EXPECT_CALL(*manager(), DeregisterDevice(_));
  EXPECT_CALL(*manager(), RegisterDevice(_));
  OnSupplicantAppear();
}

TEST_F(WiFiMainTest, OnSupplicantAppearStopped) {
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  OnSupplicantAppear();
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  // If supplicant reappears while the device is stopped, the device should not
  // be restarted.
  EXPECT_CALL(*manager(), DeregisterDevice(_)).Times(0);
  OnSupplicantAppear();
}

TEST_F(WiFiMainTest, OnSupplicantVanishStarted) {
  EXPECT_EQ(nullptr, GetSupplicantInterfaceProxyFromWiFi());

  StartWiFi();
  EXPECT_NE(nullptr, GetSupplicantInterfaceProxyFromWiFi());
  EXPECT_TRUE(GetSupplicantPresent());

  EXPECT_CALL(*manager(), DeregisterDevice(_));
  EXPECT_CALL(*manager(), RegisterDevice(_));
  OnSupplicantVanish();
}

TEST_F(WiFiMainTest, OnSupplicantVanishStopped) {
  OnSupplicantAppear();
  EXPECT_TRUE(GetSupplicantPresent());
  EXPECT_CALL(*manager(), DeregisterDevice(_)).Times(0);
  OnSupplicantVanish();
}

TEST_F(WiFiMainTest, OnSupplicantVanishedWhileConnected) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint;
  WiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), &endpoint, nullptr));
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _,
                       EndsWith("silently resetting current_service_.")));
  EXPECT_CALL(*manager(), DeregisterDevice(_))
      .WillOnce(InvokeWithoutArgs(this, &WiFiObjectTest::StopWiFi));
  unique_ptr<EndpointRemovalHandler> handler =
      MakeEndpointRemovalHandler(service);
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(endpoint)))
      .WillOnce(
          Invoke(handler.get(), &EndpointRemovalHandler::OnEndpointRemoved));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect()).Times(0);
  EXPECT_CALL(*manager(), RegisterDevice(_));
  OnSupplicantVanish();
  EXPECT_EQ(nullptr, GetCurrentService());
}

TEST_F(WiFiMainTest, CleanStart) {
  EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _));
  EXPECT_CALL(*supplicant_process_proxy_, GetInterface(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(GetScanTimer().IsCancelled());
  StartWiFi();
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_FALSE(GetScanTimer().IsCancelled());
}

TEST_F(WiFiMainTest, ClearCachedCredentials) {
  StartWiFi();
  RpcIdentifier network("/test/path");
  WiFiServiceRefPtr service(SetupConnectedService(network, nullptr, nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(network));
  ClearCachedCredentials(service.get());
}

TEST_F(WiFiMainTest, NotifyEndpointChanged) {
  WiFiEndpointRefPtr endpoint = MakeEndpoint("ssid", "00:00:00:00:00:00");
  EXPECT_CALL(*wifi_provider(), OnEndpointUpdated(EndpointMatch(endpoint)));
  NotifyEndpointChanged(endpoint);
}

TEST_F(WiFiMainTest, RemoveNetwork) {
  RpcIdentifier network("/test/path");
  StartWiFi();
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(network))
      .WillOnce(Return(true));
  EXPECT_TRUE(RemoveNetwork(network));
}

TEST_F(WiFiMainTest, UseArpGateway) {
  StartWiFi();

  // With no selected service.
  EXPECT_TRUE(wifi()->ShouldUseArpGateway());
  EXPECT_CALL(dhcp_provider_, CreateIPv4Config(kDeviceName, _, true, _))
      .WillOnce(Return(dhcp_config_));
  const_cast<WiFi*>(wifi().get())->AcquireIPConfig();

  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  InitiateConnect(service);

  // Selected service that does not have a static IP address.
  EXPECT_CALL(*service, HasStaticIPAddress()).WillRepeatedly(Return(false));
  EXPECT_TRUE(wifi()->ShouldUseArpGateway());
  EXPECT_CALL(dhcp_provider_, CreateIPv4Config(kDeviceName, _, true, _))
      .WillOnce(Return(dhcp_config_));
  const_cast<WiFi*>(wifi().get())->AcquireIPConfig();
  Mock::VerifyAndClearExpectations(service.get());

  // Selected service that has a static IP address.
  EXPECT_CALL(*service, HasStaticIPAddress()).WillRepeatedly(Return(true));
  EXPECT_FALSE(wifi()->ShouldUseArpGateway());
  EXPECT_CALL(dhcp_provider_, CreateIPv4Config(kDeviceName, _, false, _))
      .WillOnce(Return(dhcp_config_));
  const_cast<WiFi*>(wifi().get())->AcquireIPConfig();
}

TEST_F(WiFiMainTest, RemoveNetworkFailed) {
  RpcIdentifier network("/test/path");
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(network))
      .WillRepeatedly(Return(false));
  StartWiFi();
  EXPECT_FALSE(RemoveNetwork(network));
}

TEST_F(WiFiMainTest, Restart) {
  EXPECT_CALL(*supplicant_process_proxy_, CreateInterface(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*supplicant_process_proxy_, GetInterface(_, _));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  EXPECT_CALL(*metrics(), NotifyWiFiSupplicantSuccess(1));
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, StartClearsState) {
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveAllNetworks());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), FlushBSS(_));
  StartWiFi();
}

TEST_F(WiFiMainTest, NoScansWhileConnecting) {
  // Setup 'connecting' state.
  StartScan(WiFi::kScanMethodFull);
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  ExpectScanStop();
  ExpectConnecting();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  InitiateConnect(service);
  VerifyScanState(WiFi::kScanConnecting, WiFi::kScanMethodFull);

  // If we're connecting, we ignore scan requests and stay on channel.
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  TriggerScan();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  Mock::VerifyAndClearExpectations(service.get());

  // Terminate the scan.
  ExpectFoundNothing();
  TimeoutPendingConnection();
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  // Start a fresh scan.
  ExpectScanStart(WiFi::kScanMethodFull, false);
  TriggerScan();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  Mock::VerifyAndClearExpectations(service.get());

  // Similarly, ignore scans when our connected service is reconnecting.
  ExpectScanStop();
  ExpectScanIdle();
  SetPendingService(nullptr);
  SetCurrentService(service);
  EXPECT_CALL(*service, IsConnecting()).WillOnce(Return(true));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  TriggerScan();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  Mock::VerifyAndClearExpectations(service.get());

  // But otherwise we'll honor the request.
  EXPECT_CALL(*service, IsConnecting())
      .Times(AtLeast(2))
      .WillRepeatedly(Return(false));
  ExpectScanStart(WiFi::kScanMethodFull, false);
  TriggerScan();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  Mock::VerifyAndClearExpectations(service.get());

  // Silence messages from the destructor.
  ExpectScanStop();
  ExpectScanIdle();
}

TEST_F(WiFiMainTest, ResetScanStateWhenScanFailed) {
  StartScan(WiFi::kScanMethodFull);
  ExpectScanStop();
  VerifyScanState(WiFi::kScanScanning, WiFi::kScanMethodFull);
  ReportScanFailed();
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
}

TEST_F(WiFiMainTest, ResumeStartsScanWhenIdle) {
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  ReportScanDone();
  ASSERT_TRUE(wifi()->IsIdle());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  OnAfterResume();
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ResumeDoesNotScanIfConnected) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  ReportScanDone();
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());
  ASSERT_TRUE(wifi()->IsIdle());
  event_dispatcher_->DispatchPendingEvents();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  OnAfterResume();
  EXPECT_FALSE(GetScanTimer().IsCancelled());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, SuspendDoesNotStartScan) {
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  ASSERT_TRUE(wifi()->IsIdle());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  OnBeforeSuspend();
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ResumeDoesNotStartScanWhenNotIdle) {
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  WiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_FALSE(wifi()->IsIdle());
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, EndsWith("already connecting or connected.")));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  OnAfterResume();
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ResumeDoesNotStartScanWhenDisabled) {
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  SetWiFiEnabled(false);
  OnBeforeSuspend();
  OnAfterResume();
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ResumeWithCurrentService) {
  StartWiFi();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);

  OnAfterResume();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
}

TEST_F(WiFiMainTest, ScanResults) {
  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(_)).Times(3);
  StartWiFi();
  // Ad-hoc networks will be dropped.
  ReportBSS(RpcIdentifier("bss0"), "ssid0", "00:00:00:00:00:00", 0, 0,
            kNetworkModeAdHoc);
  ReportBSS(RpcIdentifier("bss1"), "ssid1", "00:00:00:00:00:01", 1, 0,
            kNetworkModeInfrastructure);
  ReportBSS(RpcIdentifier("bss2"), "ssid2", "00:00:00:00:00:02", 2, 0,
            kNetworkModeInfrastructure);
  ReportBSS(RpcIdentifier("bss3"), "ssid3", "00:00:00:00:00:03", 3, 0,
            kNetworkModeInfrastructure);
  const uint16_t frequency = 2412;
  ReportBSS(RpcIdentifier("bss4"), "ssid4", "00:00:00:00:00:04", 4, frequency,
            kNetworkModeAdHoc);

  const WiFi::EndpointMap& endpoints_by_rpcid = GetEndpointMap();
  EXPECT_EQ(3, endpoints_by_rpcid.size());

  for (const auto& endpoint : endpoints_by_rpcid) {
    EXPECT_NE(kNetworkModeAdHoc, endpoint.second->network_mode());
    EXPECT_NE(endpoint.second->bssid_string(), "00:00:00:00:00:00");
    EXPECT_NE(endpoint.second->bssid_string(), "00:00:00:00:00:04");
  }
}

TEST_F(WiFiMainTest, ScanCompleted) {
  StartWiFi();
  WiFiEndpointRefPtr ap0 = MakeEndpoint("ssid0", "00:00:00:00:00:00");
  WiFiEndpointRefPtr ap1 = MakeEndpoint("ssid1", "00:00:00:00:00:01");
  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(EndpointMatch(ap0))).Times(1);
  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(EndpointMatch(ap1))).Times(1);
  ReportBSS(RpcIdentifier("bss0"), ap0->ssid_string(), ap0->bssid_string(), 0,
            0, kNetworkModeInfrastructure);
  ReportBSS(RpcIdentifier("bss1"), ap1->ssid_string(), ap1->bssid_string(), 0,
            0, kNetworkModeInfrastructure);
  manager()->set_suppress_autoconnect(true);
  ReportScanDone();
  EXPECT_FALSE(manager()->suppress_autoconnect());
  Mock::VerifyAndClearExpectations(wifi_provider());

  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(_)).Times(0);

  // BSSes with SSIDs that start with nullptr should be filtered.
  ReportBSS(RpcIdentifier("bss2"), string(1, 0), "00:00:00:00:00:02", 3, 0,
            kNetworkModeInfrastructure);

  // BSSes with empty SSIDs should be filtered.
  ReportBSS(RpcIdentifier("bss2"), string(), "00:00:00:00:00:02", 3, 0,
            kNetworkModeInfrastructure);
}

TEST_F(WiFiMainTest, LoneBSSRemovedWhileConnected) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint;
  RpcIdentifier bss_path;
  WiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), &endpoint, &bss_path));
  unique_ptr<EndpointRemovalHandler> handler =
      MakeEndpointRemovalHandler(service);
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(endpoint)))
      .WillOnce(
          Invoke(handler.get(), &EndpointRemovalHandler::OnEndpointRemoved));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  RemoveBSS(bss_path);
}

TEST_F(WiFiMainTest, GetCurrentEndpoint) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint;
  RpcIdentifier bss_path;
  MockWiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), &endpoint, &bss_path));
  const WiFiEndpointConstRefPtr current_endpoint = wifi()->GetCurrentEndpoint();
  EXPECT_NE(nullptr, current_endpoint);
  EXPECT_EQ(current_endpoint->bssid_string(), endpoint->bssid_string());
}

TEST_F(WiFiMainTest, NonSolitaryBSSRemoved) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint;
  RpcIdentifier bss_path;
  WiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), &endpoint, &bss_path));
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(endpoint)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect()).Times(0);
  RemoveBSS(bss_path);
}

TEST_F(WiFiMainTest, ReconnectPreservesDBusPath) {
  StartWiFi();
  RpcIdentifier kPath("/test/path");
  MockWiFiServiceRefPtr service(SetupConnectedService(kPath, nullptr, nullptr));

  // Return the service to a connectable state.
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  InitiateDisconnect(service);
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  // Complete the disconnection by reporting a BSS change.
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));

  // A second connection attempt should remember the DBus path associated
  // with this service, and should not request new configuration parameters.
  EXPECT_CALL(*service, GetSupplicantConfigurationParameters()).Times(0);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _)).Times(0);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), SelectNetwork(kPath));
  InitiateConnect(service);
}

TEST_F(WiFiMainTest, DisconnectPendingService) {
  StartWiFi();
  MockWiFiServiceRefPtr service(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(GetPendingService(), service);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  EXPECT_CALL(*service, SetFailure(_)).Times(0);
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  service->set_expecting_disconnect(true);
  InitiateDisconnect(service);
  Mock::VerifyAndClearExpectations(service.get());
  EXPECT_EQ(nullptr, GetPendingService());
}

TEST_F(WiFiMainTest, DisconnectPendingServiceWithFailure) {
  StartWiFi();
  MockWiFiServiceRefPtr service(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(GetPendingService(), service);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  EXPECT_CALL(*service, SetFailure(Service::kFailureUnknown));
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  InitiateDisconnect(service);
  Mock::VerifyAndClearExpectations(service.get());
  EXPECT_EQ(nullptr, GetPendingService());
}

TEST_F(WiFiMainTest, DisconnectPendingServiceWithOutOfRange) {
  StartWiFi();

  // Initiate connection with weak signal
  MockWiFiServiceRefPtr service;
  MakeNewEndpointAndService(-90, 0, nullptr, &service);
  InitiateConnect(service);

  EXPECT_EQ(GetPendingService(), service);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  EXPECT_CALL(*service, SetFailure(Service::kFailureOutOfRange));
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  ReportDisconnectReasonChanged(-IEEE_80211::kReasonCodeInactivity);
  InitiateDisconnect(service);
  Mock::VerifyAndClearExpectations(service.get());
  EXPECT_EQ(nullptr, GetPendingService());
}

TEST_F(WiFiMainTest, DisconnectPendingServiceWithCurrent) {
  StartWiFi();
  MockWiFiServiceRefPtr service0(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(service0, GetCurrentService());
  EXPECT_EQ(nullptr, GetPendingService());

  // We don't explicitly call Disconnect() while transitioning to a new
  // service.  Instead, we use the side-effect of SelectNetwork (verified in
  // SetupConnectingService).
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect()).Times(0);
  MockWiFiServiceRefPtr service1(
      SetupConnectingService(RpcIdentifier("/new/path"), nullptr, nullptr));
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  EXPECT_EQ(service0, GetCurrentService());
  EXPECT_EQ(service1, GetPendingService());
  EXPECT_CALL(*service1, SetState(Service::kStateIdle)).Times(AtLeast(1));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  InitiateDisconnect(service1);
  Mock::VerifyAndClearExpectations(service1.get());

  // |current_service_| will be unchanged until supplicant signals
  // that CurrentBSS has changed.
  EXPECT_EQ(service0, GetCurrentService());
  // |pending_service_| is updated immediately.
  EXPECT_EQ(nullptr, GetPendingService());
  EXPECT_TRUE(GetPendingTimeout().IsCancelled());
}

TEST_F(WiFiMainTest, DisconnectCurrentService) {
  StartWiFi();
  RpcIdentifier kPath("/fake/path");
  MockWiFiServiceRefPtr service(SetupConnectedService(kPath, nullptr, nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  service->set_expecting_disconnect(true);
  InitiateDisconnect(service);

  // |current_service_| should not change until supplicant reports
  // a BSS change.
  EXPECT_EQ(service, GetCurrentService());

  // Expect that the entry associated with this network will be disabled.
  auto network_proxy = std::make_unique<MockSupplicantNetworkProxy>();
  EXPECT_CALL(*network_proxy, SetEnabled(false)).WillOnce(Return(true));
  EXPECT_CALL(*control_interface(), CreateSupplicantNetworkProxy(kPath))
      .WillOnce(Return(ByMove(std::move(network_proxy))));

  EXPECT_CALL(*eap_state_handler_, Reset());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath)).Times(0);
  EXPECT_CALL(*service, SetFailure(_)).Times(0);
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  EXPECT_EQ(nullptr, GetCurrentService());
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
}

TEST_F(WiFiMainTest, DisconnectCurrentServiceWithFailure) {
  StartWiFi();
  RpcIdentifier kPath("/fake/path");
  MockWiFiServiceRefPtr service(SetupConnectedService(kPath, nullptr, nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  InitiateDisconnect(service);

  // |current_service_| should not change until supplicant reports
  // a BSS change.
  EXPECT_EQ(service, GetCurrentService());

  // Expect that the entry associated with this network will be disabled.
  auto network_proxy = std::make_unique<MockSupplicantNetworkProxy>();
  EXPECT_CALL(*network_proxy, SetEnabled(false)).WillOnce(Return(true));
  EXPECT_CALL(*control_interface(), CreateSupplicantNetworkProxy(kPath))
      .WillOnce(Return(ByMove(std::move(network_proxy))));

  EXPECT_CALL(*eap_state_handler_, Reset());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath)).Times(0);
  EXPECT_CALL(*service, SetFailure(Service::kFailureUnknown));
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  EXPECT_EQ(nullptr, GetCurrentService());
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
}

TEST_F(WiFiMainTest, DisconnectCurrentServiceWithOutOfRange) {
  StartWiFi();

  // Setup connection with weak signal
  RpcIdentifier kPath("/fake/path");
  MockWiFiServiceRefPtr service;
  RpcIdentifier bss_path(MakeNewEndpointAndService(-80, 0, nullptr, &service));
  EXPECT_CALL(*service, GetSupplicantConfigurationParameters());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kPath), Return(true)));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), SelectNetwork(kPath));
  InitiateConnect(service);
  ReportCurrentBSSChanged(bss_path);
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);

  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  InitiateDisconnect(service);

  // |current_service_| should not change until supplicant reports
  // a BSS change.
  EXPECT_EQ(service, GetCurrentService());

  // Expect that the entry associated with this network will be disabled.
  auto network_proxy = std::make_unique<MockSupplicantNetworkProxy>();
  EXPECT_CALL(*network_proxy, SetEnabled(false)).WillOnce(Return(true));
  EXPECT_CALL(*control_interface(), CreateSupplicantNetworkProxy(kPath))
      .WillOnce(Return(ByMove(std::move(network_proxy))));

  EXPECT_CALL(*eap_state_handler_, Reset());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath)).Times(0);
  EXPECT_CALL(*service, SetFailure(Service::kFailureOutOfRange));
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  ReportDisconnectReasonChanged(-IEEE_80211::kReasonCodeInactivity);
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  EXPECT_EQ(nullptr, GetCurrentService());
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
}

TEST_F(WiFiMainTest, DisconnectCurrentServiceWithErrors) {
  StartWiFi();
  RpcIdentifier kPath("/fake/path");
  WiFiServiceRefPtr service(SetupConnectedService(kPath, nullptr, nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect())
      .WillOnce(Return(false));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath)).Times(1);
  InitiateDisconnect(service);

  // We may sometimes fail to disconnect via supplicant, and we patch up some
  // state when this happens.
  EXPECT_EQ(nullptr, GetCurrentService());
  EXPECT_EQ(nullptr, GetSelectedService());
}

TEST_F(WiFiMainTest, DisconnectCurrentServiceWithPending) {
  StartWiFi();
  MockWiFiServiceRefPtr service0(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  MockWiFiServiceRefPtr service1(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(service0, GetCurrentService());
  EXPECT_EQ(service1, GetPendingService());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect()).Times(0);
  InitiateDisconnect(service0);

  EXPECT_EQ(service0, GetCurrentService());
  EXPECT_EQ(service1, GetPendingService());
  EXPECT_FALSE(GetPendingTimeout().IsCancelled());

  EXPECT_CALL(*service0, SetState(Service::kStateIdle)).Times(AtLeast(1));
  EXPECT_CALL(*service0, SetFailure(_)).Times(0);
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
}

TEST_F(WiFiMainTest, DisconnectCurrentServiceWhileRoaming) {
  StartWiFi();
  RpcIdentifier kPath("/fake/path");
  WiFiServiceRefPtr service(SetupConnectedService(kPath, nullptr, nullptr));

  // As it roams to another AP, supplicant signals that it is in
  // the authenticating state.
  ReportStateChanged(WPASupplicant::kInterfaceStateAuthenticating);

  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath));
  InitiateDisconnect(service);

  // Because the interface was not connected, we should have immediately
  // forced ourselves into a disconnected state.
  EXPECT_EQ(nullptr, GetCurrentService());
  EXPECT_EQ(nullptr, GetSelectedService());

  // Check calls before TearDown/dtor.
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
}

TEST_F(WiFiMainTest, DisconnectWithWiFiServiceConnected) {
  StartWiFi();
  MockWiFiServiceRefPtr service0(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(2);
  EXPECT_CALL(log, Log(_, _, ContainsRegex("DisconnectFromIfActive.*service")))
      .Times(1);
  EXPECT_CALL(log, Log(_, _, ContainsRegex("DisconnectFrom[^a-zA-Z].*service")))
      .Times(1);
  EXPECT_CALL(*service0, IsActive(_)).Times(0);
  InitiateDisconnectIfActive(service0);

  Mock::VerifyAndClearExpectations(&log);
  Mock::VerifyAndClearExpectations(service0.get());
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiMainTest, DisconnectWithWiFiServiceIdle) {
  StartWiFi();
  MockWiFiServiceRefPtr service0(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  InitiateDisconnectIfActive(service0);
  MockWiFiServiceRefPtr service1(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(2);
  EXPECT_CALL(log, Log(_, _, ContainsRegex("DisconnectFromIfActive.*service")))
      .Times(1);
  EXPECT_CALL(*service0, IsActive(_)).WillOnce(Return(false));
  EXPECT_CALL(log, Log(_, _, HasSubstr("is not active, no need"))).Times(1);
  EXPECT_CALL(log, Log(logging::LOG_WARNING, _,
                       ContainsRegex("In .*DisconnectFrom\\(.*\\):")))
      .Times(0);
  InitiateDisconnectIfActive(service0);

  Mock::VerifyAndClearExpectations(&log);
  Mock::VerifyAndClearExpectations(service0.get());
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiMainTest, DisconnectWithWiFiServiceConnectedInError) {
  StartWiFi();
  MockWiFiServiceRefPtr service0(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  SetCurrentService(nullptr);
  ResetPendingService();
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(2);
  EXPECT_CALL(log, Log(_, _, ContainsRegex("DisconnectFromIfActive.*service")))
      .Times(1);
  EXPECT_CALL(*service0, IsActive(_)).WillOnce(Return(true));
  EXPECT_CALL(log, Log(_, _, ContainsRegex("DisconnectFrom[^a-zA-Z].*service")))
      .Times(1);
  EXPECT_CALL(log, Log(logging::LOG_WARNING, _,
                       ContainsRegex("In .*DisconnectFrom\\(.*\\):")))
      .Times(1);
  InitiateDisconnectIfActive(service0);

  Mock::VerifyAndClearExpectations(&log);
  Mock::VerifyAndClearExpectations(service0.get());
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiMainTest, TimeoutPendingServiceWithEndpoints) {
  StartScan(WiFi::kScanMethodFull);
  const base::CancelableClosure& pending_timeout = GetPendingTimeout();
  EXPECT_TRUE(pending_timeout.IsCancelled());
  MockWiFiServiceRefPtr service =
      AttemptConnection(WiFi::kScanMethodFull, nullptr, nullptr);

  // Timeout the connection attempt.
  EXPECT_FALSE(pending_timeout.IsCancelled());
  EXPECT_EQ(service, GetPendingService());
  // Simulate a service with a wifi_ reference calling DisconnectFrom().
  EXPECT_CALL(*service,
              DisconnectWithFailure(Service::kFailureOutOfRange, _,
                                    HasSubstr("PendingTimeoutHandler")))
      .WillOnce(InvokeWithoutArgs(this, &WiFiObjectTest::ResetPendingService));
  EXPECT_CALL(*service, HasEndpoints()).Times(0);
  // DisconnectFrom() should not be called directly from WiFi.
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(1);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect()).Times(0);

  // Innocuous redundant call to NotifyDeviceScanFinished.
  ExpectFoundNothing();
  EXPECT_CALL(*metrics(), NotifyDeviceConnectFinished(_)).Times(0);
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(10);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> FULL_NOCONNECTION")));
  pending_timeout.callback().Run();
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
  // Service state should be idle, so it is connectable again.
  EXPECT_EQ(Service::kStateIdle, service->state());
  Mock::VerifyAndClearExpectations(service.get());

  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiMainTest, TimeoutPendingServiceWithoutEndpoints) {
  StartWiFi();
  const base::CancelableClosure& pending_timeout = GetPendingTimeout();
  EXPECT_TRUE(pending_timeout.IsCancelled());
  MockWiFiServiceRefPtr service(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_FALSE(pending_timeout.IsCancelled());
  EXPECT_EQ(service, GetPendingService());
  // We expect the service to get a disconnect call, but in this scenario
  // the service does nothing.
  EXPECT_CALL(*service,
              DisconnectWithFailure(Service::kFailureOutOfRange, _,
                                    HasSubstr("PendingTimeoutHandler")));
  EXPECT_CALL(*service, HasEndpoints()).WillOnce(Return(false));
  // DisconnectFrom() should be called directly from WiFi.
  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  pending_timeout.callback().Run();
  EXPECT_EQ(nullptr, GetPendingService());
}

TEST_F(WiFiMainTest, DisconnectInvalidService) {
  StartWiFi();
  MockWiFiServiceRefPtr service;
  MakeNewEndpointAndService(0, 0, nullptr, &service);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect()).Times(0);
  InitiateDisconnect(service);
}

TEST_F(WiFiMainTest, DisconnectCurrentServiceFailure) {
  StartWiFi();
  RpcIdentifier kPath("/fake/path");
  WiFiServiceRefPtr service(SetupConnectedService(kPath, nullptr, nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath));
  InitiateDisconnect(service);
  EXPECT_EQ(nullptr, GetCurrentService());
}

TEST_F(WiFiMainTest, Stop) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint0;
  RpcIdentifier kPath("/fake/path");
  WiFiServiceRefPtr service0(SetupConnectedService(kPath, &endpoint0, nullptr));
  WiFiEndpointRefPtr endpoint1;
  MakeNewEndpointAndService(0, 0, &endpoint1, nullptr);

  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(endpoint0)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(endpoint1)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kPath)).Times(1);
  StopWiFi();
  EXPECT_TRUE(GetScanTimer().IsCancelled());
  EXPECT_FALSE(wifi()->weak_ptr_factory_while_started_.HasWeakPtrs());
}

TEST_F(WiFiMainTest, StopWhileConnected) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint;
  WiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), &endpoint, nullptr));
  unique_ptr<EndpointRemovalHandler> handler =
      MakeEndpointRemovalHandler(service);
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(endpoint)))
      .WillOnce(
          Invoke(handler.get(), &EndpointRemovalHandler::OnEndpointRemoved));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  StopWiFi();
  EXPECT_EQ(nullptr, GetCurrentService());
}

TEST_F(WiFiMainTest, ReconnectTimer) {
  StartWiFi();
  MockWiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_TRUE(GetReconnectTimeoutCallback().IsCancelled());
  ReportStateChanged(WPASupplicant::kInterfaceStateDisconnected);
  EXPECT_FALSE(GetReconnectTimeoutCallback().IsCancelled());
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
  EXPECT_TRUE(GetReconnectTimeoutCallback().IsCancelled());
  ReportStateChanged(WPASupplicant::kInterfaceStateDisconnected);
  EXPECT_FALSE(GetReconnectTimeoutCallback().IsCancelled());
  ReportCurrentBSSChanged(kBSSName);
  EXPECT_TRUE(GetReconnectTimeoutCallback().IsCancelled());
  ReportStateChanged(WPASupplicant::kInterfaceStateDisconnected);
  EXPECT_FALSE(GetReconnectTimeoutCallback().IsCancelled());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  GetReconnectTimeoutCallback().callback().Run();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_TRUE(GetReconnectTimeoutCallback().IsCancelled());
}

MATCHER_P(ScanRequestHasHiddenSSID, ssid, "") {
  if (!arg.template Contains<ByteArrays>(WPASupplicant::kPropertyScanSSIDs)) {
    return false;
  }

  ByteArrays ssids =
      arg.template Get<ByteArrays>(WPASupplicant::kPropertyScanSSIDs);
  // A valid Scan containing a single hidden SSID should contain
  // two SSID entries: one containing the SSID we are looking for,
  // and an empty entry, signifying that we also want to do a
  // broadcast probe request for all non-hidden APs as well.
  return ssids.size() == 2 && ssids[0] == ssid && ssids[1].empty();
}

MATCHER(ScanRequestHasNoHiddenSSID, "") {
  return !arg.template Contains<ByteArrays>(WPASupplicant::kPropertyScanSSIDs);
}

TEST_F(WiFiMainTest, ScanHidden) {
  vector<uint8_t> kSSID(1, 'a');
  ByteArrays ssids;
  ssids.push_back(kSSID);

  StartWiFi();
  EXPECT_CALL(*wifi_provider(), GetHiddenSSIDList()).WillOnce(Return(ssids));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              Scan(ScanRequestHasHiddenSSID(kSSID)));
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ScanNoHidden) {
  StartWiFi();
  EXPECT_CALL(*wifi_provider(), GetHiddenSSIDList())
      .WillOnce(Return(ByteArrays()));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              Scan(ScanRequestHasNoHiddenSSID()));
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ScanWiFiDisabledAfterResume) {
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  StartWiFi();
  StopWiFi();
  OnAfterResume();
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, ScanRejected) {
  ScopedMockLog log;
  StartWiFi();
  ReportScanDone();
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, EndsWith("Scan failed"))).Times(1);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).WillOnce(Return(false));
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, InitialSupplicantState) {
  EXPECT_EQ(WiFi::kInterfaceStateUnknown, GetSupplicantState());
}

TEST_F(WiFiMainTest, StateChangeNoService) {
  // State change should succeed even if there is no pending Service.
  ReportStateChanged(WPASupplicant::kInterfaceStateScanning);
  EXPECT_EQ(WPASupplicant::kInterfaceStateScanning, GetSupplicantState());
}

TEST_F(WiFiMainTest, StateChangeWithService) {
  // Forward transition should trigger a Service state change.
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  InitiateConnect(service);
  EXPECT_CALL(*service, SetState(Service::kStateAssociating));
  ReportStateChanged(WPASupplicant::kInterfaceStateAssociated);
  // Verify expectations now, because WiFi may report other state changes
  // when WiFi is Stop()-ed (during TearDown()).
  Mock::VerifyAndClearExpectations(service.get());
  EXPECT_CALL(*service, SetState(_)).Times(AnyNumber());
}

TEST_F(WiFiMainTest, StateChangeBackwardsWithService) {
  // Some backwards transitions should not trigger a Service state change.
  // Supplicant state should still be updated, however.
  EXPECT_CALL(*dhcp_provider(), CreateIPv4Config(_, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(*dhcp_config_, RequestIP()).Times(AnyNumber());
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  EXPECT_CALL(*service, SetState(Service::kStateAssociating));
  EXPECT_CALL(*service, SetState(Service::kStateConfiguring));
  EXPECT_CALL(*service, ResetSuspectedCredentialFailures());
  InitiateConnect(service);
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
  ReportStateChanged(WPASupplicant::kInterfaceStateAuthenticating);
  EXPECT_EQ(WPASupplicant::kInterfaceStateAuthenticating, GetSupplicantState());
  // Verify expectations now, because WiFi may report other state changes
  // when WiFi is Stop()-ed (during TearDown()).
  Mock::VerifyAndClearExpectations(service.get());
  EXPECT_CALL(*service, SetState(_)).Times(AnyNumber());
}

TEST_F(WiFiMainTest, ConnectToServiceWithoutRecentIssues) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  EXPECT_CALL(*service, HasRecentConnectionIssues()).WillOnce(Return(false));
  InitiateConnect(service);
  EXPECT_EQ(wifi()->is_debugging_connection_, false);
}

TEST_F(WiFiMainTest, ConnectToServiceWithRecentIssues) {
  // Turn of WiFi debugging, so the only reason we will turn on supplicant
  // debugging will be to debug a problematic connection.
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");

  MockSupplicantProcessProxy* process_proxy = supplicant_process_proxy_;
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelInfo)),
                      Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(WPASupplicant::kDebugLevelDebug))
      .Times(1);
  EXPECT_CALL(*service, HasRecentConnectionIssues()).WillOnce(Return(true));
  InitiateConnect(service);
  Mock::VerifyAndClearExpectations(process_proxy);

  SetPendingService(nullptr);
  SetCurrentService(service);

  // When we disconnect from the troubled service, we should reduce the
  // level of supplicant debugging.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelDebug)),
                      Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(WPASupplicant::kDebugLevelInfo))
      .Times(1);
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
}

TEST_F(WiFiMainTest, CurrentBSSChangeConnectedToDisconnected) {
  StartWiFi();
  WiFiEndpointRefPtr endpoint;
  MockWiFiServiceRefPtr service =
      SetupConnectedService(RpcIdentifier(""), &endpoint, nullptr);

  EXPECT_CALL(*service, SetState(Service::kStateIdle)).Times(AtLeast(1));
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  EXPECT_EQ(nullptr, GetCurrentService());
  EXPECT_EQ(nullptr, GetPendingService());
  EXPECT_FALSE(GetIsRoamingInProgress());
}

TEST_F(WiFiMainTest, CurrentBSSChangeConnectedToConnectedNewService) {
  StartWiFi();
  MockWiFiServiceRefPtr service0 =
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  MockWiFiServiceRefPtr service1;
  RpcIdentifier bss_path1(MakeNewEndpointAndService(0, 0, nullptr, &service1));
  EXPECT_EQ(service0, GetCurrentService());

  // Note that we deliberately omit intermediate supplicant states
  // (e.g. kInterfaceStateAssociating), on the theory that they are
  // unreliable. Specifically, they may be quashed if the association
  // completes before supplicant flushes its changed properties.
  EXPECT_CALL(*service0, SetState(Service::kStateIdle)).Times(AtLeast(1));
  ReportCurrentBSSChanged(bss_path1);
  EXPECT_CALL(*service1, SetState(Service::kStateConfiguring));
  EXPECT_CALL(*service1, ResetSuspectedCredentialFailures());
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
  EXPECT_EQ(service1, GetCurrentService());
  EXPECT_FALSE(GetIsRoamingInProgress());
  Mock::VerifyAndClearExpectations(service0.get());
  Mock::VerifyAndClearExpectations(service1.get());
}

TEST_F(WiFiMainTest, CurrentBSSChangedUpdateServiceEndpoint) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  VerifyScanState(WiFi::kScanScanning, WiFi::kScanMethodFull);

  MockWiFiServiceRefPtr service =
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  WiFiEndpointRefPtr endpoint;
  RpcIdentifier bss_path = AddEndpointToService(service, 0, 0, &endpoint);
  EXPECT_CALL(*service, NotifyCurrentEndpoint(EndpointMatch(endpoint)));
  ReportCurrentBSSChanged(bss_path);
  EXPECT_TRUE(GetIsRoamingInProgress());
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  // If we report a "completed" state change on a connected service after
  // wpa_supplicant has roamed, we should renew our IPConfig.
  scoped_refptr<MockIPConfig> ipconfig(
      new MockIPConfig(control_interface(), kDeviceName));
  SetIPConfig(ipconfig);
  EXPECT_CALL(*service, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*ipconfig, RenewIP());
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
  Mock::VerifyAndClearExpectations(ipconfig.get());
  EXPECT_FALSE(GetIsRoamingInProgress());
}

TEST_F(WiFiMainTest, DisconnectReasonUpdated) {
  ScopedMockLog log;
  IEEE_80211::WiFiReasonCode test_reason = IEEE_80211::kReasonCodeInactivity;
  EXPECT_CALL(*adaptor_, EmitBoolChanged(kPoweredProperty, _))
      .Times(AnyNumber());
  EXPECT_EQ(GetSupplicantDisconnectReason(), IEEE_80211::kReasonCodeInvalid);
  EXPECT_CALL(
      log,
      Log(logging::LOG_INFO, _,
          EndsWith(
              " DisconnectReason to 4 (Disassociated due to inactivity)")));
  ReportDisconnectReasonChanged(test_reason);
  EXPECT_EQ(GetSupplicantDisconnectReason(), test_reason);

  test_reason = IEEE_80211::kReasonCodeReserved0;
  EXPECT_CALL(
      log, Log(logging::LOG_INFO, _, EndsWith("Reason from 4 to 0 (Success)")));
  ReportDisconnectReasonChanged(test_reason);
  EXPECT_EQ(GetSupplicantDisconnectReason(), test_reason);
}

TEST_F(WiFiMainTest, DisconnectReasonCleared) {
  IEEE_80211::WiFiReasonCode test_reason = IEEE_80211::kReasonCodeInactivity;
  // Clearing the value for supplicant_disconnect_reason_ is done prior to any
  // early exits in the WiFi::StateChanged method.  This allows the value to be
  // checked without a mock pending or current service.
  ReportDisconnectReasonChanged(test_reason);
  EXPECT_EQ(wifi().get()->supplicant_disconnect_reason_, test_reason);
  ReportStateChanged(WPASupplicant::kInterfaceStateDisconnected);
  ReportStateChanged(WPASupplicant::kInterfaceStateAssociated);
  EXPECT_EQ(wifi().get()->supplicant_disconnect_reason_,
            IEEE_80211::kReasonCodeInvalid);
}

TEST_F(WiFiMainTest, GetSuffixFromAuthMode) {
  EXPECT_EQ("PSK", wifi()->GetSuffixFromAuthMode("WPA-PSK"));
  EXPECT_EQ("PSK", wifi()->GetSuffixFromAuthMode("WPA2-PSK"));
  EXPECT_EQ("PSK", wifi()->GetSuffixFromAuthMode("WPA2-PSK+WPA-PSK"));
  EXPECT_EQ("FTPSK", wifi()->GetSuffixFromAuthMode("FT-PSK"));
  EXPECT_EQ("FTEAP", wifi()->GetSuffixFromAuthMode("FT-EAP"));
  EXPECT_EQ("EAP", wifi()->GetSuffixFromAuthMode("EAP-TLS"));
  EXPECT_EQ("", wifi()->GetSuffixFromAuthMode("INVALID-PSK"));
}

TEST_F(WiFiMainTest, CurrentAuthModeChanged) {
  const string auth_mode0 = "FT-PSK";
  ReportCurrentAuthModeChanged(auth_mode0);
  EXPECT_EQ(wifi().get()->supplicant_auth_mode_, auth_mode0);

  const string auth_mode1 = "EAP-TLS";
  ReportCurrentAuthModeChanged(auth_mode1);
  EXPECT_EQ(wifi().get()->supplicant_auth_mode_, auth_mode1);
}

TEST_F(WiFiMainTest, NewConnectPreemptsPending) {
  StartWiFi();
  MockWiFiServiceRefPtr service0(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(service0, GetPendingService());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  MockWiFiServiceRefPtr service1(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(service1, GetPendingService());
  EXPECT_EQ(nullptr, GetCurrentService());
}

TEST_F(WiFiMainTest, ConnectedToUnintendedPreemptsPending) {
  StartWiFi();
  RpcIdentifier bss_path;
  // Connecting two different services back-to-back.
  MockWiFiServiceRefPtr unintended_service(
      SetupConnectingService(RpcIdentifier(""), nullptr, &bss_path));
  MockWiFiServiceRefPtr intended_service(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));

  // Verify the pending service.
  EXPECT_EQ(intended_service, GetPendingService());

  // Connected to the unintended service (service0).
  ReportCurrentBSSChanged(bss_path);

  // Verify the pending service is disconnected, and the service state is back
  // to idle, so it is connectable again.
  EXPECT_EQ(nullptr, GetPendingService());
  EXPECT_EQ(nullptr, GetCurrentService());
  EXPECT_EQ(Service::kStateIdle, intended_service->state());
}

TEST_F(WiFiMainTest, IsIdle) {
  StartWiFi();
  EXPECT_TRUE(wifi()->IsIdle());
  MockWiFiServiceRefPtr service(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_FALSE(wifi()->IsIdle());
}

MATCHER_P(WiFiAddedArgs, bgscan, "") {
  return arg.template Contains<uint32_t>(
             WPASupplicant::kNetworkPropertyScanSSID) &&
         arg.template Contains<uint32_t>(
             WPASupplicant::kNetworkPropertyDisableVHT) &&
         arg.template Contains<string>(WPASupplicant::kNetworkPropertyBgscan) &&
         arg.template Get<string>(WPASupplicant::kNetworkPropertyBgscan)
                 .empty() != bgscan;
}

TEST_F(WiFiMainTest, AddNetworkArgs) {
  StartWiFi();
  MockWiFiServiceRefPtr service;
  MakeNewEndpointAndService(0, 0, nullptr, &service);
  EXPECT_CALL(*service, GetSupplicantConfigurationParameters());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              AddNetwork(WiFiAddedArgs(true), _));
  EXPECT_TRUE(SetBgscanMethod(WPASupplicant::kNetworkBgscanMethodSimple));
  InitiateConnect(service);
}

TEST_F(WiFiMainTest, AddNetworkArgsNoBgscan) {
  StartWiFi();
  MockWiFiServiceRefPtr service;
  MakeNewEndpointAndService(0, 0, nullptr, &service);
  EXPECT_CALL(*service, GetSupplicantConfigurationParameters());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              AddNetwork(WiFiAddedArgs(false), _));
  EXPECT_TRUE(SetBgscanMethod(WPASupplicant::kNetworkBgscanMethodNone));
  InitiateConnect(service);
}

TEST_F(WiFiMainTest, AppendBgscan) {
  StartWiFi();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  {
    // 1 endpoint, default bgscan method -- background scan frequency very
    // reduced.
    KeyValueStore params;
    EXPECT_CALL(*service, GetEndpointCount()).WillOnce(Return(1));
    AppendBgscan(service.get(), &params);
    Mock::VerifyAndClearExpectations(service.get());
    string config_string;
    EXPECT_TRUE(params.Contains<string>(WPASupplicant::kNetworkPropertyBgscan));
    config_string = params.Get<string>(WPASupplicant::kNetworkPropertyBgscan);
    vector<string> elements = base::SplitString(
        config_string, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    EXPECT_EQ(WiFi::kDefaultBgscanMethod, elements[0]);
    EXPECT_EQ(StringPrintf("%d", WiFi::kSingleEndpointBgscanIntervalSeconds),
              elements[3]);
  }
  {
    // 2 endpoints, default bgscan method -- background scan frequency reduced.
    KeyValueStore params;
    EXPECT_CALL(*service, GetEndpointCount()).WillOnce(Return(2));
    AppendBgscan(service.get(), &params);
    Mock::VerifyAndClearExpectations(service.get());
    string config_string;
    EXPECT_TRUE(params.Contains<string>(WPASupplicant::kNetworkPropertyBgscan));
    config_string = params.Get<string>(WPASupplicant::kNetworkPropertyBgscan);
    vector<string> elements = base::SplitString(
        config_string, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    ASSERT_EQ(4, elements.size());
    EXPECT_EQ(WiFi::kDefaultBgscanMethod, elements[0]);
    EXPECT_EQ(StringPrintf("%d", WiFi::kBackgroundScanIntervalSeconds),
              elements[3]);
  }
  {
    // Explicit bgscan method -- regular background scan frequency.
    EXPECT_TRUE(SetBgscanMethod(WPASupplicant::kNetworkBgscanMethodSimple));
    KeyValueStore params;
    EXPECT_CALL(*service, GetEndpointCount()).Times(0);
    AppendBgscan(service.get(), &params);
    Mock::VerifyAndClearExpectations(service.get());
    EXPECT_TRUE(params.Contains<string>(WPASupplicant::kNetworkPropertyBgscan));
    string config_string =
        params.Get<string>(WPASupplicant::kNetworkPropertyBgscan);
    vector<string> elements = base::SplitString(
        config_string, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    ASSERT_EQ(4, elements.size());
    EXPECT_EQ(StringPrintf("%d", WiFi::kDefaultScanIntervalSeconds),
              elements[3]);
  }
  {
    // No scan method, simply returns without appending properties
    EXPECT_TRUE(SetBgscanMethod(WPASupplicant::kNetworkBgscanMethodNone));
    KeyValueStore params;
    EXPECT_CALL(*service, GetEndpointCount()).Times(0);
    AppendBgscan(service.get(), &params);
    Mock::VerifyAndClearExpectations(service.get());
    string config_string;
    EXPECT_TRUE(params.Contains<string>(WPASupplicant::kNetworkPropertyBgscan));
    EXPECT_TRUE(
        params.Get<string>(WPASupplicant::kNetworkPropertyBgscan).empty());
  }
}

TEST_F(WiFiMainTest, StateAndIPIgnoreLinkEvent) {
  StartWiFi();
  MockWiFiServiceRefPtr service(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_CALL(*service, SetState(_)).Times(0);
  EXPECT_CALL(*dhcp_config_, RequestIP()).Times(0);
  ReportLinkUp();

  // Verify expectations now, because WiFi may cause |service| state
  // changes during TearDown().
  Mock::VerifyAndClearExpectations(service.get());
}

TEST_F(WiFiMainTest, SupplicantCompletedAlreadyConnected) {
  StartWiFi();
  MockWiFiServiceRefPtr service(
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr));
  Mock::VerifyAndClearExpectations(dhcp_config_.get());
  EXPECT_CALL(*dhcp_config_, RequestIP()).Times(0);
  // Simulate a rekeying event from the AP.  These show as transitions from
  // completed->completed from wpa_supplicant.
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_CALL(*manager(), device_info()).WillOnce(Return(device_info()));
  ReportIPConfigComplete();
  // Similarly, rekeying events after we have an IP don't trigger L3
  // configuration.  However, we treat all transitions to completed as potential
  // reassociations, so we will reenable high rates again here.
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_CALL(*service, IsConnected(nullptr)).WillOnce(Return(true));
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
}

TEST_F(WiFiMainTest, BSSAddedCreatesBSSProxy) {
  // TODO(quiche): Consider using a factory for WiFiEndpoints, so that
  // we can test the interaction between WiFi and WiFiEndpoint. (Right
  // now, we're testing across multiple layers.)
  EXPECT_CALL(*supplicant_bss_proxy_, Die()).Times(AnyNumber());
  EXPECT_CALL(*control_interface(), CreateSupplicantBSSProxy(_, _));
  StartWiFi();
  ReportBSS(RpcIdentifier("bss0"), "ssid0", "00:00:00:00:00:00", 0, 0,
            kNetworkModeInfrastructure);
}

TEST_F(WiFiMainTest, BSSRemovedDestroysBSSProxy) {
  // TODO(quiche): As for BSSAddedCreatesBSSProxy, consider using a
  // factory for WiFiEndpoints.
  // Get the pointer before we transfer ownership.
  MockSupplicantBSSProxy* proxy = supplicant_bss_proxy_.get();
  EXPECT_CALL(*proxy, Die());
  StartWiFi();
  RpcIdentifier bss_path(MakeNewEndpointAndService(0, 0, nullptr, nullptr));
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(_)).WillOnce(Return(nullptr));
  RemoveBSS(bss_path);
  // Check this now, to make sure RemoveBSS killed the proxy (rather
  // than TearDown).
  Mock::VerifyAndClearExpectations(proxy);
}

TEST_F(WiFiMainTest, FlushBSSOnResume) {
  const struct timeval resume_time = {1, 0};
  const struct timeval scan_done_time = {6, 0};

  StartWiFi();

  EXPECT_CALL(time_, GetTimeMonotonic(_))
      .WillOnce(DoAll(SetArgPointee<0>(resume_time), Return(0)))
      .WillOnce(DoAll(SetArgPointee<0>(scan_done_time), Return(0)));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              FlushBSS(WiFi::kMaxBSSResumeAgeSeconds + 5));
  OnAfterResume();
  ReportScanDone();
}

TEST_F(WiFiMainTest, CallWakeOnWiFi_OnScanDone) {
  StartWiFi();

  // Call WakeOnWiFi::OnNoAutoConnectableServicesAfterScan if we find 0 auto-
  // connectable services.
  EXPECT_CALL(*wifi_provider(), NumAutoConnectableServices())
      .WillOnce(Return(0));
  EXPECT_TRUE(wifi()->IsIdle());
  EXPECT_CALL(*wake_on_wifi_, OnNoAutoConnectableServicesAfterScan(_, _, _));
  ReportScanDone();

  // If we have 1 or more auto-connectable services, do not call
  // WakeOnWiFi::OnNoAutoConnectableServicesAfterScan.
  EXPECT_CALL(*wifi_provider(), NumAutoConnectableServices())
      .WillOnce(Return(1));
  EXPECT_TRUE(wifi()->IsIdle());
  EXPECT_CALL(*wake_on_wifi_, OnNoAutoConnectableServicesAfterScan(_, _, _))
      .Times(0);
  ReportScanDone();

  // If the WiFi device is not Idle, do not call
  // WakeOnWiFi::OnNoAutoConnectableServicesAfterScan.
  SetCurrentService(MakeMockService(kSecurityWep));
  EXPECT_FALSE(wifi()->IsIdle());
  EXPECT_CALL(*wifi_provider(), NumAutoConnectableServices())
      .WillOnce(Return(0));
  EXPECT_CALL(*wake_on_wifi_, OnNoAutoConnectableServicesAfterScan(_, _, _))
      .Times(0);
  ReportScanDone();
}

TEST_F(WiFiMainTest, ScanTimerIdle) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  ReportScanDone();
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());

  EXPECT_CALL(*manager(), OnDeviceGeolocationInfoUpdated(_));
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_));
  FireScanTimer();
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_FALSE(GetScanTimer().IsCancelled());  // Automatically re-armed.
}

TEST_F(WiFiMainTest, ScanTimerScanning) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());

  // Should not call Scan, since we're already scanning.
  // (Scanning is triggered by StartWiFi.)
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  FireScanTimer();
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_FALSE(GetScanTimer().IsCancelled());  // Automatically re-armed.
}

TEST_F(WiFiMainTest, ScanTimerConnecting) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  MockWiFiServiceRefPtr service =
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr);
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());

  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  FireScanTimer();
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_FALSE(GetScanTimer().IsCancelled());  // Automatically re-armed.
}

TEST_F(WiFiMainTest, ScanTimerSuspending) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  ReportScanDone();
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());

  EXPECT_CALL(*manager(), OnDeviceGeolocationInfoUpdated(_));
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_CALL(*manager(), IsSuspending()).WillOnce(Return(true));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  FireScanTimer();
  event_dispatcher_->DispatchPendingEvents();
  EXPECT_TRUE(GetScanTimer().IsCancelled());  // Do not re-arm.
}

TEST_F(WiFiMainTest, ScanTimerReconfigured) {
  StartWiFi();
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());

  SetScanInterval(1, nullptr);
  EXPECT_FALSE(GetScanTimer().IsCancelled());
}

TEST_F(WiFiMainTest, ScanTimerResetOnScanDone) {
  StartWiFi();
  CancelScanTimer();
  EXPECT_TRUE(GetScanTimer().IsCancelled());

  ReportScanDone();
  EXPECT_FALSE(GetScanTimer().IsCancelled());
}

TEST_F(WiFiMainTest, ScanTimerStopOnZeroInterval) {
  StartWiFi();
  EXPECT_FALSE(GetScanTimer().IsCancelled());

  SetScanInterval(0, nullptr);
  EXPECT_TRUE(GetScanTimer().IsCancelled());
}

TEST_F(WiFiMainTest, ScanOnDisconnectWithHidden) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  vector<uint8_t> kSSID(1, 'a');
  ByteArrays ssids;
  ssids.push_back(kSSID);
  ExpectScanIdle();
  EXPECT_CALL(*wifi_provider(), GetHiddenSSIDList())
      .WillRepeatedly(Return(ssids));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              Scan(ScanRequestHasHiddenSSID(kSSID)));
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, NoScanOnDisconnectWithoutHidden) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(0);
  EXPECT_CALL(*wifi_provider(), GetHiddenSSIDList())
      .WillRepeatedly(Return(ByteArrays()));
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  event_dispatcher_->DispatchPendingEvents();
}

TEST_F(WiFiMainTest, LinkMonitorFailure) {
  ScopedMockLog log;
  auto link_monitor = new StrictMock<MockLinkMonitor>();
  StartWiFi();
  SetLinkMonitor(link_monitor);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(*link_monitor, IsGatewayFound())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));

  // We never had an ARP reply during this connection, so we assume
  // the problem is gateway, rather than link.
  EXPECT_CALL(log,
              Log(logging::LOG_INFO, _, EndsWith("gateway was never found.")))
      .Times(1);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Reattach()).Times(0);
  OnLinkMonitorFailure();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  // No supplicant, so we can't Reattach.
  OnSupplicantVanish();
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith("Cannot reassociate.")))
      .Times(1);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Reattach()).Times(0);
  OnLinkMonitorFailure();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  // Normal case: call Reattach.
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  SetCurrentService(service);
  OnSupplicantAppear();
  EXPECT_CALL(log, Log(logging::LOG_INFO, _, EndsWith("Called Reattach().")))
      .Times(1);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Reattach())
      .WillOnce(Return(true));
  OnLinkMonitorFailure();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());

  // Service is unreliable, skip reassociate attempt.
  service->set_unreliable(true);
  EXPECT_CALL(
      log, Log(logging::LOG_INFO, _, EndsWith("skipping reassociate attempt.")))
      .Times(1);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Reattach()).Times(0);
  OnLinkMonitorFailure();
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
}

TEST_F(WiFiMainTest, SuspectCredentialsOpen) {
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).Times(0);
  EXPECT_FALSE(SuspectCredentials(service, nullptr));
}

TEST_F(WiFiMainTest, SuspectCredentialsWPA) {
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityWpa);
  ReportStateChanged(WPASupplicant::kInterfaceState4WayHandshake);
  EXPECT_CALL(*service, AddSuspectedCredentialFailure())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_FALSE(SuspectCredentials(service, nullptr));
  Service::ConnectFailure failure;
  EXPECT_TRUE(SuspectCredentials(service, &failure));
  EXPECT_EQ(Service::kFailureBadPassphrase, failure);
}

TEST_F(WiFiMainTest, SuspectCredentialsWEP) {
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityWep);
  ExpectConnecting();
  InitiateConnect(service);
  SetCurrentService(service);

  // These expectations are very much like SetupConnectedService except
  // that we verify that ResetSupsectCredentialFailures() is not called
  // on the service just because supplicant entered the Completed state.
  EXPECT_CALL(*service, SetState(Service::kStateConfiguring));
  EXPECT_CALL(*service, ResetSuspectedCredentialFailures()).Times(0);
  EXPECT_CALL(*dhcp_provider(), CreateIPv4Config(_, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(*dhcp_config_, RequestIP()).Times(AnyNumber());
  EXPECT_CALL(*manager(), device_info()).WillRepeatedly(Return(device_info()));
  EXPECT_CALL(*device_info(), GetByteCounts(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(0LL), Return(true)));
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);

  Mock::VerifyAndClearExpectations(device_info());
  Mock::VerifyAndClearExpectations(service.get());

  // Successful connect.
  EXPECT_CALL(*service, ResetSuspectedCredentialFailures());
  ReportConnected();

  EXPECT_CALL(*device_info(), GetByteCounts(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(1LL), Return(true)))
      .WillOnce(DoAll(SetArgPointee<2>(0LL), Return(true)))
      .WillOnce(DoAll(SetArgPointee<2>(0LL), Return(true)));

  // If there was an increased byte-count while we were timing out DHCP,
  // this should be considered a DHCP failure and not a credential failure.
  EXPECT_CALL(*service, ResetSuspectedCredentialFailures()).Times(0);
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureDHCP, _,
                                              HasSubstr("OnIPConfigFailure")));
  ReportIPConfigFailure();
  Mock::VerifyAndClearExpectations(service.get());

  // Connection failed during DHCP but service does not (yet) believe this is
  // due to a passphrase issue.
  EXPECT_CALL(*service, AddSuspectedCredentialFailure())
      .WillOnce(Return(false));
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureDHCP, _,
                                              HasSubstr("OnIPConfigFailure")));
  ReportIPConfigFailure();
  Mock::VerifyAndClearExpectations(service.get());

  // Connection failed during DHCP and service believes this is due to a
  // passphrase issue.
  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).WillOnce(Return(true));
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureBadPassphrase, _,
                                              HasSubstr("OnIPConfigFailure")));
  ReportIPConfigFailure();
}

TEST_F(WiFiMainTest, SuspectCredentialsEAPInProgress) {
  MockWiFiServiceRefPtr service = MakeMockService(kSecurity8021x);
  EXPECT_CALL(*eap_state_handler_, is_eap_in_progress())
      .WillOnce(Return(false))
      .WillOnce(Return(true))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).Times(0);
  EXPECT_FALSE(SuspectCredentials(service, nullptr));
  Mock::VerifyAndClearExpectations(service.get());

  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).WillOnce(Return(true));
  Service::ConnectFailure failure;
  EXPECT_TRUE(SuspectCredentials(service, &failure));
  EXPECT_EQ(Service::kFailureEAPAuthentication, failure);
  Mock::VerifyAndClearExpectations(service.get());

  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).Times(0);
  EXPECT_FALSE(SuspectCredentials(service, nullptr));
  Mock::VerifyAndClearExpectations(service.get());

  EXPECT_CALL(*service, AddSuspectedCredentialFailure())
      .WillOnce(Return(false));
  EXPECT_FALSE(SuspectCredentials(service, nullptr));
}

TEST_F(WiFiMainTest, SuspectCredentialsYieldFailureWPA) {
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityWpa);
  SetPendingService(service);
  ReportStateChanged(WPASupplicant::kInterfaceState4WayHandshake);

  ExpectScanIdle();
  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).WillOnce(Return(true));
  EXPECT_CALL(*service, SetFailure(Service::kFailureBadPassphrase));
  EXPECT_CALL(*service, SetState(Service::kStateIdle));
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith(kErrorBadPassphrase)));
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
}

TEST_F(WiFiMainTest, SuspectCredentialsYieldFailureEAP) {
  MockWiFiServiceRefPtr service = MakeMockService(kSecurity8021x);
  SetCurrentService(service);

  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(*service, SetState(Service::kStateIdle));
  // Ensure that we retrieve is_eap_in_progress() before resetting the
  // EAP handler's state.
  InSequence seq;
  EXPECT_CALL(*eap_state_handler_, is_eap_in_progress()).WillOnce(Return(true));
  EXPECT_CALL(*service, AddSuspectedCredentialFailure()).WillOnce(Return(true));
  EXPECT_CALL(*service, SetFailure(Service::kFailureEAPAuthentication));
  EXPECT_CALL(
      log, Log(logging::LOG_ERROR, _, EndsWith(kErrorEapAuthenticationFailed)));
  EXPECT_CALL(*eap_state_handler_, Reset());
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
}

TEST_F(WiFiMainTest, ReportConnectedToServiceAfterWake_CallsWakeOnWiFi) {
  EXPECT_CALL(*wake_on_wifi_, ReportConnectedToServiceAfterWake(
                                  IsConnectedToCurrentService(), _));
  ReportConnectedToServiceAfterWake();
}

// Scanning tests will use a mock of the event dispatcher instead of a real
// one.
class WiFiTimerTest : public WiFiObjectTest {
 public:
  WiFiTimerTest()
      : WiFiObjectTest(std::make_unique<StrictMock<MockEventDispatcher>>()),
        mock_dispatcher_(static_cast<StrictMock<MockEventDispatcher>*>(
            event_dispatcher_.get())) {}

 protected:
  void ExpectInitialScanSequence();

  StrictMock<MockEventDispatcher>* mock_dispatcher_;
};

void WiFiTimerTest::ExpectInitialScanSequence() {
  // Choose a number of iterations some multiple higher than the fast scan
  // count.
  const int kScanTimes = WiFi::kNumFastScanAttempts * 4;

  // Each time we call FireScanTimer() below, WiFi will post a task to actually
  // run Scan() on the wpa_supplicant proxy.
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, 0)).Times(kScanTimes);
  {
    InSequence seq;
    // The scans immediately after the initial scan should happen at the short
    // interval.  If we add the initial scan (not invoked in this function) to
    // the ones in the expectation below, we get WiFi::kNumFastScanAttempts at
    // the fast scan interval.
    EXPECT_CALL(*mock_dispatcher_,
                PostDelayedTask(_, _, WiFi::kFastScanIntervalSeconds * 1000))
        .Times(WiFi::kNumFastScanAttempts - 1);

    // After this, the WiFi device should use the normal scan interval.
    EXPECT_CALL(*mock_dispatcher_,
                PostDelayedTask(_, _, GetScanInterval() * 1000))
        .Times(kScanTimes - WiFi::kNumFastScanAttempts + 1);

    for (int i = 0; i < kScanTimes; i++) {
      FireScanTimer();
    }
  }
}

TEST_F(WiFiTimerTest, FastRescan) {
  // This is to cover calls to PostDelayedTask by WakeOnWiFi::StartMetricsTimer.
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, _)).Times(AnyNumber());
  // This PostTask is a result of the call to Scan(nullptr), and is meant to
  // post a task to call Scan() on the wpa_supplicant proxy immediately.
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, 0));
  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, WiFi::kFastScanIntervalSeconds * 1000));
  StartWiFi();

  ExpectInitialScanSequence();

  // If we end up disconnecting, the sequence should repeat.
  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, WiFi::kFastScanIntervalSeconds * 1000));
  RestartFastScanAttempts();

  ExpectInitialScanSequence();
}

TEST_F(WiFiTimerTest, ReconnectTimer) {
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, 0)).Times(AnyNumber());
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, _)).Times(AnyNumber());
  StartWiFi();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  Mock::VerifyAndClearExpectations(&*mock_dispatcher_);

  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, GetReconnectTimeoutSeconds() * 1000))
      .Times(1);
  StartReconnectTimer();
  Mock::VerifyAndClearExpectations(&*mock_dispatcher_);
  StopReconnectTimer();

  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, GetReconnectTimeoutSeconds() * 1000))
      .Times(1);
  StartReconnectTimer();
  Mock::VerifyAndClearExpectations(&*mock_dispatcher_);
  GetReconnectTimeoutCallback().callback().Run();

  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, GetReconnectTimeoutSeconds() * 1000))
      .Times(1);
  StartReconnectTimer();
  Mock::VerifyAndClearExpectations(&*mock_dispatcher_);

  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, GetReconnectTimeoutSeconds() * 1000))
      .Times(0);
  StartReconnectTimer();
}

TEST_F(WiFiTimerTest, RequestStationInfo) {
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, 0)).Times(AnyNumber());
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, _)).Times(AnyNumber());

  // Setup a connected service here while we have the expectations above set.
  StartWiFi();
  MockWiFiServiceRefPtr service =
      SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  RpcIdentifier connected_bss = GetSupplicantBSS();
  Mock::VerifyAndClearExpectations(&*mock_dispatcher_);

  EXPECT_CALL(netlink_manager_, SendNl80211Message(_, _, _, _)).Times(0);
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, _)).Times(0);
  NiceScopedMockLog log;

  // There is no current_service_.
  EXPECT_CALL(log, Log(_, _, HasSubstr("we are not connected")));
  SetCurrentService(nullptr);
  RequestStationInfo();

  // current_service_ is not connected.
  EXPECT_CALL(*service, IsConnected(nullptr)).WillOnce(Return(false));
  SetCurrentService(service);
  EXPECT_CALL(log, Log(_, _, HasSubstr("we are not connected")));
  RequestStationInfo();

  // Endpoint does not exist in endpoint_by_rpcid_.
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  SetSupplicantBSS(
      RpcIdentifier("/some/path/that/does/not/exist/in/endpoint_by_rpcid"));
  EXPECT_CALL(
      log,
      Log(_, _, HasSubstr("Can't get endpoint for current supplicant BSS")));
  RequestStationInfo();
  Mock::VerifyAndClearExpectations(&netlink_manager_);
  Mock::VerifyAndClearExpectations(&*mock_dispatcher_);

  // We successfully trigger a request to get the station and start a timer
  // for the next call.
  EXPECT_CALL(netlink_manager_,
              SendNl80211Message(
                  IsNl80211Command(kNl80211FamilyId, NL80211_CMD_GET_STATION),
                  _, _, _));
  EXPECT_CALL(
      *mock_dispatcher_,
      PostDelayedTask(_, _, WiFi::kRequestStationInfoPeriodSeconds * 1000));
  SetSupplicantBSS(connected_bss);
  RequestStationInfo();

  // Now test that a properly formatted New Station message updates strength.
  NewStationMessage new_station;
  new_station.attributes()->CreateRawAttribute(NL80211_ATTR_MAC, "BSSID");

  // Confirm that up until now no link statistics exist.
  KeyValueStore link_statistics = GetLinkStatistics();
  EXPECT_TRUE(link_statistics.IsEmpty());

  // Use a reference to the endpoint instance in the WiFi device instead of
  // the copy returned by SetupConnectedService().
  WiFiEndpointRefPtr endpoint = GetEndpointMap().begin()->second;
  new_station.attributes()->SetRawAttributeValue(
      NL80211_ATTR_MAC, ByteString::CreateFromHexString(endpoint->bssid_hex()));
  new_station.attributes()->CreateNestedAttribute(NL80211_ATTR_STA_INFO,
                                                  "Station Info");
  AttributeListRefPtr station_info;
  new_station.attributes()->GetNestedAttributeList(NL80211_ATTR_STA_INFO,
                                                   &station_info);
  station_info->CreateU8Attribute(NL80211_STA_INFO_SIGNAL, "Signal");
  const int kSignalValue = -20;
  station_info->SetU8AttributeValue(NL80211_STA_INFO_SIGNAL, kSignalValue);
  station_info->CreateU8Attribute(NL80211_STA_INFO_SIGNAL_AVG, "SignalAverage");
  const int kSignalAvgValue = -40;
  station_info->SetU8AttributeValue(NL80211_STA_INFO_SIGNAL_AVG,
                                    kSignalAvgValue);
  station_info->CreateU32Attribute(NL80211_STA_INFO_INACTIVE_TIME,
                                   "InactiveTime");
  const int32_t kInactiveTime = 100;
  station_info->SetU32AttributeValue(NL80211_STA_INFO_INACTIVE_TIME,
                                     kInactiveTime);
  station_info->CreateU32Attribute(NL80211_STA_INFO_RX_PACKETS,
                                   "ReceivedSuccesses");
  const int32_t kReceiveSuccesses = 200;
  station_info->SetU32AttributeValue(NL80211_STA_INFO_RX_PACKETS,
                                     kReceiveSuccesses);
  station_info->CreateU32Attribute(NL80211_STA_INFO_TX_FAILED,
                                   "TransmitFailed");
  const int32_t kTransmitFailed = 300;
  station_info->SetU32AttributeValue(NL80211_STA_INFO_TX_FAILED,
                                     kTransmitFailed);
  station_info->CreateU32Attribute(NL80211_STA_INFO_TX_PACKETS,
                                   "TransmitSuccesses");
  const int32_t kTransmitSuccesses = 400;
  station_info->SetU32AttributeValue(NL80211_STA_INFO_TX_PACKETS,
                                     kTransmitSuccesses);
  station_info->CreateU32Attribute(NL80211_STA_INFO_TX_RETRIES,
                                   "TransmitRetries");
  const int32_t kTransmitRetries = 500;
  station_info->SetU32AttributeValue(NL80211_STA_INFO_TX_RETRIES,
                                     kTransmitRetries);
  station_info->CreateNestedAttribute(NL80211_STA_INFO_TX_BITRATE,
                                      "TX Bitrate Info");

  // Embed transmit bitrate info within the station info element.
  AttributeListRefPtr bitrate_info;
  station_info->GetNestedAttributeList(NL80211_STA_INFO_TX_BITRATE,
                                       &bitrate_info);
  bitrate_info->CreateU16Attribute(NL80211_RATE_INFO_BITRATE, "Bitrate");
  const int16_t kBitrate = 6005;
  bitrate_info->SetU16AttributeValue(NL80211_RATE_INFO_BITRATE, kBitrate);
  bitrate_info->CreateU8Attribute(NL80211_RATE_INFO_MCS, "MCS");
  const int16_t kMCS = 7;
  bitrate_info->SetU8AttributeValue(NL80211_RATE_INFO_MCS, kMCS);
  bitrate_info->CreateFlagAttribute(NL80211_RATE_INFO_40_MHZ_WIDTH, "HT40");
  bitrate_info->SetFlagAttributeValue(NL80211_RATE_INFO_40_MHZ_WIDTH, true);
  bitrate_info->CreateFlagAttribute(NL80211_RATE_INFO_SHORT_GI, "SGI");
  bitrate_info->SetFlagAttributeValue(NL80211_RATE_INFO_SHORT_GI, false);
  station_info->SetNestedAttributeHasAValue(NL80211_STA_INFO_TX_BITRATE);

  new_station.attributes()->SetNestedAttributeHasAValue(NL80211_ATTR_STA_INFO);

  EXPECT_NE(kSignalValue, endpoint->signal_strength());
  EXPECT_CALL(*wifi_provider(), OnEndpointUpdated(EndpointMatch(endpoint)));
  EXPECT_CALL(*metrics(), NotifyWifiTxBitrate(kBitrate / 10));
  AttributeListConstRefPtr station_info_prime;
  ReportReceivedStationInfo(new_station);
  EXPECT_EQ(kSignalValue, endpoint->signal_strength());

  link_statistics = GetLinkStatistics();
  ASSERT_FALSE(link_statistics.IsEmpty());
  ASSERT_TRUE(link_statistics.Contains<int32_t>(kLastReceiveSignalDbmProperty));
  EXPECT_EQ(kSignalValue,
            link_statistics.Get<int32_t>(kLastReceiveSignalDbmProperty));
  ASSERT_TRUE(
      link_statistics.Contains<int32_t>(kAverageReceiveSignalDbmProperty));
  EXPECT_EQ(kSignalAvgValue,
            link_statistics.Get<int32_t>(kAverageReceiveSignalDbmProperty));
  ASSERT_TRUE(
      link_statistics.Contains<uint32_t>(kInactiveTimeMillisecondsProperty));
  EXPECT_EQ(kInactiveTime,
            link_statistics.Get<uint32_t>(kInactiveTimeMillisecondsProperty));
  ASSERT_TRUE(
      link_statistics.Contains<uint32_t>(kPacketReceiveSuccessesProperty));
  EXPECT_EQ(kReceiveSuccesses,
            link_statistics.Get<uint32_t>(kPacketReceiveSuccessesProperty));
  ASSERT_TRUE(
      link_statistics.Contains<uint32_t>(kPacketTransmitFailuresProperty));
  EXPECT_EQ(kTransmitFailed,
            link_statistics.Get<uint32_t>(kPacketTransmitFailuresProperty));
  ASSERT_TRUE(
      link_statistics.Contains<uint32_t>(kPacketTransmitSuccessesProperty));
  EXPECT_EQ(kTransmitSuccesses,
            link_statistics.Get<uint32_t>(kPacketTransmitSuccessesProperty));
  ASSERT_TRUE(link_statistics.Contains<uint32_t>(kTransmitRetriesProperty));
  EXPECT_EQ(kTransmitRetries,
            link_statistics.Get<uint32_t>(kTransmitRetriesProperty));
  EXPECT_EQ(StringPrintf("%d.%d MBit/s MCS %d 40MHz", kBitrate / 10,
                         kBitrate % 10, kMCS),
            link_statistics.Lookup<string>(kTransmitBitrateProperty, ""));
  EXPECT_EQ("", link_statistics.Lookup<string>(kReceiveBitrateProperty, ""));

  // New station info with VHT rate parameters.
  NewStationMessage new_vht_station;
  new_vht_station.attributes()->CreateRawAttribute(NL80211_ATTR_MAC, "BSSID");

  new_vht_station.attributes()->SetRawAttributeValue(
      NL80211_ATTR_MAC, ByteString::CreateFromHexString(endpoint->bssid_hex()));
  new_vht_station.attributes()->CreateNestedAttribute(NL80211_ATTR_STA_INFO,
                                                      "Station Info");
  new_vht_station.attributes()->GetNestedAttributeList(NL80211_ATTR_STA_INFO,
                                                       &station_info);
  station_info->CreateU8Attribute(NL80211_STA_INFO_SIGNAL, "Signal");
  station_info->SetU8AttributeValue(NL80211_STA_INFO_SIGNAL, kSignalValue);
  station_info->CreateNestedAttribute(NL80211_STA_INFO_RX_BITRATE,
                                      "RX Bitrate Info");
  station_info->CreateNestedAttribute(NL80211_STA_INFO_TX_BITRATE,
                                      "TX Bitrate Info");

  // Embed transmit VHT bitrate info within the station info element.
  station_info->GetNestedAttributeList(NL80211_STA_INFO_TX_BITRATE,
                                       &bitrate_info);
  bitrate_info->CreateU32Attribute(NL80211_RATE_INFO_BITRATE32, "Bitrate32");
  const int32_t kVhtBitrate = 70000;
  bitrate_info->SetU32AttributeValue(NL80211_RATE_INFO_BITRATE32, kVhtBitrate);
  bitrate_info->CreateU8Attribute(NL80211_RATE_INFO_VHT_MCS, "VHT-MCS");
  const int8_t kVhtMCS = 7;
  bitrate_info->SetU8AttributeValue(NL80211_RATE_INFO_VHT_MCS, kVhtMCS);
  bitrate_info->CreateU8Attribute(NL80211_RATE_INFO_VHT_NSS, "VHT-NSS");
  const int8_t kVhtNSS = 1;
  bitrate_info->SetU8AttributeValue(NL80211_RATE_INFO_VHT_NSS, kVhtNSS);
  bitrate_info->CreateFlagAttribute(NL80211_RATE_INFO_80_MHZ_WIDTH, "VHT80");
  bitrate_info->SetFlagAttributeValue(NL80211_RATE_INFO_80_MHZ_WIDTH, true);
  bitrate_info->CreateFlagAttribute(NL80211_RATE_INFO_SHORT_GI, "SGI");
  bitrate_info->SetFlagAttributeValue(NL80211_RATE_INFO_SHORT_GI, false);
  station_info->SetNestedAttributeHasAValue(NL80211_STA_INFO_TX_BITRATE);

  // Embed receive VHT bitrate info within the station info element.
  station_info->GetNestedAttributeList(NL80211_STA_INFO_RX_BITRATE,
                                       &bitrate_info);
  bitrate_info->CreateU32Attribute(NL80211_RATE_INFO_BITRATE32, "Bitrate32");
  bitrate_info->SetU32AttributeValue(NL80211_RATE_INFO_BITRATE32, kVhtBitrate);
  bitrate_info->CreateU8Attribute(NL80211_RATE_INFO_VHT_MCS, "VHT-MCS");
  bitrate_info->SetU8AttributeValue(NL80211_RATE_INFO_VHT_MCS, kVhtMCS);
  bitrate_info->CreateU8Attribute(NL80211_RATE_INFO_VHT_NSS, "VHT-NSS");
  bitrate_info->SetU8AttributeValue(NL80211_RATE_INFO_VHT_NSS, kVhtNSS);
  bitrate_info->CreateFlagAttribute(NL80211_RATE_INFO_80_MHZ_WIDTH, "VHT80");
  bitrate_info->SetFlagAttributeValue(NL80211_RATE_INFO_80_MHZ_WIDTH, true);
  bitrate_info->CreateFlagAttribute(NL80211_RATE_INFO_SHORT_GI, "SGI");
  bitrate_info->SetFlagAttributeValue(NL80211_RATE_INFO_SHORT_GI, false);
  station_info->SetNestedAttributeHasAValue(NL80211_STA_INFO_RX_BITRATE);

  new_vht_station.attributes()->SetNestedAttributeHasAValue(
      NL80211_ATTR_STA_INFO);

  EXPECT_CALL(*metrics(), NotifyWifiTxBitrate(kVhtBitrate / 10));

  ReportReceivedStationInfo(new_vht_station);

  link_statistics = GetLinkStatistics();
  {
    string rate =
        StringPrintf("%d.%d MBit/s VHT-MCS %d 80MHz VHT-NSS %d",
                     kVhtBitrate / 10, kVhtBitrate % 10, kVhtMCS, kVhtNSS);
    EXPECT_EQ(rate,
              link_statistics.Lookup<string>(kTransmitBitrateProperty, ""));
    EXPECT_EQ(rate,
              link_statistics.Lookup<string>(kReceiveBitrateProperty, ""));
  }
}

TEST_F(WiFiTimerTest, ResumeDispatchesConnectivityReportTask) {
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, 0)).Times(AnyNumber());
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, _)).Times(AnyNumber());
  StartWiFi();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(
                  _, _, WiFi::kPostWakeConnectivityReportDelayMilliseconds));
  OnAfterResume();
}

TEST_F(WiFiTimerTest, StartScanTimer_ReturnsImmediately) {
  Error e;
  // Return immediately if scan interval is 0.
  SetScanInterval(0, &e);
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, _)).Times(0);
  StartScanTimer();
}

TEST_F(WiFiTimerTest, StartScanTimer_HaveFastScansRemaining) {
  Error e;
  const int scan_interval = 10;
  SetScanInterval(scan_interval, &e);
  SetFastScansRemaining(1);
  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, WiFi::kFastScanIntervalSeconds * 1000));
  StartScanTimer();
}

TEST_F(WiFiTimerTest, StartScanTimer_NoFastScansRemaining) {
  Error e;
  const int scan_interval = 10;
  SetScanInterval(scan_interval, &e);
  SetFastScansRemaining(0);
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, scan_interval * 1000));
  StartScanTimer();
}

TEST_F(WiFiMainTest, EAPCertification) {
  MockWiFiServiceRefPtr service = MakeMockService(kSecurity8021x);
  EXPECT_CALL(*service, AddEAPCertification(_, _)).Times(0);

  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith("no current service.")));
  KeyValueStore args;
  ReportCertification(args);
  Mock::VerifyAndClearExpectations(&log);

  SetCurrentService(service);
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith("no depth parameter.")));
  ReportCertification(args);
  Mock::VerifyAndClearExpectations(&log);

  const uint32_t kDepth = 123;
  args.Set<uint32_t>(WPASupplicant::kInterfacePropertyDepth, kDepth);

  EXPECT_CALL(log,
              Log(logging::LOG_ERROR, _, EndsWith("no subject parameter.")));
  ReportCertification(args);
  Mock::VerifyAndClearExpectations(&log);

  const string kSubject("subject");
  args.Set<string>(WPASupplicant::kInterfacePropertySubject, kSubject);
  EXPECT_CALL(*service, AddEAPCertification(kSubject, kDepth)).Times(1);
  ReportCertification(args);
}

TEST_F(WiFiTimerTest, ScanDoneDispatchesTasks) {
  // Dispatch WiFi::ScanFailedTask if scan failed.
  EXPECT_TRUE(ScanFailedCallbackIsCancelled());
  EXPECT_CALL(*mock_dispatcher_,
              PostDelayedTask(_, _, WiFi::kPostScanFailedDelayMilliseconds));
  ScanDone(false);
  EXPECT_FALSE(ScanFailedCallbackIsCancelled());

  // Dispatch WiFi::ScanDoneTask if scan succeeded, and cancel the scan failed
  // callback if has been dispatched.
  EXPECT_CALL(*mock_dispatcher_, PostDelayedTask(_, _, 0));
  ScanDone(true);
  EXPECT_TRUE(ScanFailedCallbackIsCancelled());
}

TEST_F(WiFiMainTest, EAPEvent) {
  StartWiFi();
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith("no current service.")));
  EXPECT_CALL(*eap_state_handler_, ParseStatus(_, _, _)).Times(0);
  const string kEAPStatus("eap-status");
  const string kEAPParameter("eap-parameter");
  ReportEAPEvent(kEAPStatus, kEAPParameter);
  Mock::VerifyAndClearExpectations(&log);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());

  MockWiFiServiceRefPtr service = MakeMockService(kSecurity8021x);
  EXPECT_CALL(*service, SetFailure(_)).Times(0);
  EXPECT_CALL(*eap_state_handler_, ParseStatus(kEAPStatus, kEAPParameter, _));
  SetCurrentService(service);
  ReportEAPEvent(kEAPStatus, kEAPParameter);
  Mock::VerifyAndClearExpectations(service.get());
  Mock::VerifyAndClearExpectations(eap_state_handler_);

  EXPECT_CALL(*eap_state_handler_, ParseStatus(kEAPStatus, kEAPParameter, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(Service::kFailureOutOfRange), Return(false)));
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureOutOfRange, _,
                                              HasSubstr("EAPEventTask")));
  ReportEAPEvent(kEAPStatus, kEAPParameter);

  MockEapCredentials* eap = new MockEapCredentials();
  service->eap_.reset(eap);  // Passes ownership.
  const RpcIdentifier kNetworkRpcId("/service/network/rpcid");
  SetServiceNetworkRpcId(service, kNetworkRpcId);
  EXPECT_CALL(*eap_state_handler_, ParseStatus(kEAPStatus, kEAPParameter, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(Service::kFailurePinMissing), Return(false)));
  // We need a real string object since it will be returned by reference below.
  const string kEmptyPin;
  EXPECT_CALL(*eap, pin()).WillOnce(ReturnRef(kEmptyPin));
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailurePinMissing, _,
                                              HasSubstr("EAPEventTask")));
  ReportEAPEvent(kEAPStatus, kEAPParameter);

  EXPECT_CALL(*eap_state_handler_, ParseStatus(kEAPStatus, kEAPParameter, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(Service::kFailurePinMissing), Return(false)));
  // We need a real string object since it will be returned by reference below.
  const string kPin("000000");
  EXPECT_CALL(*eap, pin()).WillOnce(ReturnRef(kPin));
  EXPECT_CALL(*service, DisconnectWithFailure(_, _, _)).Times(0);
  EXPECT_CALL(
      *GetSupplicantInterfaceProxy(),
      NetworkReply(kNetworkRpcId,
                   StrEq(WPASupplicant::kEAPRequestedParameterPin), Ref(kPin)));
  ReportEAPEvent(kEAPStatus, kEAPParameter);
}

TEST_F(WiFiMainTest, PendingScanDoesNotCrashAfterStop) {
  // Scan is one task that should be skipped after Stop. Others are
  // skipped by the same mechanism (invalidating weak pointers), so we
  // don't test them individually.
  //
  // Note that we can't test behavior by setting expectations on the
  // supplicant_interface_proxy_, since that is destroyed when we StopWiFi().
  StartWiFi();
  StopWiFi();
  event_dispatcher_->DispatchPendingEvents();
}

struct BSS {
  RpcIdentifier bsspath;
  string ssid;
  string bssid;
  int16_t signal_strength;
  uint16_t frequency;
  const char* mode;
};

TEST_F(WiFiMainTest, GetGeolocationObjects) {
  BSS bsses[] = {
      {RpcIdentifier("bssid1"), "ssid1", "00:00:00:00:00:00", 5,
       Metrics::kWiFiFrequency2412, kNetworkModeInfrastructure},
      {RpcIdentifier("bssid2"), "ssid2", "01:00:00:00:00:00", 30,
       Metrics::kWiFiFrequency5170, kNetworkModeInfrastructure},
      // Same SSID but different BSSID is an additional geolocation object.
      {RpcIdentifier("bssid3"), "ssid1", "02:00:00:00:00:00", 100, 0,
       kNetworkModeInfrastructure}};
  StartWiFi();
  vector<GeolocationInfo> objects;
  EXPECT_EQ(objects.size(), 0);

  for (size_t i = 0; i < base::size(bsses); ++i) {
    ReportBSS(bsses[i].bsspath, bsses[i].ssid, bsses[i].bssid,
              bsses[i].signal_strength, bsses[i].frequency, bsses[i].mode);
    objects = wifi()->GetGeolocationObjects();
    EXPECT_EQ(objects.size(), i + 1);

    GeolocationInfo expected_info;
    expected_info[kGeoMacAddressProperty] = bsses[i].bssid;
    expected_info[kGeoSignalStrengthProperty] =
        StringPrintf("%d", bsses[i].signal_strength);
    expected_info[kGeoChannelProperty] =
        StringPrintf("%d", Metrics::WiFiFrequencyToChannel(bsses[i].frequency));
    EXPECT_EQ(expected_info, objects[i]);
  }
}

TEST_F(WiFiMainTest, SetSupplicantDebugLevel) {
  MockSupplicantProcessProxy* process_proxy = supplicant_process_proxy_;

  // With WiFi not yet started, nothing interesting (including a crash) should
  // happen.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_)).Times(0);
  EXPECT_CALL(*process_proxy, SetDebugLevel(_)).Times(0);
  ReportWiFiDebugScopeChanged(true);

  // This unit test turns on WiFi debugging, so when we start WiFi, we should
  // check but not set the debug level if we return the "debug" level.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelDebug)),
                      Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(_)).Times(0);
  StartWiFi();
  Mock::VerifyAndClearExpectations(process_proxy);

  // If WiFi debugging is toggled and wpa_supplicant reports debugging
  // is set to some unmanaged level, WiFi should leave it alone.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelError)),
                      Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelError)),
                      Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelExcessive)),
                Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelExcessive)),
                Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelMsgDump)),
                Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelMsgDump)),
                Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelWarning)),
                Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelWarning)),
                Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(_)).Times(0);
  ReportWiFiDebugScopeChanged(true);
  ReportWiFiDebugScopeChanged(false);
  ReportWiFiDebugScopeChanged(true);
  ReportWiFiDebugScopeChanged(false);
  ReportWiFiDebugScopeChanged(true);
  ReportWiFiDebugScopeChanged(false);
  ReportWiFiDebugScopeChanged(true);
  ReportWiFiDebugScopeChanged(false);
  Mock::VerifyAndClearExpectations(process_proxy);

  // If WiFi debugging is turned off and wpa_supplicant reports debugging
  // is turned on, WiFi should turn supplicant debugging off.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelDebug)),
                      Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(WPASupplicant::kDebugLevelInfo))
      .Times(1);
  ReportWiFiDebugScopeChanged(false);
  Mock::VerifyAndClearExpectations(process_proxy);

  // If WiFi debugging is turned on and wpa_supplicant reports debugging
  // is turned off, WiFi should turn supplicant debugging on.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelInfo)),
                      Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(WPASupplicant::kDebugLevelDebug))
      .Times(1);
  ReportWiFiDebugScopeChanged(true);
  Mock::VerifyAndClearExpectations(process_proxy);

  // If WiFi debugging is already in the correct state, it should not be
  // changed.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelDebug)),
                      Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(string(WPASupplicant::kDebugLevelInfo)),
                      Return(true)));
  EXPECT_CALL(*process_proxy, SetDebugLevel(_)).Times(0);
  ReportWiFiDebugScopeChanged(true);
  ReportWiFiDebugScopeChanged(false);

  // After WiFi is stopped, we shouldn't be calling the proxy.
  EXPECT_CALL(*process_proxy, GetDebugLevel(_)).Times(0);
  EXPECT_CALL(*process_proxy, SetDebugLevel(_)).Times(0);
  StopWiFi();
  ReportWiFiDebugScopeChanged(true);
  ReportWiFiDebugScopeChanged(false);
}

TEST_F(WiFiMainTest, LogSSID) {
  EXPECT_EQ("[SSID=]", WiFi::LogSSID(""));
  EXPECT_EQ("[SSID=foo\\x5b\\x09\\x5dbar]", WiFi::LogSSID("foo[\t]bar"));
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(WiFiMainTest, CustomSetterNoopChange) {
  // SetBgscanShortInterval
  {
    Error error;
    static const uint16_t kKnownScanInterval = 4;
    // Set to known value.
    EXPECT_TRUE(SetBgscanShortInterval(kKnownScanInterval, &error));
    EXPECT_TRUE(error.IsSuccess());
    // Set to same value.
    EXPECT_FALSE(SetBgscanShortInterval(kKnownScanInterval, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  // SetBgscanSignalThreshold
  {
    Error error;
    static const int32_t kKnownSignalThreshold = 4;
    // Set to known value.
    EXPECT_TRUE(SetBgscanSignalThreshold(kKnownSignalThreshold, &error));
    EXPECT_TRUE(error.IsSuccess());
    // Set to same value.
    EXPECT_FALSE(SetBgscanSignalThreshold(kKnownSignalThreshold, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  // SetScanInterval
  {
    Error error;
    EXPECT_FALSE(SetScanInterval(GetScanInterval(), &error));
    EXPECT_TRUE(error.IsSuccess());
  }
}

// The following tests check the scan_state_ / scan_method_ state machine.

TEST_F(WiFiMainTest, FullScanFindsNothing) {
  StartScan(WiFi::kScanMethodFull);
  ReportScanDone();
  ExpectScanStop();
  ExpectFoundNothing();
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(10);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("FULL_NOCONNECTION ->")));
  EXPECT_CALL(*manager(), OnDeviceGeolocationInfoUpdated(_));
  event_dispatcher_
      ->DispatchPendingEvents();  // Launch UpdateScanStateAfterScanDone
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiMainTest, FullScanConnectingToConnected) {
  StartScan(WiFi::kScanMethodFull);
  WiFiEndpointRefPtr endpoint;
  RpcIdentifier bss_path;
  MockWiFiServiceRefPtr service =
      AttemptConnection(WiFi::kScanMethodFull, &endpoint, &bss_path);

  // Complete the connection.
  ExpectConnected();
  EXPECT_CALL(*service, NotifyCurrentEndpoint(EndpointMatch(endpoint)));
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(10);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> FULL_CONNECTED")));
  ReportCurrentBSSChanged(bss_path);
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
}

TEST_F(WiFiMainTest, ScanStateUma) {
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricScanResult, _, _))
      .Times(0);
  EXPECT_CALL(*metrics(), NotifyDeviceScanStarted(_));
  SetScanState(WiFi::kScanScanning, WiFi::kScanMethodFull, __func__);

  EXPECT_CALL(*metrics(), NotifyDeviceScanFinished(_));
  EXPECT_CALL(*metrics(), NotifyDeviceConnectStarted(_, _));
  SetScanState(WiFi::kScanConnecting, WiFi::kScanMethodFull, __func__);

  ExpectScanIdle();  // After connected.
  EXPECT_CALL(*metrics(), NotifyDeviceConnectFinished(_));
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricScanResult, _, _));
  SetScanState(WiFi::kScanConnected, WiFi::kScanMethodFull, __func__);
}

TEST_F(WiFiMainTest, ScanStateNotScanningNoUma) {
  EXPECT_CALL(*metrics(), NotifyDeviceScanStarted(_)).Times(0);
  EXPECT_CALL(*metrics(), NotifyDeviceConnectStarted(_, _));
  SetScanState(WiFi::kScanConnecting, WiFi::kScanMethodNone, __func__);

  ExpectScanIdle();  // After connected.
  EXPECT_CALL(*metrics(), NotifyDeviceConnectFinished(_));
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricScanResult, _, _))
      .Times(0);
  SetScanState(WiFi::kScanConnected, WiFi::kScanMethodNone, __func__);
}

TEST_F(WiFiMainTest, ConnectToServiceNotPending) {
  // Test for SetPendingService(nullptr), condition a)
  // |ConnectTo|->|DisconnectFrom|.
  StartScan(WiFi::kScanMethodFull);

  // Setup pending service.
  ExpectScanStop();
  ExpectConnecting();
  MockWiFiServiceRefPtr service_pending(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  EXPECT_EQ(service_pending, GetPendingService());

  // ConnectTo a different service than the pending one.
  ExpectConnecting();
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Disconnect());
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(10);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> TRANSITION_TO_CONNECTING")));
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> FULL_CONNECTING")));
  MockWiFiServiceRefPtr service_connecting(
      SetupConnectingService(RpcIdentifier(""), nullptr, nullptr));
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
  EXPECT_EQ(service_connecting, GetPendingService());
  EXPECT_EQ(nullptr, GetCurrentService());
  VerifyScanState(WiFi::kScanConnecting, WiFi::kScanMethodFull);

  ExpectScanIdle();  // To silence messages from the destructor.
}

TEST_F(WiFiMainTest, ConnectToWithError) {
  StartScan(WiFi::kScanMethodFull);

  ExpectScanIdle();
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), AddNetwork(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*metrics(), NotifyDeviceScanFinished(_)).Times(0);
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricScanResult, _, _))
      .Times(0);
  EXPECT_CALL(*adaptor_, EmitBoolChanged(kScanningProperty, false));
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  EXPECT_CALL(*service, GetSupplicantConfigurationParameters());
  InitiateConnect(service);
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
}

TEST_F(WiFiMainTest, ScanStateHandleDisconnect) {
  // Test for SetPendingService(nullptr), condition d) Disconnect while
  // scanning.

  // Start scanning.
  StartScan(WiFi::kScanMethodFull);

  // Set the pending service.
  ReportScanDone();
  ExpectScanStop();
  ExpectConnecting();
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityNone);
  SetPendingService(service);
  VerifyScanState(WiFi::kScanConnecting, WiFi::kScanMethodFull);

  // Disconnect from the pending service.
  ExpectScanIdle();
  EXPECT_CALL(*metrics(), NotifyDeviceScanFinished(_)).Times(0);
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricScanResult, _, _))
      .Times(0);
  ReportCurrentBSSChanged(RpcIdentifier(WPASupplicant::kCurrentBSSNull));
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
}

TEST_F(WiFiMainTest, ConnectWhileNotScanning) {
  // Setup WiFi but terminate scan.
  EXPECT_CALL(*adaptor_, EmitBoolChanged(kPoweredProperty, _))
      .Times(AnyNumber());

  ExpectScanStart(WiFi::kScanMethodFull, false);
  StartWiFi();
  event_dispatcher_->DispatchPendingEvents();

  ExpectScanStop();
  ExpectFoundNothing();
  ReportScanDone();
  event_dispatcher_->DispatchPendingEvents();
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  // Connecting.
  ExpectConnecting();
  EXPECT_CALL(*metrics(), NotifyDeviceScanStarted(_)).Times(0);
  WiFiEndpointRefPtr endpoint;
  RpcIdentifier bss_path;
  NiceScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(10);
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> TRANSITION_TO_CONNECTING")))
      .Times(0);
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> CONNECTING (not scan related)")));
  MockWiFiServiceRefPtr service =
      SetupConnectingService(RpcIdentifier(""), &endpoint, &bss_path);

  // Connected.
  ExpectConnected();
  EXPECT_CALL(log, Log(_, _, HasSubstr("-> CONNECTED (not scan related")));
  ReportCurrentBSSChanged(bss_path);
  ScopeLogger::GetInstance()->set_verbose_level(0);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
}

TEST_F(WiFiMainTest, BackgroundScan) {
  StartWiFi();
  SetupConnectedService(RpcIdentifier(""), nullptr, nullptr);
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);

  EXPECT_CALL(*GetSupplicantInterfaceProxy(), Scan(_)).Times(1);
  event_dispatcher_->DispatchPendingEvents();
  VerifyScanState(WiFi::kScanBackgroundScanning, WiFi::kScanMethodFull);

  ReportScanDone();
  EXPECT_CALL(*manager(), OnDeviceGeolocationInfoUpdated(_));
  event_dispatcher_
      ->DispatchPendingEvents();  // Launch UpdateScanStateAfterScanDone
  VerifyScanState(WiFi::kScanIdle, WiFi::kScanMethodNone);
}

TEST_F(WiFiMainTest, TDLSDiscoverResponse) {
  const char kPeer[] = "peer";
  MockTDLSManager* tdls_manager = new StrictMock<MockTDLSManager>();
  SetTDLSManager(tdls_manager);

  EXPECT_CALL(*tdls_manager, OnDiscoverResponseReceived(kPeer));
  TDLSDiscoverResponse(kPeer);
  Mock::VerifyAndClearExpectations(tdls_manager);
}

TEST_F(WiFiMainTest, PerformTDLSOperation) {
  const char kPeerMac[] = "00:11:22:33:44:55";
  MockTDLSManager* tdls_manager = new StrictMock<MockTDLSManager>();
  SetTDLSManager(tdls_manager);

  Error error;
  // No address resolution is performed since MAC address is provided.
  EXPECT_CALL(*tdls_manager,
              PerformOperation(kPeerMac, kTDLSStatusOperation, &error))
      .WillOnce(Return(kTDLSConnectedState));
  EXPECT_EQ(kTDLSConnectedState,
            PerformTDLSOperation(kTDLSStatusOperation, kPeerMac, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(WiFiMainTest, OnNewWiphy) {
  NewWiphyMessage new_wiphy_message;
  NetlinkPacket packet(kNewWiphyNlMsg, sizeof(kNewWiphyNlMsg));
  new_wiphy_message.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_CALL(*mac80211_monitor(), Start(_));
  EXPECT_CALL(*wake_on_wifi_, ParseWakeOnWiFiCapabilities(_));
  EXPECT_CALL(*wake_on_wifi_, OnWiphyIndexReceived(kNewWiphyNlMsg_WiphyIndex));
  GetAllScanFrequencies()->clear();
  OnNewWiphy(new_wiphy_message);
  EXPECT_EQ(base::size(kNewWiphyNlMsg_UniqueFrequencies),
            GetAllScanFrequencies()->size());
  for (uint16_t freq : kNewWiphyNlMsg_UniqueFrequencies) {
    EXPECT_TRUE(GetAllScanFrequencies()->find(freq) !=
                GetAllScanFrequencies()->end());
  }
}

TEST_F(WiFiMainTest, StateChangedUpdatesMac80211Monitor) {
  EXPECT_CALL(*mac80211_monitor(), UpdateConnectedState(true)).Times(2);
  ReportStateChanged(WPASupplicant::kInterfaceStateCompleted);
  ReportStateChanged(WPASupplicant::kInterfaceState4WayHandshake);

  EXPECT_CALL(*mac80211_monitor(), UpdateConnectedState(false));
  ReportStateChanged(WPASupplicant::kInterfaceStateAssociating);
}

TEST_F(WiFiMainTest, OnIPConfigUpdated_InvokesOnConnectedAndReachable) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);
  EXPECT_CALL(log, Log(_, _, HasSubstr("IPv4 DHCP lease obtained")));
  EXPECT_CALL(*wake_on_wifi_, OnConnectedAndReachable(_, _));
  EXPECT_CALL(*manager(), device_info()).WillOnce(Return(device_info()));
  ReportIPConfigComplete();

  // We should not call WakeOnWiFi::OnConnectedAndReachable if we are not
  // actually connected to a service.
  SetCurrentService(nullptr);
  EXPECT_CALL(*wake_on_wifi_, OnConnectedAndReachable(_, _)).Times(0);
  ReportIPv6ConfigComplete();

  // If we are actually connected to a service when our IPv6 configuration is
  // updated, we should call WakeOnWiFi::OnConnectedAndReachable.
  MockWiFiServiceRefPtr service = MakeMockService(kSecurity8021x);
  EXPECT_CALL(*service, IsConnected(nullptr)).WillOnce(Return(true));
  SetCurrentService(service);
  EXPECT_CALL(log, Log(_, _, HasSubstr("IPv6 configuration obtained")));
  EXPECT_CALL(*wake_on_wifi_, OnConnectedAndReachable(_, _));
  ReportIPv6ConfigComplete();

  // Do not call WakeOnWiFi::OnConnectedAndReachable if the IP config update was
  // triggered by a gateway ARP.
  EXPECT_CALL(log, Log(_, _, HasSubstr("Gateway ARP received")));
  EXPECT_CALL(*wake_on_wifi_, OnConnectedAndReachable(_, _)).Times(0);
  ReportIPConfigCompleteGatewayArpReceived();

  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
  ScopeLogger::GetInstance()->set_verbose_level(0);
}

TEST_F(WiFiMainTest, OnBeforeSuspend_CallsWakeOnWiFi) {
  SetWiFiEnabled(true);
  EXPECT_CALL(*wake_on_wifi_,
              OnBeforeSuspend(IsConnectedToCurrentService(), _, _, _, _, _, _));
  EXPECT_CALL(*this, SuspendCallback(_)).Times(0);
  OnBeforeSuspend();

  SetWiFiEnabled(false);
  EXPECT_CALL(*wake_on_wifi_,
              OnBeforeSuspend(IsConnectedToCurrentService(), _, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*this, SuspendCallback(ErrorTypeIs(Error::kSuccess)));
  OnBeforeSuspend();
}

TEST_F(WiFiMainTest, OnDarkResume_CallsWakeOnWiFi) {
  SetWiFiEnabled(true);
  EXPECT_CALL(*wake_on_wifi_,
              OnDarkResume(IsConnectedToCurrentService(), _, _, _, _, _));
  EXPECT_CALL(*this, SuspendCallback(_)).Times(0);
  OnDarkResume();

  SetWiFiEnabled(false);
  EXPECT_CALL(*wake_on_wifi_,
              OnDarkResume(IsConnectedToCurrentService(), _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*this, SuspendCallback(ErrorTypeIs(Error::kSuccess)));
  OnDarkResume();
}

TEST_F(WiFiMainTest, RemoveSupplicantNetworks) {
  StartWiFi();
  MockWiFiServiceRefPtr service1 = MakeMockService(kSecurity8021x);
  MockWiFiServiceRefPtr service2 = MakeMockService(kSecurity8021x);
  const RpcIdentifier kNetworkRpcId1("/service/network/rpcid1");
  const RpcIdentifier kNetworkRpcId2("/service/network/rpcid2");
  SetServiceNetworkRpcId(service1, kNetworkRpcId1);
  SetServiceNetworkRpcId(service2, kNetworkRpcId2);
  ASSERT_FALSE(RpcIdByServiceIsEmpty());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kNetworkRpcId1));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), RemoveNetwork(kNetworkRpcId2));
  RemoveSupplicantNetworks();
  ASSERT_TRUE(RpcIdByServiceIsEmpty());
}

TEST_F(WiFiMainTest, InitiateScan_Idle) {
  ScopedMockLog log;
  ASSERT_TRUE(wifi()->IsIdle());
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, ContainsRegex("Scan"))).Times(AtLeast(1));
  InitiateScan();
}

TEST_F(WiFiMainTest, InitiateScan_NotIdle) {
  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(1);
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityWpa);
  SetPendingService(service);
  EXPECT_FALSE(wifi()->IsIdle());
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(
      log,
      Log(_, _, HasSubstr("skipping scan, already connecting or connected.")));
  InitiateScan();
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
  ScopeLogger::GetInstance()->set_verbose_level(0);
}

TEST_F(WiFiMainTest, InitiateScanInDarkResume_Idle) {
  const WiFi::FreqSet freqs;
  StartWiFi();
  manager()->set_suppress_autoconnect(false);
  ASSERT_TRUE(wifi()->IsIdle());
  EXPECT_CALL(netlink_manager_,
              SendNl80211Message(IsNl80211Command(kNl80211FamilyId,
                                                  TriggerScanMessage::kCommand),
                                 _, _, _));
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), FlushBSS(0));
  InitiateScanInDarkResume(freqs);
  EXPECT_TRUE(manager()->suppress_autoconnect());
}

TEST_F(WiFiMainTest, InitiateScanInDarkResume_NotIdle) {
  const WiFi::FreqSet freqs;
  ScopedMockLog log;
  MockWiFiServiceRefPtr service = MakeMockService(kSecurityWpa);
  SetPendingService(service);
  manager()->set_suppress_autoconnect(false);
  EXPECT_FALSE(wifi()->IsIdle());
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(
      log,
      Log(_, _, HasSubstr("skipping scan, already connecting or connected.")));
  EXPECT_CALL(netlink_manager_,
              SendNl80211Message(IsNl80211Command(kNl80211FamilyId,
                                                  TriggerScanMessage::kCommand),
                                 _, _, _))
      .Times(0);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), FlushBSS(_)).Times(0);
  InitiateScanInDarkResume(freqs);
  EXPECT_FALSE(manager()->suppress_autoconnect());
}

TEST_F(WiFiMainTest, TriggerPassiveScan_NoResults) {
  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);
  const WiFi::FreqSet freqs;
  EXPECT_CALL(netlink_manager_,
              SendNl80211Message(IsNl80211Command(kNl80211FamilyId,
                                                  TriggerScanMessage::kCommand),
                                 _, _, _));
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("Scanning on specific channels")))
      .Times(0);
  TriggerPassiveScan(freqs);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
  ScopeLogger::GetInstance()->set_verbose_level(0);
}

TEST_F(WiFiMainTest, TriggerPassiveScan_HasResults) {
  ScopedMockLog log;
  ScopeLogger::GetInstance()->EnableScopesByName("wifi");
  ScopeLogger::GetInstance()->set_verbose_level(3);
  const WiFi::FreqSet freqs = {1};
  EXPECT_CALL(netlink_manager_,
              SendNl80211Message(IsNl80211Command(kNl80211FamilyId,
                                                  TriggerScanMessage::kCommand),
                                 _, _, _));
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(_, _, HasSubstr("Scanning on specific channels")))
      .Times(1);
  TriggerPassiveScan(freqs);
  ScopeLogger::GetInstance()->EnableScopesByName("-wifi");
  ScopeLogger::GetInstance()->set_verbose_level(0);
}

TEST_F(WiFiMainTest, PendingScanEvents) {
  // This test essentially performs ReportBSS(), but ensures that the
  // WiFi object successfully dispatches events in order.
  StartWiFi();
  BSSAdded(RpcIdentifier("bss0"),
           CreateBSSProperties("ssid0", "00:00:00:00:00:00", 0, 0,
                               kNetworkModeInfrastructure));
  BSSAdded(RpcIdentifier("bss1"),
           CreateBSSProperties("ssid1", "00:00:00:00:00:01", 0, 0,
                               kNetworkModeInfrastructure));
  BSSRemoved(RpcIdentifier("bss0"));
  BSSAdded(RpcIdentifier("bss2"),
           CreateBSSProperties("ssid2", "00:00:00:00:00:02", 0, 0,
                               kNetworkModeInfrastructure));

  WiFiEndpointRefPtr ap0 = MakeEndpoint("ssid0", "00:00:00:00:00:00");
  WiFiEndpointRefPtr ap1 = MakeEndpoint("ssid1", "00:00:00:00:00:01");
  WiFiEndpointRefPtr ap2 = MakeEndpoint("ssid2", "00:00:00:00:00:02");

  InSequence seq;
  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(EndpointMatch(ap0)));
  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(EndpointMatch(ap1)));
  WiFiServiceRefPtr null_service;
  EXPECT_CALL(*wifi_provider(), OnEndpointRemoved(EndpointMatch(ap0)))
      .WillOnce(Return(null_service));
  EXPECT_CALL(*wifi_provider(), OnEndpointAdded(EndpointMatch(ap2)));
  event_dispatcher_->DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(wifi_provider());

  const WiFi::EndpointMap& endpoints_by_rpcid = GetEndpointMap();
  EXPECT_EQ(2, endpoints_by_rpcid.size());
}

TEST_F(WiFiMainTest, ParseWiphyIndex_Success) {
  // Verify that the wiphy index in kNewWiphyNlMsg is parsed, and that the flag
  // for having the wiphy index is set by ParseWiphyIndex.
  EXPECT_EQ(GetWiphyIndex(), WiFi::kDefaultWiphyIndex);
  NewWiphyMessage msg;
  NetlinkPacket packet(kNewWiphyNlMsg, sizeof(kNewWiphyNlMsg));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_TRUE(ParseWiphyIndex(msg));
  EXPECT_EQ(GetWiphyIndex(), kNewWiphyNlMsg_WiphyIndex);
}

TEST_F(WiFiMainTest, ParseWiphyIndex_Failure) {
  ScopedMockLog log;
  // Change the NL80211_ATTR_WIPHY U32 attribute to the NL80211_ATTR_WIPHY_FREQ
  // U32 attribute, so that this message no longer contains a wiphy_index to be
  // parsed.
  NewWiphyMessage msg;
  MutableNetlinkPacket packet(kNewWiphyNlMsg, sizeof(kNewWiphyNlMsg));
  struct nlattr* nl80211_attr_wiphy = reinterpret_cast<struct nlattr*>(
      &packet.GetMutablePayload()
           ->GetData()[kNewWiphyNlMsg_Nl80211AttrWiphyOffset]);
  nl80211_attr_wiphy->nla_type = NL80211_ATTR_WIPHY_FREQ;
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _,
                       "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY"));
  EXPECT_FALSE(ParseWiphyIndex(msg));
  EXPECT_CALL(*wake_on_wifi_, OnWiphyIndexReceived(_)).Times(0);
}

TEST_F(WiFiMainTest, ParseFeatureFlags_RandomMacSupport) {
  NewWiphyMessage msg;
  NetlinkPacket packet(kNewWiphyNlMsg, sizeof(kNewWiphyNlMsg));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  // Make sure the feature is marked unsupported
  uint32_t flags;
  msg.const_attributes()->GetU32AttributeValue(NL80211_ATTR_FEATURE_FLAGS,
                                               &flags);
  flags &= ~(NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
             NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR);
  msg.attributes()->SetU32AttributeValue(NL80211_ATTR_FEATURE_FLAGS, flags);
  ParseFeatureFlags(msg);
  EXPECT_FALSE(GetRandomMacSupported());

  // Make sure the feature is marked supported
  msg.const_attributes()->GetU32AttributeValue(NL80211_ATTR_FEATURE_FLAGS,
                                               &flags);
  flags |= (NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
            NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR);
  msg.attributes()->SetU32AttributeValue(NL80211_ATTR_FEATURE_FLAGS, flags);
  ParseFeatureFlags(msg);
  EXPECT_TRUE(GetRandomMacSupported());
}

TEST_F(WiFiMainTest, RandomMacProperty_Unsupported) {
  StartWiFi();
  SetRandomMacSupported(false);
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              EnableMacAddressRandomization(_, _))
      .Times(0);
  SetRandomMacEnabled(true);
  EXPECT_FALSE(GetRandomMacEnabled());
}

TEST_F(WiFiMainTest, RandomMacProperty_Supported) {
  StartWiFi();
  SetRandomMacSupported(true);

  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              EnableMacAddressRandomization(GetRandomMacMask(), _))
      .Times(1);
  SetRandomMacEnabled(true);
  EXPECT_TRUE(GetRandomMacEnabled());

  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), DisableMacAddressRandomization())
      .Times(1);
  SetRandomMacEnabled(false);
  EXPECT_FALSE(GetRandomMacEnabled());
}

TEST_F(WiFiMainTest, RandomMacProperty_SupplicantFailed) {
  StartWiFi();
  SetRandomMacSupported(true);

  // Test wpa_supplicant failing to enable random MAC.
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(),
              EnableMacAddressRandomization(GetRandomMacMask(), _))
      .WillOnce(Return(false));
  SetRandomMacEnabled(true);
  EXPECT_FALSE(GetRandomMacEnabled());

  // Enable random MAC.
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  SetRandomMacEnabled(true);

  // Test wpa_supplicant failing to disable random MAC.
  Mock::VerifyAndClearExpectations(GetSupplicantInterfaceProxy());
  EXPECT_CALL(*GetSupplicantInterfaceProxy(), DisableMacAddressRandomization())
      .WillOnce(Return(false));
  SetRandomMacEnabled(false);
  EXPECT_TRUE(GetRandomMacEnabled());
}

TEST_F(WiFiMainTest, OnScanStarted_ActiveScan) {
  SetWiphyIndex(kScanTriggerMsgWiphyIndex);
  TriggerScanMessage msg;
  NetlinkPacket packet(kActiveScanTriggerNlMsg,
                       sizeof(kActiveScanTriggerNlMsg));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_CALL(*wake_on_wifi_, OnScanStarted(true));
  HandleNetlinkBroadcast(msg);
}

TEST_F(WiFiMainTest, OnScanStarted_PassiveScan) {
  SetWiphyIndex(kScanTriggerMsgWiphyIndex);
  TriggerScanMessage msg;
  NetlinkPacket packet(kPassiveScanTriggerNlMsg,
                       sizeof(kPassiveScanTriggerNlMsg));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_CALL(*wake_on_wifi_, OnScanStarted(false));
  HandleNetlinkBroadcast(msg);
}

TEST_F(WiFiMainTest, RemoveNetlinkHandler) {
  StartWiFi();
  StopWiFi();
  // WiFi is deleted when we go out of scope.
  EXPECT_CALL(netlink_manager_, RemoveBroadcastHandler(_)).Times(1);
}

TEST_F(WiFiMainTest, OnGetReg) {
  GetRegMessage msg;
  msg.attributes()->CreateStringAttribute(NL80211_ATTR_REG_ALPHA2, "alpha2");
  msg.attributes()->CreateU8Attribute(NL80211_ATTR_DFS_REGION, "dfs-region");

  // First Regulatory Domain enum enrty.
  msg.attributes()->SetStringAttributeValue(NL80211_ATTR_REG_ALPHA2, "00");
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricRegulatoryDomain,
                            Metrics::RegulatoryDomain::kRegDom00, _))
      .Times(1);
  // Should call ChangeRegDomain with region UNSET when no dfs_region present.
  EXPECT_CALL(*power_manager(), ChangeRegDomain(NL80211_DFS_UNSET)).Times(1);
  OnGetReg(msg);
  // Last Regulatory Domain enum entry. Zimbabwe = 674.
  msg.attributes()->SetStringAttributeValue(NL80211_ATTR_REG_ALPHA2, "ZW");
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricRegulatoryDomain, 674, _))
      .Times(1);
  msg.attributes()->SetU8AttributeValue(NL80211_ATTR_DFS_REGION,
                                        NL80211_DFS_FCC);
  // Subsequent calls should all call ChangeRegDomain() with the current region.
  EXPECT_CALL(*power_manager(), ChangeRegDomain(NL80211_DFS_FCC)).Times(4);
  OnGetReg(msg);
  // Second call with same country code should not trigger SendEnumToUMA() call.
  OnGetReg(msg);
  // Lower case valid country code. United States = 540.
  msg.attributes()->SetStringAttributeValue(NL80211_ATTR_REG_ALPHA2, "us");
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricRegulatoryDomain, 540, _))
      .Times(1);
  OnGetReg(msg);
  // Invalid country code.
  msg.attributes()->SetStringAttributeValue(NL80211_ATTR_REG_ALPHA2, "err");
  EXPECT_CALL(*metrics(),
              SendEnumToUMA(Metrics::kMetricRegulatoryDomain,
                            Metrics::RegulatoryDomain::kCountryCodeInvalid, _))
      .Times(1);
  OnGetReg(msg);
  // Message with no alpha2 attribute.
  WiphyRegChangeMessage no_alpha2;
  EXPECT_CALL(*metrics(), SendEnumToUMA(Metrics::kMetricRegulatoryDomain, _, _))
      .Times(0);
  OnGetReg(no_alpha2);
  msg.attributes()->SetU8AttributeValue(NL80211_ATTR_DFS_REGION,
                                        NL80211_DFS_ETSI);
  EXPECT_CALL(*power_manager(), ChangeRegDomain(NL80211_DFS_ETSI)).Times(2);
  OnGetReg(msg);
  OnGetReg(msg);
  EXPECT_CALL(*power_manager(), ChangeRegDomain(NL80211_DFS_JP)).Times(1);
  msg.attributes()->SetU8AttributeValue(NL80211_ATTR_DFS_REGION,
                                        NL80211_DFS_JP);
  OnGetReg(msg);
}

}  // namespace shill
