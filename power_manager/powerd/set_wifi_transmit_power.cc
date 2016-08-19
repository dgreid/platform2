// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Helper program for setting WiFi transmission power.

#include <string>
#include <vector>

#include <linux/nl80211.h>
#include <net/if.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/flag_helper.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>


// Vendor command definition for marvell mwifiex driver
// Defined in Linux kernel:
// drivers/net/wireless/marvell/mwifiex/main.h
#define MWIFIEX_VENDOR_ID  0x005043

// Vendor sub command
#define MWIFIEX_VENDOR_CMD_SET_TX_POWER_LIMIT 0

#define MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24  1
#define MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52  2


// Vendor command definition for intel iwl7000 driver
// Defined in Linux kernel:
// drivers/net/wireless/iwl7000/iwlwifi/mvm/vendor-cmd.h
#define INTEL_OUI  0x001735

// Vendor sub command
#define IWL_MVM_VENDOR_CMD_SET_NIC_TXPOWER_LIMIT  13

#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_24   13
#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52L  14
#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52H  15


namespace {

// Kernel module path for Marvell wireless driver.
const char kMwifiexModulePath[] = "/sys/module/mwifiex";


int ErrorHandler(struct sockaddr_nl* nla, struct nlmsgerr* err, void* arg) {
  int* ret = static_cast<int*>(arg);
  *ret = err->error;
  return NL_STOP;
}

int FinishHandler(struct nl_msg* msg, void* arg) {
  int* ret = static_cast<int*>(arg);
  *ret = 0;
  return NL_SKIP;
}

int AckHandler(struct nl_msg* msg, void* arg) {
  int* ret = static_cast<int*>(arg);
  *ret = 0;
  return NL_STOP;
}

int ValidHandler(struct nl_msg* msg, void* arg) {
  return NL_OK;
}

enum WirelessDriver {
  DRIVER_NONE = 0,
  DRIVER_MWIFIEX,
  DRIVER_IWL
};

class SetWiFiTransmitPower {
 public:
  SetWiFiTransmitPower();
  ~SetWiFiTransmitPower();

  // Set power mode according to tablet mode state.
  // |iwl_power_table| is only required when the system has a Intel WiFi device
  // (iwlwifi driver). Returns 0 on success and a negative number on error.
  void SetPowerMode(bool tablet, const std::string& iwl_power_table);

 private:
  static WirelessDriver GetWirelessDriverType();

  // Return the wireless device index for Chrome OS (either wlan0 or mlan0).
  static int GetWirelessDeviceIndex();

  // Fill in nl80211 message for the mwifiex driver.
  void FillMessageMwifiex(struct nl_msg* msg, bool tablet);

  // Fill in nl80211 message for the iwl driver.
  void FillMessageIwl(struct nl_msg* msg,
                      bool tablet,
                      const std::string iwl_power_table);

  struct nl_sock* nl_sock_;
  struct nl_cb* cb_;
  int err_;  // For communicating with libnl callbacks

  DISALLOW_COPY_AND_ASSIGN(SetWiFiTransmitPower);
};

SetWiFiTransmitPower::SetWiFiTransmitPower()
    : nl_sock_(nl_socket_alloc()),
      cb_(nl_cb_alloc(NL_CB_DEFAULT)),
      err_(0) {
  CHECK(nl_sock_);
  CHECK(cb_);

  // Register libnl callbacks.
  nl_cb_err(cb_, NL_CB_CUSTOM, ErrorHandler, &err_);
  nl_cb_set(cb_, NL_CB_FINISH, NL_CB_CUSTOM, FinishHandler, &err_);
  nl_cb_set(cb_, NL_CB_ACK, NL_CB_CUSTOM, AckHandler, &err_);
  nl_cb_set(cb_, NL_CB_VALID, NL_CB_CUSTOM, ValidHandler, NULL);
}

SetWiFiTransmitPower::~SetWiFiTransmitPower() {
  nl_socket_free(nl_sock_);
  nl_cb_put(cb_);
}

WirelessDriver SetWiFiTransmitPower::GetWirelessDriverType() {
  if (base::PathExists(base::FilePath(kMwifiexModulePath))) {
    return DRIVER_MWIFIEX;
  }
  return DRIVER_IWL;
}

int SetWiFiTransmitPower::GetWirelessDeviceIndex() {
  // Chrome OS only has one network device
  // The device is usually 'mlan0' for arm devices, and 'wlan0' for intel
  // devices.
  int index = 0;

  index = if_nametoindex("mlan0");
  if (index)
    return index;

  index = if_nametoindex("wlan0");
  if (index)
    return index;

  LOG(FATAL) << "No wireless driver found";

  return index;  // Not reached.
}

void SetWiFiTransmitPower::FillMessageMwifiex(struct nl_msg* msg, bool tablet) {
  int err = 0;

  err = nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, MWIFIEX_VENDOR_ID);
  CHECK(!err) << "Failed to put NL80211_ATTR_VENDOR_ID";

