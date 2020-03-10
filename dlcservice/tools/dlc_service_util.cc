// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <chromeos/constants/imageloader.h>
#include <dbus/bus.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <libimageloader/manifest.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "dlcservice/dbus-proxies.h"
#include "dlcservice/utils.h"

using dlcservice::DlcModuleInfo;
using dlcservice::DlcModuleList;
using org::chromium::DlcServiceInterfaceProxy;
using std::string;
using std::vector;

namespace {

constexpr uid_t kRootUid = 0;
constexpr uid_t kChronosUid = 1000;
constexpr char kChronosUser[] = "chronos";
constexpr char kChronosGroup[] = "chronos";

void EnterMinijail() {
  ScopedMinijail jail(minijail_new());
  CHECK_EQ(0, minijail_change_user(jail.get(), kChronosUser));
  CHECK_EQ(0, minijail_change_group(jail.get(), kChronosGroup));
  minijail_inherit_usergroups(jail.get());
  minijail_no_new_privs(jail.get());
  minijail_enter(jail.get());
}

string ErrorPtrStr(const brillo::ErrorPtr& err) {
  std::ostringstream err_stream;
  err_stream << "Domain=" << err->GetDomain() << " "
             << "Error Code=" << err->GetCode() << " "
             << "Error Message=" << err->GetMessage();
  // TODO(crbug.com/999284): No inner error support, err->GetInnerError().
  return err_stream.str();
}

}  // namespace

class DlcServiceUtil : public brillo::Daemon {
 public:
  DlcServiceUtil(int argc, const char** argv)
      : argc_(argc), argv_(argv), weak_ptr_factory_(this) {}
  ~DlcServiceUtil() {}

 private:
  bool InitDlcModuleList(const string& omaha_url, const string& dlc_ids) {
    const vector<string>& dlc_ids_list = SplitString(
        dlc_ids, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (dlc_ids_list.empty()) {
      LOG(ERROR) << "Please specify a list of DLC modules.";
      return false;
    }
    dlc_module_list_str_ = dlc_ids;
    dlc_module_list_.set_omaha_url(omaha_url);
    for (const string& dlc_id : dlc_ids_list) {
      DlcModuleInfo* dlc_module_info = dlc_module_list_.add_dlc_module_infos();
      dlc_module_info->set_dlc_id(dlc_id);
    }
    return true;
  }

  int OnEventLoopStarted() override {
    DEFINE_bool(install, false, "Install a given list of DLC modules.");
    DEFINE_bool(uninstall, false, "Uninstall a given list of DLC modules.");
    DEFINE_bool(list, false, "List all installed DLC modules.");
    DEFINE_bool(oneline, false, "Print short module DLC module information.");
    DEFINE_string(dlc_ids, "", "Colon separated list of DLC module ids.");
    DEFINE_string(omaha_url, "",
                  "Overrides the default Omaha URL in the update_engine.");
    brillo::FlagHelper::Init(argc_, argv_, "dlcservice_util");

    // Enforce mutually exclusive flags.
    vector<bool> exclusive_flags = {FLAGS_install, FLAGS_uninstall, FLAGS_list};
    if (std::count(exclusive_flags.begin(), exclusive_flags.end(), true) != 1) {
      LOG(ERROR) << "Only one of --install, --uninstall, --list must be set.";
      return EX_SOFTWARE;
    }

    int error = EX_OK;
    if (!Init(&error)) {
      LOG(ERROR) << "Failed to initialize client.";
      return error;
    }

    // Called with "--list".
    if (FLAGS_list) {
      if (!GetInstalled(&dlc_module_list_))
        return EX_SOFTWARE;
      PrintDlcModuleList(FLAGS_oneline);
      Quit();
      return EX_OK;
    }

    if (!InitDlcModuleList(FLAGS_omaha_url, FLAGS_dlc_ids))
      return EX_SOFTWARE;

    // Called with "--install".
    if (FLAGS_install) {
      // Set up callbacks
      dlc_service_proxy_->RegisterOnInstallStatusSignalHandler(
          base::Bind(&DlcServiceUtil::OnInstallStatus,
                     weak_ptr_factory_.GetWeakPtr()),
          base::Bind(&DlcServiceUtil::OnInstallStatusConnect,
                     weak_ptr_factory_.GetWeakPtr()));
      if (Install()) {
        // Don't |Quit()| as we will need to wait for signal of install.
        return EX_OK;
      }
    }

    // Called with "--uninstall".
    if (FLAGS_uninstall) {
      if (Uninstall()) {
        Quit();
        return EX_OK;
      }
    }

    Quit();
    return EX_SOFTWARE;
  }

  // Initialize the dlcservice proxy. Returns true on success, false otherwise.
  // Sets the given error pointer on failure.
  bool Init(int* error_ptr) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    scoped_refptr<dbus::Bus> bus{new dbus::Bus{options}};
    if (!bus->Connect()) {
      LOG(ERROR) << "Failed to connect to DBus.";
      *error_ptr = EX_UNAVAILABLE;
      return false;
    }
    dlc_service_proxy_ = std::make_unique<DlcServiceInterfaceProxy>(bus);

    return true;
  }

  // Callback invoked on receiving |OnInstallStatus| signal.
  void OnInstallStatus(const dlcservice::InstallStatus& install_status) {
    switch (install_status.status()) {
      case dlcservice::Status::COMPLETED:
        LOG(INFO) << "Install successful!: '" << dlc_module_list_str_ << "'.";
        Quit();
        break;
      case dlcservice::Status::RUNNING:
        LOG(INFO) << "Install in progress: " << install_status.progress();
        break;
      case dlcservice::Status::FAILED:
        LOG(ERROR) << "Failed to install: '" << dlc_module_list_str_
                   << "' with error code: " << install_status.error_code();
        QuitWithExitCode(EX_SOFTWARE);
        break;
      default:
        NOTREACHED();
    }
  }

