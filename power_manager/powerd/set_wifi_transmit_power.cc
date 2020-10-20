// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Helper program for setting WiFi transmission power.

#include <map>
#include <string>
#include <vector>

#include <linux/nl80211.h>
#include <net/if.h>

#include <base/at_exit.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <brillo/flag_helper.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <power_manager/common/power_constants.h>

// Vendor command definition for marvell mwifiex driver
// Defined in Linux kernel:
// drivers/net/wireless/marvell/mwifiex/main.h
#define MWIFIEX_VENDOR_ID 0x005043

// Vendor sub command
#define MWIFIEX_VENDOR_CMD_SET_TX_POWER_LIMIT 0

#define MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24 1
#define MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52 2

// Vendor command definition for intel iwl7000 driver
// Defined in Linux kernel:
// drivers/net/wireless/iwl7000/iwlwifi/mvm/vendor-cmd.h
#define INTEL_OUI 0x001735

// Vendor sub command
#define IWL_MVM_VENDOR_CMD_SET_SAR_PROFILE 28

#define IWL_MVM_VENDOR_ATTR_SAR_CHAIN_A_PROFILE 58
#define IWL_MVM_VENDOR_ATTR_SAR_CHAIN_B_PROFILE 59

#define IWL_TABLET_PROFILE_INDEX 1
#define IWL_CLAMSHELL_PROFILE_INDEX 2

// Legacy vendor subcommand used for devices without limits in VPD.
#define IWL_MVM_VENDOR_CMD_SET_NIC_TXPOWER_LIMIT 13

#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_24 13
#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52L 14
#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52H 15

#define REALTEK_OUI 0x00E04C
#define REALTEK_NL80211_VNDCMD_SET_SAR 0x88
#define REALTEK_VNDCMD_ATTR_SAR_RULES 1
#define REALTEK_VNDCMD_ATTR_SAR_BAND 2
#define REALTEK_VNDCMD_ATTR_SAR_POWER 3