  err = nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                    MWIFIEX_VENDOR_CMD_SET_TX_POWER_LIMIT);
  CHECK(!err) << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* limits = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
  CHECK(limits) << "Failed in nla_nest_start";

  err = nla_put_u8(msg, MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24, tablet);
  CHECK(!err) << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24";

  err = nla_put_u8(msg, MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52, tablet);
  CHECK(!err) << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52";

  err = nla_nest_end(msg, limits);
  CHECK(!err) << "Failed in nla_nest_end";
}

void SetWiFiTransmitPower::FillMessageIwl(struct nl_msg* msg,
                                          bool tablet,
                                          const std::string iwl_power_table) {
  int err = 0;

  CHECK(!iwl_power_table.empty()) << "No power table information available";

  // The iwl_power_table string has 6 unsigned integers separated by colon.
  // The first three integers represent the power value of 24, 52L, 52H bands
  // in clamshell mode, while the last three integers represent that of the
  // power value for tablet mode.
  std::vector<std::string> str_values =
      base::SplitString(iwl_power_table, ":", base::TRIM_WHITESPACE,
                        base::SPLIT_WANT_ALL);

  CHECK_EQ(str_values.size(), static_cast<size_t>(6))
      << "Wrong number of power table literal";

  // Parse string value to unsigned integers.
  std::vector<unsigned> values;
  for (const auto& str_value : str_values) {
    unsigned output;
    CHECK(base::StringToUint(str_value, &output))
        << "Invalid power table literal \"" << str_value << "\"";
    values.push_back(output);
  }

  err = nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, INTEL_OUI);
  CHECK(!err) << "Failed to put NL80211_ATTR_VENDOR_ID";

  err = nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                    IWL_MVM_VENDOR_CMD_SET_NIC_TXPOWER_LIMIT);
  CHECK(!err) << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* limits = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
  CHECK(limits) << "Failed in nla_nest_start";

  int group = tablet ? 1 : 0;

  err = nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_24,
                    values[group * 3 + 0] * 8);
  CHECK(!err) << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24";

  err = nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52L,
                    values[group * 3 + 1] * 8);
  CHECK(!err) << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52L";

  err = nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52H,
                    values[group * 3 + 2] * 8);
  CHECK(!err) << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52H";

  err = nla_nest_end(msg, limits);
  CHECK(!err) << "Failed in nla_nest_end";
}

void SetWiFiTransmitPower::SetPowerMode(bool tablet,
                                       const std::string& iwl_power_table) {
  int err = 0;

  err = genl_connect(nl_sock_);
  CHECK(!err) << "Failed to connect to netlink";

  int nl_family_id = genl_ctrl_resolve(nl_sock_, "nl80211");
  CHECK_GE(nl_family_id, 0) << "family nl80211 not found";

  struct nl_msg* msg = nlmsg_alloc();
  CHECK(msg);

  // Set header.
  genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nl_family_id, 0, 0,
              NL80211_CMD_VENDOR, 0);

  // Set actual message.
  err = nla_put_u32(msg, NL80211_ATTR_IFINDEX, GetWirelessDeviceIndex());
  CHECK(!err) << "Failed to put NL80211_ATTR_IFINDEX";

  switch (GetWirelessDriverType()) {
    case DRIVER_MWIFIEX:
      FillMessageMwifiex(msg, tablet);
      break;
    case DRIVER_IWL:
      FillMessageIwl(msg, tablet, iwl_power_table);
      break;
    default:
      LOG(FATAL) << "no valid wireless driver found";
  }

  err = nl_send_auto(nl_sock_, msg);
  CHECK_GE(err, 0) << "nl_send_auto failed: " << nl_geterror(err_);

  while (err_ != 0)
    nl_recvmsgs(nl_sock_, cb_);

  nlmsg_free(msg);
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(tablet, false, "Set wifi transmit power mode to tablet mode");
  DEFINE_string(iwl_power_table, "", "Power table for iwlwifi driver. "
                "The argument should be a string containing 6 integers "
                "separated by colons");
  brillo::FlagHelper::Init(argc, argv, "Set wifi transmit power mode");

  SetWiFiTransmitPower().SetPowerMode(FLAGS_tablet, FLAGS_iwl_power_table);
  return 0;
}
