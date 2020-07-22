// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <base/strings/string_util.h>
#include <base/strings/string_split.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/errors/error.h>
#include <brillo/flag_helper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/dbus-proxies.h"

using org::chromium::lorgnette::ManagerProxy;

namespace {

constexpr uint32_t kScanResolutionLow = 100;

base::Optional<std::vector<std::string>> ReadLines(base::File* file) {
  std::string buf(1 << 20, '\0');
  int read = file->ReadAtCurrentPos(&buf[0], buf.size());
  if (read < 0) {
    PLOG(ERROR) << "Reading from file failed";
    return base::nullopt;
  }

  buf.resize(read);
  return base::SplitString(buf, "\n", base::KEEP_WHITESPACE,
                           base::SPLIT_WANT_ALL);
}

class SignalHandler {
 public:
  explicit SignalHandler(base::RepeatingClosure quit_closure)
      : cvar_(&lock_), quit_closure_(quit_closure) {}

  bool WaitUntilConnected();

  void HandleScanStatusChangedSignal(
      const std::vector<uint8_t>& signal_serialized);

  void OnConnectedCallback(const std::string& interface_name,
                           const std::string& signal_name,
                           bool signal_connected);

  base::WeakPtr<SignalHandler> GetWeakPtr();

 private:
  base::Lock lock_;
  base::ConditionVariable cvar_;
  base::RepeatingClosure quit_closure_;

  bool connected_callback_called_;
  bool connection_status_;