namespace {

int ErrorHandler(struct sockaddr_nl* nla, struct nlmsgerr* err, void* arg) {
  *static_cast<int*>(arg) = err->error;
  return NL_STOP;
}

int FinishHandler(struct nl_msg* msg, void* arg) {
  *static_cast<int*>(arg) = 0;
  return NL_SKIP;
}

int AckHandler(struct nl_msg* msg, void* arg) {
  *static_cast<int*>(arg) = 0;
  return NL_STOP;
}

int ValidHandler(struct nl_msg* msg, void* arg) {
  return NL_OK;
}

enum class WirelessDriver { NONE, MWIFIEX, IWL, ATH10K, RTW };

enum RealtekVndcmdSARBand {
  REALTEK_VNDCMD_ATTR_SAR_BAND_2g = 0,
  REALTEK_VNDCMD_ATTR_SAR_BAND_5g_1 = 1,
  REALTEK_VNDCMD_ATTR_SAR_BAND_5g_3 = 3,
  REALTEK_VNDCMD_ATTR_SAR_BAND_5g_4 = 4,
};

// Returns the type of wireless driver that's present on the system.
WirelessDriver GetWirelessDriverType(const std::string& device_name) {
  const std::map<std::string, WirelessDriver> drivers = {
      {"ath10k_pci", WirelessDriver::ATH10K},
      {"ath10k_sdio", WirelessDriver::ATH10K},
      {"ath10k_snoc", WirelessDriver::ATH10K},
      {"iwlwifi", WirelessDriver::IWL},
      {"mwifiex_pcie", WirelessDriver::MWIFIEX},
      {"mwifiex_sdio", WirelessDriver::MWIFIEX},
      {"rtw_pci", WirelessDriver::RTW},
      {"rtw_8822ce", WirelessDriver::RTW},
  };

  // .../device/driver symlink should point at the driver's module.
  base::FilePath link_path(base::StringPrintf("/sys/class/net/%s/device/driver",
                                              device_name.c_str()));
  base::FilePath driver_path;
  CHECK(base::ReadSymbolicLink(link_path, &driver_path));
  base::FilePath driver_name = driver_path.BaseName();
  const auto driver = drivers.find(driver_name.value());
  if (driver != drivers.end())
    return driver->second;

  return WirelessDriver::NONE;
}

// Returns a vector of wireless device name(s) found on the system. We
// generally should only have 1 internal WiFi device, but it's possible to have
// an external device plugged in (e.g., via USB).
std::vector<std::string> GetWirelessDeviceNames() {
  std::vector<std::string> names;
  base::FileEnumerator iter(base::FilePath("/sys/class/net"), false,
                            base::FileEnumerator::FileType::FILES |
                                base::FileEnumerator::FileType::SHOW_SYM_LINKS,
                            "*");

  for (base::FilePath name = iter.Next(); !name.empty(); name = iter.Next()) {
    std::string uevent;
    CHECK(base::ReadFileToString(name.Append("uevent"), &uevent));

    for (const auto& line : base::SplitString(
             uevent, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
      if (line == "DEVTYPE=wlan") {
        names.push_back(name.BaseName().value());
        break;
      }
    }
  }
  return names;
}

// Returns a vector of tx power limits for mode |tablet|.
// If the board does not store power limits for rtw driver in chromeos-config,
// the function will fail.
std::map<enum RealtekVndcmdSARBand, uint8_t> GetRtwChromeosConfigPowerTable(
    bool tablet, power_manager::WifiRegDomain domain) {
  std::map<enum RealtekVndcmdSARBand, uint8_t> power_table = {};
  auto config = std::make_unique<brillo::CrosConfig>();
  CHECK(config->Init()) << "Could not find config";
  std::string wifi_power_table_path =
      tablet ? "/wifi/tablet-mode-power-table-rtw"
             : "/wifi/non-tablet-mode-power-table-rtw";
  std::string wifi_geo_offsets_path;
  switch (domain) {
    case power_manager::WifiRegDomain::FCC:
      wifi_geo_offsets_path = "/wifi/geo-offsets-fcc";
      break;
    case power_manager::WifiRegDomain::EU:
      wifi_geo_offsets_path = "/wifi/geo-offsets-eu";
      break;
    case power_manager::WifiRegDomain::REST_OF_WORLD:
      wifi_geo_offsets_path = "/wifi/geo-offsets-rest-of-world";
      break;
    case power_manager::WifiRegDomain::NONE:
      break;
  }

  int offset_2g = 0, offset_5g = 0;
  if (domain != power_manager::WifiRegDomain::NONE) {
    std::string offset_string;
    if (config->GetString(wifi_geo_offsets_path, "offset-2g", &offset_string)) {
      offset_2g = std::stoi(offset_string);
    }
    if (config->GetString(wifi_geo_offsets_path, "offset-5g", &offset_string)) {
      offset_5g = std::stoi(offset_string);
    }
  }

  std::string value;
  int power_limit;

  CHECK(config->GetString(wifi_power_table_path, "limit-2g", &value))
      << "Could not get ChromeosConfig power table.";
  power_limit = std::stoi(value) + offset_2g;
  CHECK(power_limit <= UINT8_MAX) << "Invalid power limit configs. Limit "
                                     "value cannot exceed 255.";
  power_table[REALTEK_VNDCMD_ATTR_SAR_BAND_2g] = power_limit;

  CHECK(config->GetString(wifi_power_table_path, "limit-5g-1", &value))
      << "Could not get ChromeosConfig power table.";
  power_limit = std::stoi(value) + offset_5g;
  CHECK(power_limit <= UINT8_MAX) << "Invalid power limit configs. Limit "
                                     "value cannot exceed 255.";
  power_table[REALTEK_VNDCMD_ATTR_SAR_BAND_5g_1] = power_limit;

  // Rtw driver does not support 5g band 2, so skip it.

  CHECK(config->GetString(wifi_power_table_path, "limit-5g-3", &value))
      << "Could not get ChromeosConfig power table.";
  power_limit = std::stoi(value) + offset_5g;
  CHECK(power_limit <= UINT8_MAX) << "Invalid power limit configs. Limit "
                                     "value cannot exceed 255.";
  power_table[REALTEK_VNDCMD_ATTR_SAR_BAND_5g_3] = power_limit;

  CHECK(config->GetString(wifi_power_table_path, "limit-5g-4", &value))
      << "Could not get ChromeosConfig power table.";
  power_limit = std::stoi(value) + offset_5g;
  CHECK(power_limit <= UINT8_MAX) << "Invalid power limit configs. Limit "
                                     "value cannot exceed 255.";
  power_table[REALTEK_VNDCMD_ATTR_SAR_BAND_5g_4] = power_limit;

  return power_table;
}

// Fill in nl80211 message for the mwifiex driver.
void FillMessageMwifiex(struct nl_msg* msg, bool tablet) {
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, MWIFIEX_VENDOR_ID))
      << "Failed to put NL80211_ATTR_VENDOR_ID";
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                     MWIFIEX_VENDOR_CMD_SET_TX_POWER_LIMIT))
      << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* limits = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
  CHECK(limits) << "Failed in nla_nest_start";

  CHECK(!nla_put_u8(msg, MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24, tablet))
      << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24";
  CHECK(!nla_put_u8(msg, MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52, tablet))
      << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52";
  CHECK(!nla_nest_end(msg, limits)) << "Failed in nla_nest_end";
}

