// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <memory>
#include <random>
#include <string>

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/message_loop/message_loop.h>
#include <base/time/default_clock.h>
#include <brillo/flag_helper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <metrics/metrics_library.h>

#include <systemd/sd-journal.h>

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

struct JournalEntry {
  // Value of SYSLOG_IDENTIFIER. Generally, the program's short name.
  std::string tag;
  std::string message;
  uint64_t monotonic_usec;
};

class Journal {
 public:
  Journal() {
    int ret = sd_journal_open(&j_, SD_JOURNAL_SYSTEM | SD_JOURNAL_LOCAL_ONLY);
    CHECK_GE(ret, 0) << "Could not open journal: " << strerror(-ret);
    // Go directly to the end of the file.  We don't want to parse the same
    // anomalies multiple times on reboot/restart.  We might miss some
    // anomalies, but so be it---it's too hard to keep track reliably of the
    // last parsed position in the syslog.
    SeekToEnd();
  }

  base::Optional<JournalEntry> GetNextEntry() {
    if (!MoveToNext()) {
      return base::nullopt;
    }
    auto tag = GetFieldValue("SYSLOG_IDENTIFIER");
    auto message = GetFieldValue("MESSAGE");
    if (tag && message) {
      uint64_t monotonic_usec;
      sd_id128_t ignore;
      int ret = sd_journal_get_monotonic_usec(j_, &monotonic_usec, &ignore);
      CHECK_GE(ret, 0) << "Failed to get monotonic timestamp from journal: "
                       << strerror(-ret);
      JournalEntry je{std::move(*tag), std::move(*message), monotonic_usec};
      return je;
    } else {
      return GetNextEntry();
    }
  }

 private:
  void SeekToEnd() {
    int ret = sd_journal_seek_tail(j_);
    CHECK_GE(ret, 0) << "Could not seek to end of journal: " << strerror(-ret);
  }

  // Returns true if a next entry was found, false otherwise.
  bool MoveToNext() {
    int ret = sd_journal_next(j_);
    CHECK_GE(ret, 0) << "Failed to iterate to next journal entry: "
                     << strerror(-ret);
    if (ret == 0) {
      /* Reached the end, let's wait for changes, and try again. */
      ret = sd_journal_wait(j_, kTimeBetweenPeriodicUpdates.InMicroseconds());
      if (ret == SD_JOURNAL_NOP) {
        // Timeout.
        return false;
      }
      CHECK_GE(ret, 0) << "Failed to wait for journal changes: "
                       << strerror(-ret);
      return MoveToNext();
    }

    return true;
  }

  base::Optional<std::string> GetFieldValue(const std::string& field) {
    const char* data = nullptr;
    size_t length = 0;
    int ret =
        sd_journal_get_data(j_, field.c_str(), (const void**)&data, &length);
    if (ret == -EBADMSG) {
      LOG(WARNING) << "Ignoring corrupt journal entry: " << field;
      return base::nullopt;
    }
    if (ret == -ENOENT)
      return base::nullopt;
    CHECK_GE(ret, 0) << "Failed to read field '" << field
                     << "' from journal: " << strerror(-ret);
    data += field.length() + 1;
    length -= field.length() + 1;

    return std::string(data, length);
  }

  sd_journal* j_ = 0;
};

// Prepares for sending D-Bus signals.  Returns a D-Bus object, which provides
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
void RunCrashReporter(const std::string& flag, const std::string& input) {
  LOG(INFO) << "anomaly_detector invoking crash_reporter with " << flag;
  brillo::ProcessImpl cmd;
  cmd.AddArg("/sbin/crash_reporter");
  cmd.AddArg(flag);
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

  // We only want to report 0.1% of selinux violations. Set up the random
  // distribution.
  std::default_random_engine gen((std::random_device())());
  std::bernoulli_distribution drop_audit_report(1.0 -
                                                1.0 / util::GetSelinuxWeight());
  // Only report 2% of service failures due to noise.
  // TODO(https://crbug.com/1017491): Remove this once the rate of service
  // failures is acceptably low.
  std::bernoulli_distribution drop_service_failure_report(
      1.0 - 1.0 / util::GetServiceFailureWeight());

  Journal j;

  base::FilePath path = base::FilePath(paths::kSystemRunStateDirectory)
                            .Append(paths::kAnomalyDetectorReady);
  if (base::WriteFile(path, "", 0) == -1) {
    // Log but don't prevent anomaly detector from starting because this file
    // is not essential to its operation.
    PLOG(ERROR) << "Couldn't write " << path.value() << " (tests may fail)";
  }

  std::map<std::string, std::unique_ptr<anomaly::Parser>> parsers;
  parsers["audit"] = std::make_unique<anomaly::SELinuxParser>();
  parsers["init"] = std::make_unique<anomaly::ServiceParser>();
  parsers["kernel"] = std::make_unique<anomaly::KernelParser>();
  parsers["powerd_suspend"] = std::make_unique<anomaly::SuspendParser>();
  parsers["crash_reporter"] = std::make_unique<anomaly::CrashReporterParser>(
      std::make_unique<base::DefaultClock>(),
      std::make_unique<MetricsLibrary>());
  auto termina_parser = std::make_unique<anomaly::TerminaParser>(dbus);

  base::Time last_periodic_update = base::Time::Now();

  while (true) {
    base::Optional<JournalEntry> entry = j.GetNextEntry();
    if (entry) {
      anomaly::MaybeCrashReport crash_report;
      if (parsers.count(entry->tag) > 0) {
        crash_report = parsers[entry->tag]->ParseLogEntry(entry->message);
      } else if (entry->tag.compare(0, 3, "VM(") == 0) {
        crash_report =
            termina_parser->ParseLogEntry(entry->tag, entry->message);
      }

      if (crash_report) {
        if (!FLAGS_testonly_send_all) {
          if (entry->tag == "audit" && drop_audit_report(gen)) {
            continue;
          } else if (entry->tag == "init" && drop_service_failure_report(gen)) {
            LOG(INFO) << "Dropping service failure report: "
                      << crash_report->text;
            continue;
          }
        }
        RunCrashReporter(crash_report->flag, crash_report->text);
      }

      // Handle OOM messages.
      if (entry->tag == "kernel" &&
          entry->message.find("Out of memory: Kill process") !=
              std::string::npos)
        exported_object->SendSignal(
            MakeOomSignal(entry->monotonic_usec / 1000).get());
    }

    if (last_periodic_update <=
        base::Time::Now() - kTimeBetweenPeriodicUpdates) {
      for (const auto& parser : parsers) {
        parser.second->PeriodicUpdate();
      }
      last_periodic_update = base::Time::Now();
    }
  }
}