  // Callback invoked on connecting |OnInstallStatus| signal.
  void OnInstallStatusConnect(const string& interface_name,
                              const string& signal_name,
                              bool success) {
    if (!success) {
      LOG(ERROR) << "Error connecting " << interface_name << "." << signal_name;
      QuitWithExitCode(EX_SOFTWARE);
    }
  }

  // Install current DLC module. Returns true if current module can be
  // installed. False otherwise.
  bool Install() {
    brillo::ErrorPtr err;
    LOG(INFO) << "Attempting to install DLC modules: " << dlc_module_list_str_;
    if (!dlc_service_proxy_->Install(dlc_module_list_, &err)) {
      LOG(ERROR) << "Failed to install: " << dlc_module_list_str_ << ", "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  // Uninstall a list of DLC modules. Returns true of all uninstall operations
  // complete successfully, false otherwise. Sets the given error pointer on
  // failure.
  bool Uninstall() {
    brillo::ErrorPtr err;
    for (const auto& dlc_module : dlc_module_list_.dlc_module_infos()) {
      const string& dlc_id = dlc_module.dlc_id();
      LOG(INFO) << "Attempting to uninstall DLC module '" << dlc_id << "'.";
      if (!dlc_service_proxy_->Uninstall(dlc_id, &err)) {
        LOG(ERROR) << "Failed to uninstall '" << dlc_id << ", "
                   << ErrorPtrStr(err);
        return false;
      }
      LOG(INFO) << "'" << dlc_id << "' successfully uninstalled.";
    }
    return true;
  }

  // Retrieves a list of all installed DLC modules. Returns true if the list is
  // retrieved successfully, false otherwise. Sets the given error pointer on
  // failure.
  bool GetInstalled(DlcModuleList* dlc_module_list) {
    brillo::ErrorPtr err;
    if (!dlc_service_proxy_->GetInstalled(dlc_module_list, &err)) {
      LOG(ERROR) << "Failed to get the list of installed DLC modules, "
                 << ErrorPtrStr(err);
      return false;
    }
    return true;
  }

  void PrintDlcModuleList(bool quiet) {
    std::cout << "Installed DLC modules:\n";
    for (const auto& dlc_module_info : dlc_module_list_.dlc_module_infos()) {
      std::cout << dlc_module_info.dlc_id() << std::endl;
      if (!quiet) {
        if (!DlcServiceUtil::PrintDlcDetails(dlc_module_info))
          LOG(ERROR) << "Failed to print details of DLC '"
                     << dlc_module_info.dlc_id() << "'.";
      }
    }
  }

  // Prints the information contained in the manifest of a DLC.
  static bool PrintDlcDetails(const DlcModuleInfo& dlc_info) {
    base::FilePath manifest_root =
        base::FilePath(imageloader::kDlcManifestRootpath);
    base::FilePath dlc_path =
        dlcservice::JoinPaths(manifest_root, dlc_info.dlc_id());
    // TODO(ahassani): This is a workaround. We need to get the list of packages
    // in the |GetInstalled()| or a separate signal. But for now since we just
    // have one package per DLC, this would work.
    std::set<string> packages = dlcservice::ScanDirectory(dlc_path);
    if (packages.empty()) {
      LOG(ERROR) << "Failed to get DLC package";
      return false;
    }
    string package = *(packages.begin());

    imageloader::Manifest manifest;
    if (!dlcservice::GetDlcManifest(manifest_root, dlc_info.dlc_id(), package,
                                    &manifest)) {
      LOG(ERROR) << "Failed to get DLC module manifest.";
      return false;
    }
    std::cout << "\tname: " << manifest.name() << std::endl;
    std::cout << "\tid: " << manifest.id() << std::endl;
    std::cout << "\tpackage: " << manifest.package() << std::endl;
    std::cout << "\tversion: " << manifest.version() << std::endl;
    std::cout << "\tmanifest version: " << manifest.manifest_version()
              << std::endl;
    std::cout << "\tpreallocated size: " << manifest.preallocated_size()
              << std::endl;
    std::cout << "\tsize: " << manifest.size() << std::endl;
    std::cout << "\timage type: " << manifest.image_type() << std::endl;
    std::cout << "\tremovable: " << (manifest.is_removable() ? "true" : "false")
              << std::endl;
    std::cout << "\tfs-type: ";
    switch (manifest.fs_type()) {
      case imageloader::FileSystem::kExt4:
        std::cout << "ext4" << std::endl;
        break;
      case imageloader::FileSystem::kSquashFS:
        std::cout << "squashfs" << std::endl;
        break;
    }
    std::cout << "\tdlc_path: " << dlc_path.value() << std::endl;
    std::cout << "\tdlc_root: " << dlc_info.dlc_root() << std::endl;
    return true;
  }

  std::unique_ptr<DlcServiceInterfaceProxy> dlc_service_proxy_;

  // argc and argv passed to main().
  int argc_;
  const char** argv_;

  // A list of DLC module IDs being installed.
  DlcModuleList dlc_module_list_;
  // A string representation of |dlc_module_list_|.
  string dlc_module_list_str_;
  // Customized Omaha server URL (empty being the default URL).
  string omaha_url_;

  base::WeakPtrFactory<DlcServiceUtil> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DlcServiceUtil);
};

int main(int argc, const char** argv) {
  // Check user that is running dlcservice_util.
  switch (getuid()) {
    case kRootUid:
      EnterMinijail();
      break;
    case kChronosUid:
      break;
    default:
      LOG(ERROR) << "dlcservice_util can only be run as root or chronos";
      return 1;
  }
  DlcServiceUtil client(argc, argv);
  return client.Run();
}