// Returns a vector of three IWL transmit power limits for mode |tablet| if the
// board doesn't contain limits in VPD, or an empty vector if VPD should be
// used. VPD limits are expected; this is just a hack for devices (currently
// only cave) that lack limits in VPD. See b:70549692 for details.
std::vector<uint32_t> GetNonVpdIwlPowerTable(bool tablet) {
  // Get the board name minus an e.g. "-signed-mpkeys" suffix.
  std::string board = base::SysInfo::GetLsbReleaseBoard();
  const size_t index = board.find("-signed-");
  if (index != std::string::npos)
    board.resize(index);

  if (board == "cave") {
    return tablet ? std::vector<uint32_t>{13, 9, 9}
                  : std::vector<uint32_t>{30, 30, 30};
  }
  return {};
}

// Fill in nl80211 message for the iwl driver.
void FillMessageIwl(struct nl_msg* msg, bool tablet) {
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, INTEL_OUI))
      << "Failed to put NL80211_ATTR_VENDOR_ID";

  const std::vector<uint32_t> table = GetNonVpdIwlPowerTable(tablet);
  const bool use_vpd = table.empty();

  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                     use_vpd ? IWL_MVM_VENDOR_CMD_SET_SAR_PROFILE
                             : IWL_MVM_VENDOR_CMD_SET_NIC_TXPOWER_LIMIT))
      << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* limits =
      nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA | NLA_F_NESTED);
  CHECK(limits) << "Failed in nla_nest_start";

  if (use_vpd) {
    int index = tablet ? IWL_TABLET_PROFILE_INDEX : IWL_CLAMSHELL_PROFILE_INDEX;
    CHECK(!nla_put_u8(msg, IWL_MVM_VENDOR_ATTR_SAR_CHAIN_A_PROFILE, index))
        << "Failed to put IWL_MVM_VENDOR_ATTR_SAR_CHAIN_A_PROFILE";
    CHECK(!nla_put_u8(msg, IWL_MVM_VENDOR_ATTR_SAR_CHAIN_B_PROFILE, index))
        << "Failed to put IWL_MVM_VENDOR_ATTR_SAR_CHAIN_B_PROFILE";
  } else {
    DCHECK_EQ(table.size(), 3);
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_24, table[0] * 8))
        << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24";
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52L, table[1] * 8))
        << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52L";
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52H, table[2] * 8))
        << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52H";
  }

  CHECK(!nla_nest_end(msg, limits)) << "Failed in nla_nest_end";
}

// Fill in nl80211 message for the rtw driver.
void FillMessageRtw(struct nl_msg* msg,
                    bool tablet,
                    power_manager::WifiRegDomain domain) {
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, REALTEK_OUI))
      << "Failed to put NL80211_ATTR_VENDOR_ID";
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                     REALTEK_NL80211_VNDCMD_SET_SAR))
      << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* vendor_cmd = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
  struct nlattr* rules = nla_nest_start(msg, REALTEK_VNDCMD_ATTR_SAR_RULES);
  for (const auto& limit : GetRtwChromeosConfigPowerTable(tablet, domain)) {
    struct nlattr* rule = nla_nest_start(msg, 1);
    CHECK(rule) << "Failed in nla_nest_start";
    CHECK(!nla_put_u32(msg, REALTEK_VNDCMD_ATTR_SAR_BAND, limit.first))
        << "Failed to put REALTEK_VNDCMD_ATTR_SAR_BAND";
    CHECK(!nla_put_u8(msg, REALTEK_VNDCMD_ATTR_SAR_POWER, limit.second))
        << "Failed to put REALTEK_VNDCMD_ATTR_SAR_POWER";
    CHECK(!nla_nest_end(msg, rule)) << "Failed in nla_nest_end";
  }
  CHECK(!nla_nest_end(msg, rules)) << "Failed in nla_nest_end";
  CHECK(!nla_nest_end(msg, vendor_cmd)) << "Failed in nla_nest_end";
}