  base::WeakPtrFactory<SignalHandler> weak_factory_{this};
};

bool SignalHandler::WaitUntilConnected() {
  base::AutoLock auto_lock(lock_);
  while (!connected_callback_called_) {
    cvar_.Wait();
  }
  return connection_status_;
}

void SignalHandler::HandleScanStatusChangedSignal(
    const std::vector<uint8_t>& signal_serialized) {
  lorgnette::ScanStatusChangedSignal signal;
  if (!signal.ParseFromArray(signal_serialized.data(),
                             signal_serialized.size())) {
    LOG(ERROR) << "Failed to parse ScanStatusSignal";
    return;
  }

  if (signal.state() == lorgnette::SCAN_STATE_IN_PROGRESS) {
    std::cout << "Scan " << signal.scan_uuid() << " is " << signal.progress()
              << "% finished" << std::endl;
  } else if (signal.state() == lorgnette::SCAN_STATE_FAILED) {
    std::cout << "Scan " << signal.scan_uuid()
              << "failed: " << signal.failure_reason() << std::endl;
    quit_closure_.Run();
  } else if (signal.state() == lorgnette::SCAN_STATE_COMPLETED) {
    std::cout << "Scan " << signal.scan_uuid() << " completed successfully."
              << std::endl;
    quit_closure_.Run();
  }
}

void SignalHandler::OnConnectedCallback(const std::string& interface_name,
                                        const std::string& signal_name,
                                        bool signal_connected) {
  base::AutoLock auto_lock(lock_);
  connected_callback_called_ = true;
  connection_status_ = signal_connected;
  if (!signal_connected) {
    LOG(ERROR) << "Failed to connect to ScanStatusChanged signal";
  }
  cvar_.Signal();
}

base::WeakPtr<SignalHandler> SignalHandler::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

base::Optional<std::vector<std::string>> ListScanners(ManagerProxy* manager) {
  brillo::ErrorPtr error;
  std::vector<uint8_t> out_scanner_list;
  if (!manager->ListScanners(&out_scanner_list, &error)) {
    LOG(ERROR) << "ListScanners failed: " << error->GetMessage();
    return base::nullopt;
  }

  lorgnette::ListScannersResponse scanner_list;
  if (!scanner_list.ParseFromArray(out_scanner_list.data(),
                                   out_scanner_list.size())) {
    LOG(ERROR) << "Failed to parse ListScanners response";
    return base::nullopt;
  }

  std::vector<std::string> scanner_names;
  std::cout << "SANE scanners: " << std::endl;
  for (const lorgnette::ScannerInfo& scanner : scanner_list.scanners()) {
    std::cout << scanner.name() << ": " << scanner.manufacturer() << " "
              << scanner.model() << "(" << scanner.type() << ")" << std::endl;
    scanner_names.push_back(scanner.name());
  }
  std::cout << scanner_list.scanners_size() << " SANE scanners found."
            << std::endl;
  return scanner_names;
}

bool PrintScannerCapabilities(ManagerProxy* manager,
                              const std::string& scanner_name) {
  brillo::ErrorPtr error;
  std::vector<uint8_t> capabilities_out;
  if (!manager->GetScannerCapabilities(scanner_name, &capabilities_out,
                                       &error)) {
    LOG(ERROR) << "GetScannerCapabilities failed: " << error->GetMessage();
    return false;
  }

  lorgnette::ScannerCapabilities capabilities;
  if (!capabilities.ParseFromArray(capabilities_out.data(),
                                   capabilities_out.size())) {
    LOG(ERROR) << "Failed to parse ScannerCapabilities response";
    return false;
  }

  std::cout << "--- Capabilities ---" << std::endl;
  std::cout << "Resolutions:" << std::endl;
  for (uint32_t resolution : capabilities.resolutions()) {
    std::cout << "\t" << resolution << std::endl;
  }

  std::cout << "Sources:" << std::endl;
  for (const lorgnette::DocumentSource& source : capabilities.sources()) {
    std::cout << "\t" << source.name() << " ("
              << lorgnette::SourceType_Name(source.type()) << ")" << std::endl;
  }

  std::cout << "Color Modes:" << std::endl;
  for (int color_mode : capabilities.color_modes()) {
    std::cout << "\t" << lorgnette::ColorMode_Name(color_mode) << std::endl;
  }
  return true;
}

base::Optional<std::vector<std::string>> ReadAirscanOutput(
    brillo::ProcessImpl* discover) {
  base::File discover_output(discover->GetPipe(STDOUT_FILENO));
  if (!discover_output.IsValid()) {
    LOG(ERROR) << "Failed to open airscan-discover output pipe";
    return base::nullopt;
  }

  int ret = discover->Wait();
  if (ret != 0) {
    LOG(ERROR) << "airscan-discover exited with error " << ret;
    return base::nullopt;
  }

  base::Optional<std::vector<std::string>> lines = ReadLines(&discover_output);
  if (!lines.has_value()) {
    LOG(ERROR) << "Failed to read output from airscan-discover";
    return base::nullopt;
  }

  const std::string protocol = ", eSCL";
  std::vector<std::string> scanner_names;
  for (const std::string& line : lines.value()) {
    size_t equals = line.find('=');
    size_t suffix = line.find(protocol, equals);
    if (equals != std::string::npos && suffix != std::string::npos) {
      std::string name = line.substr(0, equals);
      base::TrimWhitespaceASCII(name, base::TrimPositions::TRIM_ALL, &name);

      std::string url = line.substr(equals + 1, suffix - (equals + 1));
      base::TrimWhitespaceASCII(url, base::TrimPositions::TRIM_ALL, &url);

      scanner_names.push_back("airscan:escl:" + name + ":" + url);
    }
  }

  return scanner_names;
}

bool StartScan(ManagerProxy* manager,
               const std::string& scanner,
               const base::FilePath& output_path) {
  base::File output_file(
      output_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!output_file.IsValid()) {
    PLOG(ERROR) << "Failed to open output file " << output_path;
    return false;
  }

  lorgnette::StartScanRequest request;
  request.set_device_name(scanner);
  request.mutable_settings()->set_resolution(kScanResolutionLow);
  request.mutable_settings()->set_color_mode(lorgnette::MODE_COLOR);
  std::vector<uint8_t> request_in(request.ByteSizeLong());
  request.SerializeToArray(request_in.data(), request_in.size());

  brillo::ErrorPtr error;
  std::vector<uint8_t> response_out;
  if (!manager->StartScan(request_in, output_file.GetPlatformFile(),
                          &response_out, &error)) {
    LOG(ERROR) << "StartScan failed: " << error->GetMessage();
    return false;
  }

  lorgnette::StartScanResponse response;
  if (!response.ParseFromArray(response_out.data(), response_out.size())) {
    LOG(ERROR) << "Failed to parse StartScanResponse";
    return false;
  }

  if (response.state() == lorgnette::SCAN_STATE_FAILED) {
    LOG(ERROR) << "StartScan failed: " << response.failure_reason();
    return false;
  }

  std::cout << "Scan " << response.scan_uuid() << " started successfully"
            << std::endl;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty |
                  brillo::kLogHeader);

