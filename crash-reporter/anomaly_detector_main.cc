// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Lines in log files are parsed by a LogReader and a Parser each defined
// in anomaly_detector_log_reader.h and anomaly_detector.h. LogReader uses
// TextFileReader class to open a log file. TextFileReader is responsible for
// detecting log rotation and reopening the newly created log file.

#include "crash-reporter/anomaly_detector.h"
#include "crash-reporter/anomaly_detector_log_reader.h"

#include <memory>
#include <numeric>
#include <string>

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/strcat.h>
#include <base/time/default_clock.h>
#include <base/threading/platform_thread.h>
#include <brillo/flag_helper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_reporter_parser.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"
#include "metrics_event/proto_bindings/metrics_event.pb.h"

// work around https://crbug.com/849450: the LOG_WARNING macro from
// usr/include/sys/syslog.h overrides the LOG_WARNING constant in
// base/logging.h, causing LOG(WARNING) to not compile.
// TODO(https://crbug.com/849450): Remove this once bug is fixed.
#undef LOG_INFO
#undef LOG_WARNING

// Time between calls to Parser::PeriodicUpdate. Note that this is a minimum;
// the actual maximum is twice this (if the sd_journal_wait timeout starts just
// before the timeout in main()). We could make this more exact with some extra
// work, but it's not worth the trouble.
constexpr base::TimeDelta kTimeBetweenPeriodicUpdates =
    base::TimeDelta::FromSeconds(10);

const base::FilePath kMessageLogPath("/var/log/messages");

const base::FilePath kAuditLogPath("/var/log/audit/audit.log");

const base::FilePath kUpstartLogPath("/var/log/upstart.log");

constexpr base::TimeDelta kSleepBetweenLoop =
    base::TimeDelta::FromMilliseconds(100);

// Prepares for sending D-Bus signals. Returns a D-Bus object, which provides
// a handle for sending signals.
scoped_refptr<dbus::Bus> SetUpDBus(void) {
  // Connect the bus.
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> dbus(new dbus::Bus(options));
  CHECK(dbus);
  CHECK(dbus->Connect()) << "Failed to connect to D-Bus";
  CHECK(dbus->RequestOwnershipAndBlock(
      anomaly_detector::kAnomalyEventServiceName,
      dbus::Bus::ServiceOwnershipOptions::REQUIRE_PRIMARY))
      << "Failed to take ownership of the anomaly event service name";
  return dbus;
}

// Callback to run crash-reporter.
void RunCrashReporter(const std::vector<std::string>& flags,
                      const std::string& input) {
  LOG(INFO) << "anomaly_detector invoking crash_reporter with"
            << std::accumulate(flags.begin(), flags.end(), std::string(),
                               [](const std::string& a, const std::string& b) {
                                 return base::StrCat({a, " ", b});
                               });
  brillo::ProcessImpl cmd;
  cmd.AddArg("/sbin/crash_reporter");
  for (const std::string& flag : flags) {
    cmd.AddArg(flag);
  }
  cmd.RedirectUsingPipe(STDIN_FILENO, true);
  CHECK(cmd.Start());
  int stdin_fd = cmd.GetPipe(STDIN_FILENO);
  CHECK(base::WriteFileDescriptor(stdin_fd, input.data(), input.length()));
  CHECK_GE(close(stdin_fd), 0);
  CHECK_EQ(0, cmd.Wait());
}

std::unique_ptr<dbus::Signal> MakeOomSignal(const int64_t oom_timestamp_ms) {
  auto signal = std::make_unique<dbus::Signal>(
      anomaly_detector::kAnomalyEventServiceInterface,
      anomaly_detector::kAnomalyEventSignalName);
  dbus::MessageWriter writer(signal.get());
  metrics_event::Event payload;
  payload.set_type(metrics_event::Event_Type_OOM_KILL_KERNEL);
  payload.set_timestamp(oom_timestamp_ms);
  writer.AppendProtoAsArrayOfBytes(payload);

  return signal;
}