class PowerSetter {
 public:
  PowerSetter() : nl_sock_(nl_socket_alloc()), cb_(nl_cb_alloc(NL_CB_DEFAULT)) {
    CHECK(nl_sock_);
    CHECK(cb_);

    // Register libnl callbacks.
    nl_cb_err(cb_, NL_CB_CUSTOM, ErrorHandler, &err_);
    nl_cb_set(cb_, NL_CB_FINISH, NL_CB_CUSTOM, FinishHandler, &err_);
    nl_cb_set(cb_, NL_CB_ACK, NL_CB_CUSTOM, AckHandler, &err_);
    nl_cb_set(cb_, NL_CB_VALID, NL_CB_CUSTOM, ValidHandler, nullptr);
  }
  ~PowerSetter() {
    nl_socket_free(nl_sock_);
    nl_cb_put(cb_);
  }

  bool SendModeSwitch(const std::string& dev_name,
                      bool tablet,
                      power_manager::WifiRegDomain domain) {
    const uint32_t index = if_nametoindex(dev_name.c_str());
    if (!index) {
      LOG(ERROR) << "Failed to find wireless device index for " << dev_name;
      return false;
    }
    WirelessDriver driver = GetWirelessDriverType(dev_name);
    if (driver == WirelessDriver::NONE || driver == WirelessDriver::ATH10K) {
      LOG(ERROR) << "No valid wireless driver found for " << dev_name;
      return false;
    }
    LOG(INFO) << "Found wireless device " << dev_name << " (index " << index
              << ")";

    struct nl_msg* msg = nlmsg_alloc();
    CHECK(msg);

    // Set header.
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nl_family_id_, 0, 0,
                NL80211_CMD_VENDOR, 0);

    // Set actual message.
    CHECK(!nla_put_u32(msg, NL80211_ATTR_IFINDEX, index))
        << "Failed to put NL80211_ATTR_IFINDEX";

    switch (driver) {
      case WirelessDriver::MWIFIEX:
        FillMessageMwifiex(msg, tablet);
        break;
      case WirelessDriver::IWL:
        FillMessageIwl(msg, tablet);
        break;
      case WirelessDriver::RTW:
        FillMessageRtw(msg, tablet, domain);
        break;
      case WirelessDriver::ATH10K:
        // TODO(https://crbug.com/782924): implement for ath10k.
      case WirelessDriver::NONE:
        NOTREACHED() << "No driver found";
    }

    CHECK_GE(nl_send_auto(nl_sock_, msg), 0)
        << "nl_send_auto failed: " << nl_geterror(err_);
    while (err_ != 0)
      nl_recvmsgs(nl_sock_, cb_);

    nlmsg_free(msg);
    return true;
  }

  // Sets power mode according to tablet mode state. Returns true on success and
  // false on failure.
  bool SetPowerMode(bool tablet, power_manager::WifiRegDomain domain) {
    CHECK(!genl_connect(nl_sock_)) << "Failed to connect to netlink";

    nl_family_id_ = genl_ctrl_resolve(nl_sock_, "nl80211");
    CHECK_GE(nl_family_id_, 0) << "family nl80211 not found";

    const std::vector<std::string> device_names = GetWirelessDeviceNames();
    if (device_names.empty()) {
      LOG(ERROR) << "No wireless device found";
      return false;
    }

    bool ret = true;
    for (const auto& name : device_names)
      if (!SendModeSwitch(name, tablet, domain))
        ret = false;
    return ret;
  }

 private:
  struct nl_sock* nl_sock_;
  int nl_family_id_ = 0;
  struct nl_cb* cb_;
  int err_ = 0;  // Used by |cb_| to store errors.

  DISALLOW_COPY_AND_ASSIGN(PowerSetter);
};

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(tablet, false, "Set wifi transmit power mode to tablet mode");
  DEFINE_string(domain, "none",
                "Regulatory domain for wifi transmit power"
                "Options: fcc, eu, rest-of-world, none");
  brillo::FlagHelper::Init(argc, argv, "Set wifi transmit power mode");

  base::AtExitManager at_exit_manager;
  power_manager::WifiRegDomain domain = power_manager::WifiRegDomain::NONE;
  if (FLAGS_domain == "fcc") {
    domain = power_manager::WifiRegDomain::FCC;
  } else if (FLAGS_domain == "eu") {
    domain = power_manager::WifiRegDomain::EU;
  } else if (FLAGS_domain == "rest-of-world") {
    domain = power_manager::WifiRegDomain::REST_OF_WORLD;
  } else if (FLAGS_domain != "none") {
    LOG(ERROR) << "Domain argument \"" << FLAGS_domain
               << "\" is not an "
                  "accepted value. Options: fcc, eu, rest-of-world, none";
    return 1;
  }
  return PowerSetter().SetPowerMode(FLAGS_tablet, domain) ? 0 : 1;
}