  brillo::FlagHelper::Init(
      argc, argv,
      "lorgnette_cli, command-line interface to Chromium OS Scanning Daemon");

  if (base::CommandLine::ForCurrentProcess()->GetArgs().size() != 0) {
    LOG(ERROR) << "Unexpected command-line argument";
    return 1;
  }

  // Start the airscan-discover process immediately since it can be slightly
  // long-running. We read the output later after we've gotten a scanner list
  // from lorgnette.
  brillo::ProcessImpl discover;
  discover.AddArg("/usr/bin/airscan-discover");
  discover.RedirectUsingPipe(STDOUT_FILENO, false);
  if (!discover.Start()) {
    LOG(ERROR) << "Failed to start airscan-discover process";
    return 1;
  }

  // Create a task executor for this thread. This will automatically be bound
  // to the current thread so that it is usable by other code for posting tasks.
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);

  // Create a FileDescriptorWatcher instance for this thread. The libbase D-Bus
  // bindings use this internally via thread-local storage, but do not properly
  // instantiate it.
  base::FileDescriptorWatcher watcher(executor.task_runner());

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  auto manager =
      std::make_unique<ManagerProxy>(bus, lorgnette::kManagerServiceName);

  std::cout << "Getting scanner list." << std::endl;
  base::Optional<std::vector<std::string>> sane_scanners =
      ListScanners(manager.get());
  if (!sane_scanners.has_value())
    return 1;

  base::Optional<std::vector<std::string>> airscan_scanners =
      ReadAirscanOutput(&discover);
  if (!airscan_scanners.has_value())
    return 1;

  std::vector<std::string> scanners = std::move(sane_scanners.value());
  scanners.insert(scanners.end(), airscan_scanners.value().begin(),
                  airscan_scanners.value().end());

  std::cout << "Choose a scanner (blank to quit):" << std::endl;
  for (int i = 0; i < scanners.size(); i++) {
    std::cout << i << ". " << scanners[i] << std::endl;
  }
  int index = -1;
  std::cout << "> ";
  std::cin >> index;
  if (std::cin.fail()) {
    return 0;
  }

  std::string scanner = scanners[index];
  std::cout << "Getting device capabilities for " << scanner << std::endl;
  if (!PrintScannerCapabilities(manager.get(), scanner))
    return 1;

  // Implicitly uses this thread's executor as defined above.
  base::RunLoop run_loop;
  SignalHandler handler(run_loop.QuitClosure());
  manager->RegisterScanStatusChangedSignalHandler(
      base::BindRepeating(&SignalHandler::HandleScanStatusChangedSignal,
                          handler.GetWeakPtr()),
      base::BindOnce(&SignalHandler::OnConnectedCallback,
                     handler.GetWeakPtr()));

  if (!handler.WaitUntilConnected()) {
    return 1;
  }

  std::cout << "Scanning from " << scanner << std::endl;

  std::string escaped_scanner;
  for (char c : scanner) {
    if (isalnum(c)) {
      escaped_scanner += c;
    } else {
      escaped_scanner += '_';
    }
  }
  base::FilePath output_path("/tmp/scan-" + escaped_scanner + ".png");

  if (!StartScan(manager.get(), scanner, output_path)) {
    return 1;
  }

  // Will run until the SignalHandler runs this RunLoop's quit_closure.
  run_loop.Run();

  std::cout << "Scanned to " << output_path << std::endl;
  return 0;
}