int main(int argc, char* argv[]) {
  DEFINE_bool(testonly_send_all, false,
              "True iff the anomaly detector should send all reports. "
              "Only use for testing.");
  brillo::FlagHelper::Init(argc, argv, "Chromium OS Anomaly Detector");
  // Sim sala bim!  These are needed to send D-Bus signals.  Even though they
  // are not used directly, they set up some global state needed by the D-Bus
  // library.
  base::MessageLoop message_loop;
  base::AtExitManager at_exit_manager;

  brillo::OpenLog("anomaly_detector", true);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  scoped_refptr<dbus::Bus> dbus = SetUpDBus();
  // Export a bus object so that other processes can register signal handlers
  // (this service only sends signals, no methods are exported).
  dbus::ExportedObject* exported_object = dbus->GetExportedObject(
      dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath));
  CHECK(exported_object);

  std::map<std::string, std::unique_ptr<anomaly::Parser>> parsers;
  parsers["audit"] =
      std::make_unique<anomaly::SELinuxParser>(FLAGS_testonly_send_all);
  parsers["init"] =
      std::make_unique<anomaly::ServiceParser>(FLAGS_testonly_send_all);
  parsers["kernel"] = std::make_unique<anomaly::KernelParser>();
  parsers["powerd_suspend"] = std::make_unique<anomaly::SuspendParser>();
  parsers["crash_reporter"] = std::make_unique<anomaly::CrashReporterParser>(
      std::make_unique<base::DefaultClock>(),
      std::make_unique<MetricsLibrary>());
  auto termina_parser = std::make_unique<anomaly::TerminaParser>(dbus);

  base::Time last_periodic_update = base::Time::Now();

  // If any log file is missing, the LogReader will try to reopen the file on
  // GetNextEntry method call. After multiple attempts however LogReader will
  // give up and logs the error. Note that some boards do not have SELinux and
  // thus no audit.log.
  anomaly::AuditReader audit_reader(kAuditLogPath, anomaly::kAuditLogPattern);
  anomaly::MessageReader message_reader(kMessageLogPath,
                                        anomaly::kMessageLogPattern);
  anomaly::MessageReader upstart_reader(kUpstartLogPath,
                                        anomaly::kUpstartLogPattern);
  anomaly::LogReader* log_readers[]{&audit_reader, &message_reader,
                                    &upstart_reader};

  // Indicate to tast tests that anomaly-detector has started.
  base::FilePath path = base::FilePath(paths::kSystemRunStateDirectory)
                            .Append(paths::kAnomalyDetectorReady);
  if (base::WriteFile(path, "", 0) == -1) {
    // Log but don't prevent anomaly detector from starting because this file
    // is not essential to its operation.
    PLOG(ERROR) << "Couldn't write " << path.value() << " (tests may fail)";
  }

  while (true) {
    for (auto* reader : log_readers) {
      anomaly::LogEntry entry;
      while (reader->GetNextEntry(&entry)) {
        anomaly::MaybeCrashReport crash_report;
        if (parsers.count(entry.tag) > 0) {
          crash_report = parsers[entry.tag]->ParseLogEntry(entry.message);
        } else if (entry.tag.compare(0, 3, "VM(") == 0) {
          crash_report =
              termina_parser->ParseLogEntry(entry.tag, entry.message);
        }

        if (crash_report) {
          RunCrashReporter(crash_report->flags, crash_report->text);
        }

        // Handle OOM messages.
        if (entry.tag == "kernel" &&
            entry.message.find("Out of memory: Kill process") !=
                std::string::npos)
          exported_object->SendSignal(
              MakeOomSignal(
                  static_cast<int>(entry.timestamp.ToDoubleT() * 1000))
                  .get());
      }
    }

    if (last_periodic_update <=
        base::Time::Now() - kTimeBetweenPeriodicUpdates) {
      for (const auto& parser : parsers) {
        parser.second->PeriodicUpdate();
      }
      last_periodic_update = base::Time::Now();
    }

    base::PlatformThread::Sleep(kSleepBetweenLoop);
  }
}
