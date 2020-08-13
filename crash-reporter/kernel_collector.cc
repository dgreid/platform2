// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_collector.h"

#include <sys/stat.h>
#include <algorithm>
#include <cinttypes>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

using base::FilePath;
using base::StringPiece;
using base::StringPrintf;

#include "crash-reporter/util.h"

namespace {

// Name for extra BIOS dump attached to report. Also used as metadata key.
constexpr char kBiosDumpName[] = "bios_log";
const FilePath kBiosLogPath("/sys/firmware/log");
// Names of the four BIOS stages in which the BIOS log can start.
const char* const kBiosStageNames[] = {
    "bootblock",
    "verstage",
    "romstage",
    "ramstage",
};
constexpr char kDefaultKernelStackSignature[] =
    "kernel-UnspecifiedStackSignature";
constexpr char kDumpParentPath[] = "/sys/fs";
constexpr char kDumpPath[] = "/sys/fs/pstore";
constexpr char kDumpRecordDmesgName[] = "dmesg";
constexpr char kDumpRecordConsoleName[] = "console";
constexpr char kDumpDriverRamoopsName[] = "ramoops";
constexpr char kDumpDriverEfiName[] = "efi";
// The files take the form <record type>-<driver name>-<record id>.
// e.g. console-ramoops-0 or dmesg-ramoops-0.
constexpr char kDumpNameFormat[] = "%s-%s-%zu";
// Like above, but for older systems when the kernel didn't add the record id.
constexpr char kDumpNameFormatOld[] = "%s-%s";

const FilePath kEventLogPath("/var/log/eventlog.txt");
constexpr char kEventNameBoot[] = "System boot";
constexpr char kEventNameWatchdog[] = "Hardware watchdog reset";
constexpr char kKernelExecName[] = "kernel";
// Maximum number of records to examine in the kDumpPath.
constexpr size_t kMaxDumpRecords = 100;
// Maximum buffer size of pstore records reads. PSTORE_DEFAULT_KMSG_BYTES as set
// in the kernel is 10KiB, and record_size for RAM Oops/Panic trigger is
// defaulted at 4KiB with another 4KiB each for console, ftrace, and pmsg logs.
// This gives a maximum of ~26 KiB, but in practice logs can be significantly
// larger (e.g. 66 KiB is easily achieved). Set a limit substantially above
// this.
constexpr size_t kMaxRecordSize = 1024 * 1024;
constexpr pid_t kKernelPid = 0;
constexpr char kKernelSignatureKey[] = "sig";
// Byte length of maximum human readable portion of a kernel crash signature.
constexpr size_t kMaxHumanStringLength = 40;
// Time in seconds from the final kernel log message for a call stack
// to count towards the signature of the kcrash.
constexpr int kSignatureTimestampWindow = 2;
// Kernel log timestamp regular expression.
constexpr char kTimestampRegex[] = "^<.*>\\[\\s*(\\d+\\.\\d+)\\]";

//
// These regular expressions enable to us capture the PC in a backtrace.
// The backtrace is obtained through dmesg or the kernel's preserved/kcrashmem
// feature.
//
// For ARM we see:
//   "<5>[   39.458982] PC is at write_breakme+0xd0/0x1b4" (arm32)
//   "<4>[  263.857834] pc : lkdtm_BUG+0xc/0x10" (arm64)
// For MIPS we see:
//   "<5>[ 3378.552000] epc   : 804010f0 lkdtm_do_action+0x68/0x3f8"
// For x86:
//   "<0>[   37.474699] EIP: [<790ed488>] write_breakme+0x80/0x108
//    SS:ESP 0068:e9dd3efc"
//
const char* const kPCRegex[] = {
    nullptr, " (?:PC is at |pc : )([^\\+\\[ ]+).*",
    " epc\\s+:\\s+\\S+\\s+([^\\+ ]+).*",  // MIPS has an exception program
                                          // counter
    " EIP: \\[<.*>\\] ([^\\+ ]+).*",  // X86 uses EIP for the program counter
    " RIP \\[<.*>\\] ([^\\+ ]+).*",   // X86_64 uses RIP for the program counter
};

static_assert(base::size(kPCRegex) == KernelCollector::kArchCount,
              "Missing Arch PC RegExp");

pcrecpp::RE kSanityCheckRe("\n(<\\d+>)?\\[\\s*(\\d+\\.\\d+)\\]");

}  // namespace

KernelCollector::KernelCollector()
    : CrashCollector("kernel"),
      is_enabled_(false),
      eventlog_path_(kEventLogPath),
      dump_path_(kDumpPath),
      bios_log_path_(kBiosLogPath),
      records_(0),
      // We expect crash dumps in the format of architecture we are built for.
      arch_(GetCompilerArch()) {}

KernelCollector::~KernelCollector() {}

void KernelCollector::OverrideEventLogPath(const FilePath& file_path) {
  eventlog_path_ = file_path;
}

void KernelCollector::OverrideBiosLogPath(const FilePath& file_path) {
  bios_log_path_ = file_path;
}

void KernelCollector::OverridePreservedDumpPath(const FilePath& file_path) {
  dump_path_ = file_path;
}

bool KernelCollector::ReadRecordToString(std::string* contents,
                                         size_t current_record,
                                         bool* record_found) {
  // A record is a ramoops dump. It has an associated size of "record_size".
  std::string record;
  std::string captured;

  // Ramoops appends a header to a crash which contains ==== followed by a
  // timestamp. Ignore the header.
  pcrecpp::RE record_re(
      "====\\d+\\.\\d+\n(.*)",
      pcrecpp::RE_Options().set_multiline(true).set_dotall(true));

  FilePath record_path = GetDumpRecordPath(
      kDumpRecordDmesgName, kDumpDriverRamoopsName, current_record);
  if (!base::ReadFileToStringWithMaxSize(record_path, &record,
                                         kMaxRecordSize)) {
    if (record.empty()) {
      PLOG(ERROR) << "Unable to read " << record_path.value();
      return false;
    }

    PLOG(ERROR) << "Record is larger than " << kMaxRecordSize;
    return false;
  }

  *record_found = false;
  if (record_re.FullMatch(record, &captured)) {
    // Found a ramoops header, so strip the header and append the rest.
    contents->append(captured);
    *record_found = true;
  } else if (kSanityCheckRe.PartialMatch(record.substr(0, 1024))) {
    // pstore compression has been added since kernel 3.12. In order to
    // decompress dmesg correctly, ramoops driver has to strip the header
    // before handing over the record to the pstore driver, so we don't
    // need to do it here anymore. However, the sanity check is needed because
    // sometimes a pstore record is just a chunk of uninitialized memory which
    // is not the result of a kernel crash. See crbug.com/443764
    contents->append(record);
    *record_found = true;
  } else {
    LOG(WARNING) << "Found invalid record at " << record_path.value();
  }

  // Remove the record from pstore after it's found.
  if (*record_found)
    base::DeleteFile(record_path, false);

  return true;
}

FilePath KernelCollector::GetDumpRecordPath(const char* type,
                                            const char* driver,
                                            size_t record) {
  return dump_path_.Append(StringPrintf(kDumpNameFormat, type, driver, record));
}

FilePath KernelCollector::GetDumpRecordOldPath(const char* type,
                                               const char* driver) {
  return dump_path_.Append(StringPrintf(kDumpNameFormatOld, type, driver));
}

bool KernelCollector::LoadParameters() {
  // Discover how many ramoops records are being exported by the driver.
  size_t count;

  for (count = 0; count < kMaxDumpRecords; ++count) {
    FilePath record_path =
        GetDumpRecordPath(kDumpRecordDmesgName, kDumpDriverRamoopsName, count);

    if (!base::PathExists(record_path))
      break;
  }

  records_ = count;
  return (records_ > 0);
}

bool KernelCollector::LoadPreservedDump(std::string* contents) {
  // Load dumps from the preserved memory and save them in contents.
  // Since the system is set to restart on oops we won't actually ever have
  // multiple records (only 0 or 1), but check in case we don't restart on
  // oops in the future.
  bool any_records_found = false;
  bool record_found = false;
  // clear contents since ReadFileToString actually appends to the string.
  contents->clear();

  for (size_t i = 0; i < records_; ++i) {
    if (!ReadRecordToString(contents, i, &record_found)) {
      break;
    }
    if (record_found) {
      any_records_found = true;
    }
  }

  if (!any_records_found) {
    LOG(ERROR) << "No valid records found in " << dump_path_.value();
    return false;
  }

  return true;
}

bool KernelCollector::LoadLastBootBiosLog(std::string* contents) {
  contents->clear();

  if (!base::PathExists(bios_log_path_)) {
    LOG(INFO) << bios_log_path_.value() << " does not exist, skipping "
              << "BIOS crash check. (This is normal for older boards.)";
    return false;
  }

  std::string full_log;
  if (!base::ReadFileToString(bios_log_path_, &full_log)) {
    PLOG(ERROR) << "Unable to read " << bios_log_path_.value();
    return false;
  }

  // Different platforms start their BIOS log at different stages. Look for
  // banner strings of all stages in order until we find one that works.
  for (auto stage : kBiosStageNames) {
    pcrecpp::RE banner_re(
        StringPrintf("(.*?)(?="
                     "\n\\*\\*\\* Pre-CBMEM %s console overflow"
                     "|"
                     "\n\ncoreboot-[^\n]* %s starting.*\\.\\.\\.\n"
                     ")",
                     stage, stage),
        pcrecpp::RE_Options().set_multiline(true).set_dotall(true));
    pcrecpp::StringPiece remaining_log(full_log);
    pcrecpp::StringPiece previous_boot;
    bool found = false;

    // Keep iterating until last previous_boot before current one.
    while (banner_re.Consume(&remaining_log, &previous_boot)) {
      remaining_log.remove_prefix(1);
      found = true;
    }

    if (!previous_boot.empty()) {
      previous_boot.CopyToString(contents);
      return true;
    }

    // If banner found but no log before it, don't look for other stage banners.
    // This just means we booted up from S5 and there was nothing left in DRAM.
    if (found)
      return false;
  }

  // This shouldn't happen since we should always see at least the current boot.
  LOG(ERROR) << "BIOS log contains no known banner strings!";
  return false;
}

bool KernelCollector::LastRebootWasBiosCrash(const std::string& dump) {
  // BIOS crash detection only supported on ARM64 for now. We're in userspace,
  // so we can't easily check for 64-bit (but that's not a big deal).
  if (arch_ != kArchArm)
    return false;

  if (dump.empty())
    return false;

  return pcrecpp::RE("(PANIC|Unhandled( Interrupt)? Exception) in EL3")
      .PartialMatch(dump);
}

// We can't always trust kernel watchdog drivers to correctly report the boot
// reason, since on some platforms our BIOS has to reinitialize the hardware
// registers in a way that clears this information. Instead read the BIOS
// eventlog to figure out if a watchdog reset was detected during the last boot.
bool KernelCollector::LastRebootWasWatchdog() {
  if (!base::PathExists(eventlog_path_)) {
    LOG(INFO) << "Cannot find " << eventlog_path_.value()
              << ", skipping hardware watchdog check.";
    return false;
  }

  std::string eventlog;
  if (!base::ReadFileToString(eventlog_path_, &eventlog)) {
    PLOG(ERROR) << "Unable to open " << eventlog_path_.value();
    return false;
  }

  StringPiece piece = StringPiece(eventlog);
  size_t last_boot = piece.rfind(kEventNameBoot);
  if (last_boot == StringPiece::npos)
    return false;

  return piece.find(kEventNameWatchdog, last_boot) != StringPiece::npos;
}

bool KernelCollector::LoadConsoleRamoops(std::string* contents) {
  FilePath record_path;

  // We assume there is only one record.  Bad idea?
  record_path =
      GetDumpRecordPath(kDumpRecordConsoleName, kDumpDriverRamoopsName, 0);

  // Deal with the filename change starting with linux-3.19+.
  if (!base::PathExists(record_path)) {
    // If the file doesn't exist, we might be running on an older system which
    // uses the older file name format (<linux-3.19).
    record_path =
        GetDumpRecordOldPath(kDumpRecordConsoleName, kDumpDriverRamoopsName);
    if (!base::PathExists(record_path)) {
      LOG(WARNING) << "No console-ramoops file found after watchdog reset";
      return false;
    }
  }

  if (!base::ReadFileToString(record_path, contents)) {
    PLOG(ERROR) << "Unable to open " << record_path.value();
    return false;
  }

  if (!kSanityCheckRe.PartialMatch(contents->substr(0, 1024))) {
    LOG(WARNING) << "Found invalid console-ramoops file";
    return false;
  }

  return true;
}

bool KernelCollector::DumpDirMounted() {
  struct stat st_parent;
  if (stat(kDumpParentPath, &st_parent)) {
    PLOG(WARNING) << "Could not stat " << kDumpParentPath;
    return false;
  }

  struct stat st_dump;
  if (stat(kDumpPath, &st_dump)) {
    PLOG(WARNING) << "Could not stat " << kDumpPath;
    return false;
  }

  if (st_parent.st_dev == st_dump.st_dev) {
    LOG(WARNING) << "Dump dir " << kDumpPath << " not mounted";
    return false;
  }

  return true;
}

bool KernelCollector::Enable() {
  if (arch_ == kArchUnknown || arch_ >= kArchCount ||
      kPCRegex[arch_] == nullptr) {
    LOG(WARNING) << "KernelCollector does not understand this architecture";
    return false;
  }

  if (!DumpDirMounted()) {
    LOG(WARNING) << "Kernel does not support crash dumping";
    return false;
  }

  // To enable crashes, we will eventually need to set
  // the chnv bit in BIOS, but it does not yet work.
  LOG(INFO) << "Enabling kernel crash handling";
  is_enabled_ = true;
  return true;
}

void KernelCollector::ProcessStackTrace(pcrecpp::StringPiece kernel_dump,
                                        unsigned* hash,
                                        float* last_stack_timestamp,
                                        bool* is_watchdog_crash) {
  pcrecpp::RE line_re("(.+)", pcrecpp::MULTILINE());
  pcrecpp::RE stack_trace_start_re(
      std::string(kTimestampRegex) + " (Call Trace|Backtrace):$",
      pcrecpp::CASELESS());

  // Match lines such as the following and grab out "function_name".
  // The ? may or may not be present.
  //
  // For ARM:
  // <4>[ 3498.731164] [<c0057220>] ? (function_name+0x20/0x2c) from
  // [<c018062c>] (foo_bar+0xdc/0x1bc) (arm32 older)
  // <4>[  263.956936]  lkdtm_do_action+0x24/0x40 (arm64 / arm32 newer)
  //
  // For MIPS:
  // <5>[ 3378.656000] [<804010f0>] lkdtm_do_action+0x68/0x3f8
  //
  // For X86:
  // <4>[ 6066.849504]  [<7937bcee>] ? function_name+0x66/0x6c
  //
  pcrecpp::RE stack_entry_re(
      std::string(kTimestampRegex) +
      R"(\s+(?:\[<[[:xdigit:]]+>\])?)"  // Matches "  [<7937bcee>]" (if any)
      R"(([\s?(]+))"                    // Matches " ? (" (ARM) or " ? " (X86)
      R"(([^\+ )]+))");                 // Matches until delimiter reached
  std::string line;
  std::string hashable;
  std::string previous_hashable;
  bool is_watchdog = false;

  *hash = 0;
  *last_stack_timestamp = 0;

  // Find the last and second-to-last stack traces.  The latter is used when
  // the panic is from a watchdog timeout.
  while (line_re.FindAndConsume(&kernel_dump, &line)) {
    std::string certainty;
    std::string function_name;
    if (stack_trace_start_re.PartialMatch(line, last_stack_timestamp)) {
      previous_hashable = hashable;
      hashable.clear();
      is_watchdog = false;
    } else if (stack_entry_re.PartialMatch(line, last_stack_timestamp,
                                           &certainty, &function_name)) {
      bool is_certain = certainty.find('?') == std::string::npos;
      // Do not include any uncertain (prefixed by '?') frames in our hash.
      if (!is_certain)
        continue;
      if (!hashable.empty())
        hashable.append("|");
      if (function_name == "watchdog_timer_fn" || function_name == "watchdog") {
        is_watchdog = true;
      }
      hashable.append(function_name);
    }
  }

  // If the last stack trace contains a watchdog function we assume the panic
  // is from the watchdog timer, and we hash the previous stack trace rather
  // than the last one, assuming that the previous stack is that of the hung
  // thread.
  //
  // In addition, if the hashable is empty (meaning all frames are uncertain,
  // for whatever reason) also use the previous frame, as it cannot be any
  // worse.
  if (is_watchdog || hashable.empty()) {
    hashable = previous_hashable;
  }

  *hash = HashString(StringPiece(hashable));
  *is_watchdog_crash = is_watchdog;
}

// static
KernelCollector::ArchKind KernelCollector::GetCompilerArch() {
#if defined(COMPILER_GCC) && defined(ARCH_CPU_ARM_FAMILY)
  return kArchArm;
#elif defined(COMPILER_GCC) && defined(ARCH_CPU_MIPS_FAMILY)
  return kArchMips;
#elif defined(COMPILER_GCC) && defined(ARCH_CPU_X86_64)
  return kArchX86_64;
#elif defined(COMPILER_GCC) && defined(ARCH_CPU_X86_FAMILY)
  return kArchX86;
#else
  return kArchUnknown;
#endif
}

bool KernelCollector::FindCrashingFunction(pcrecpp::StringPiece kernel_dump,
                                           float stack_trace_timestamp,
                                           std::string* crashing_function) {
  float timestamp = 0;

  // Use the correct regex for this architecture.
  pcrecpp::RE eip_re(std::string(kTimestampRegex) + kPCRegex[arch_],
                     pcrecpp::MULTILINE());

  while (eip_re.FindAndConsume(&kernel_dump, &timestamp, crashing_function)) {
  }
  if (timestamp == 0) {
    LOG(WARNING) << "Found no crashing function";
    return false;
  }
  if (stack_trace_timestamp != 0 &&
      abs(static_cast<int>(stack_trace_timestamp - timestamp)) >
          kSignatureTimestampWindow) {
    LOG(WARNING) << "Found crashing function but not within window";
    return false;
  }
  return true;
}

bool KernelCollector::FindPanicMessage(pcrecpp::StringPiece kernel_dump,
                                       std::string* panic_message) {
  // Match lines such as the following and grab out "Fatal exception"
  // <0>[  342.841135] Kernel panic - not syncing: Fatal exception
  pcrecpp::RE kernel_panic_re(
      std::string(kTimestampRegex) + " Kernel panic[^\\:]*\\:\\s*(.*)",
      pcrecpp::MULTILINE());
  float timestamp = 0;
  while (
      kernel_panic_re.FindAndConsume(&kernel_dump, &timestamp, panic_message)) {
  }
  if (timestamp == 0) {
    LOG(WARNING) << "Found no panic message";
    return false;
  }
  return true;
}

std::string KernelCollector::ComputeKernelStackSignature(
    const std::string& kernel_dump) {
  unsigned stack_hash = 0;
  float last_stack_timestamp = 0;
  std::string human_string;
  bool is_watchdog_crash;

  ProcessStackTrace(kernel_dump, &stack_hash, &last_stack_timestamp,
                    &is_watchdog_crash);

  if (!FindCrashingFunction(kernel_dump, last_stack_timestamp, &human_string)) {
    if (!FindPanicMessage(kernel_dump, &human_string)) {
      LOG(WARNING) << "Found no human readable string, using empty string";
      human_string.clear();
    }
  }

  if (human_string.empty() && stack_hash == 0) {
    LOG(WARNING) << "Cannot find a stack or a human readable string";
    return kDefaultKernelStackSignature;
  }

  human_string = human_string.substr(0, kMaxHumanStringLength);
  return StringPrintf("%s-%s%s-%08X", kKernelExecName,
                      (is_watchdog_crash ? "(HANG)-" : ""),
                      human_string.c_str(), stack_hash);
}

std::string KernelCollector::BiosCrashSignature(const std::string& dump) {
  const char* type = "";

  if (pcrecpp::RE("PANIC in EL3").PartialMatch(dump))
    type = "PANIC";
  else if (pcrecpp::RE("Unhandled Exception in EL3").PartialMatch(dump))
    type = "EXCPT";
  else if (pcrecpp::RE("Unhandled Interrupt Exception in").PartialMatch(dump))
    type = "INTR";

  std::string elr;
  pcrecpp::RE("x30 =\\s+(0x[0-9a-fA-F]+)").PartialMatch(dump, &elr);

  return StringPrintf("bios-(%s)-%s", type, elr.c_str());
}

// Watchdog reboots leave no stack trace. Generate a poor man's signature out
// of the last log line instead (minus the timestamp ended by ']').
std::string KernelCollector::WatchdogSignature(
    const std::string& console_ramoops) {
  StringPiece line(console_ramoops);
  line = line.substr(line.rfind("] ") + 2);
  size_t end = std::min(line.find("\n"), kMaxHumanStringLength) - 1;
  return StringPrintf("%s-(WATCHDOG)-%s-%08X", kKernelExecName,
                      line.substr(0, end).as_string().c_str(),
                      HashString(line));
}

bool KernelCollector::Collect() {
  bool found_efi_crash = CollectEfiCrash();
  return (CollectRamoopsCrash() || found_efi_crash);
}

// Returns file path for corresponding efi crash part.
base::FilePath KernelCollector::EfiCrash::GetFilePath(uint32_t part) const {
  return collector_.dump_path_.Append(
      StringPrintf("%s-%s-%" PRIu64, kDumpRecordDmesgName, kDumpDriverEfiName,
                   GetIdForPart(part)));
}

// Get type of crash.
// Stack traces could be generated and written to efi pstore during kernel oops,
// kernel warning or kernel panic. First line contains header of format:
// <crash_type>#<crash_count> Part#<part_number>
// <crash_type> indicates when stack trace was generated. e.g. Panic#1 Part#1.
bool KernelCollector::EfiCrash::GetType(std::string* crash_type) const {
  std::string dump;
  if (base::ReadFileToString(GetFilePath(1), &dump)) {
    size_t pos = dump.find('#');
    if (pos != std::string::npos) {
      crash_type->append(dump, 0, pos);
      return true;
    }
  }
  return false;
}

// Loads efi crash to given string.
// Returns true iff all parts of crashes are copied to contents.
// In case of failure string contents might be modified.
bool KernelCollector::EfiCrash::Load(std::string* contents) const {
  // Part0 is never generated by efi driver.
  // Part number is descending, so Part1 contains last 1KiB (EFI
  // varaible size) of kmsg buffer, Part2 contains the second to last 1KiB,
  // etc....
  for (uint32_t part = max_part_; part > 0; part--) {
    std::string dump;
    if (!base::ReadFileToString(GetFilePath(part), &dump)) {
      PLOG(ERROR) << "Unable to open->read file for crash:" << id_
                  << " part: " << part;
      return false;
    }
    // Strip first line since it contains header e.g. Panic#1 Part#1.
    contents->append(dump, dump.find('\n') + 1, std::string::npos);
  }
  return true;
}

// Removes efi crash represented by efi variables from pstore.
void KernelCollector::EfiCrash::Remove() const {
  // Delete efi crash.
  // Part can be deleted in any order, start from Part1 since Part0 is
  // never generated.
  for (uint32_t part = 1; part <= max_part_; part++) {
    base::DeleteFile(GetFilePath(part), false);
  }
}

// Find number of efi crashes at /sys/fs/pstore and returns vector of EfiCrash.
std::vector<KernelCollector::EfiCrash> KernelCollector::FindEfiCrashes() const {
  std::vector<EfiCrash> efi_crashes;
  const base::FilePath pstore_dir(dump_path_);
  if (!base::PathExists(pstore_dir)) {
    return efi_crashes;
  }

  // Scan /sys/fs/pstore/.
  std::string efi_crash_pattern =
      StringPrintf("%s-%s-*", kDumpRecordDmesgName, kDumpDriverEfiName);
  base::FileEnumerator efi_file_iter(
      pstore_dir, false, base::FileEnumerator::FILES, efi_crash_pattern);

  for (auto efi_file = efi_file_iter.Next(); !efi_file.empty();
       efi_file = efi_file_iter.Next()) {
    uint64_t crash_id;
    if (!base::StringToUint64(
            efi_file.BaseName().value().substr(efi_crash_pattern.length() - 1),
            &crash_id)) {
      // This should not ever happen.
      LOG(ERROR) << "Failed to parse efi file name:"
                 << efi_file.BaseName().value();
      continue;
    }

    const uint64_t keyed_crash_id = EfiCrash::GetIdForPart(crash_id, 1);
    std::vector<EfiCrash>::iterator it =
        std::find_if(efi_crashes.begin(), efi_crashes.end(),
                     [keyed_crash_id](const EfiCrash& efi_crash) -> bool {
                       return efi_crash.GetId() == keyed_crash_id;
                     });
    if (it != efi_crashes.end()) {
      // Update part number if its greater.
      it->UpdateMaxPart(crash_id);

    } else {
      // New crash detected.
      EfiCrash efi_crash(keyed_crash_id, *this);
      efi_crash.UpdateMaxPart(crash_id);
      efi_crashes.push_back(efi_crash);
    }
  }
  return efi_crashes;
}

// Stores crash pointed by kernel_dump to crash directory. This will be later
// sent to backend from crash directory by crash_sender.
bool KernelCollector::HandleCrash(const std::string& kernel_dump,
                                  const std::string& bios_dump,
                                  const std::string& signature) {
  FilePath root_crash_directory;
  std::string reason = "handling";
  bool feedback = true;
  if (util::IsDeveloperImage()) {
    reason = "developer build - always dumping";
    feedback = true;
  } else if (!is_feedback_allowed_function_()) {
    reason = "ignoring - no consent";
    feedback = false;
  }

  LOG(INFO) << "Received prior crash notification from "
            << "kernel (signature " << signature << ") (" << reason << ")";

  if (feedback) {
    if (!GetCreatedCrashDirectoryByEuid(kRootUid, &root_crash_directory,
                                        nullptr)) {
      return true;
    }

    std::string dump_basename =
        FormatDumpBasename(kKernelExecName, time(nullptr), kKernelPid);
    FilePath kernel_crash_path = root_crash_directory.Append(
        StringPrintf("%s.kcrash", dump_basename.c_str()));
    FilePath bios_dump_path = root_crash_directory.Append(
        StringPrintf("%s.%s", dump_basename.c_str(), kBiosDumpName));
    FilePath log_path = root_crash_directory.Append(
        StringPrintf("%s.log", dump_basename.c_str()));

    // We must use WriteNewFile instead of base::WriteFile as we
    // do not want to write with root access to a symlink that an attacker
    // might have created.
    if (WriteNewFile(kernel_crash_path, kernel_dump.data(),
                     kernel_dump.length()) !=
        static_cast<int>(kernel_dump.length())) {
      LOG(INFO) << "Failed to write kernel dump to "
                << kernel_crash_path.value().c_str();
      return true;
    }
    if (!bios_dump.empty()) {
      if (WriteNewFile(bios_dump_path, bios_dump.data(), bios_dump.length()) !=
          static_cast<int>(bios_dump.length())) {
        PLOG(WARNING) << "Failed to write BIOS log to "
                      << bios_dump_path.value() << " (ignoring)";
      } else {
        AddCrashMetaUploadFile(kBiosDumpName,
                               bios_dump_path.BaseName().value());
        LOG(INFO) << "Stored BIOS log to " << bios_dump_path.value();
      }
    }

    AddCrashMetaData(kKernelSignatureKey, signature);

    // Collect additional logs if one is specified in the config file.
    if (GetLogContents(log_config_path_, kKernelExecName, log_path)) {
      AddCrashMetaUploadFile("log", log_path.BaseName().value());
    }

    FinishCrash(root_crash_directory.Append(
                    StringPrintf("%s.meta", dump_basename.c_str())),
                kKernelExecName, kernel_crash_path.BaseName().value());

    LOG(INFO) << "Stored kcrash to " << kernel_crash_path.value();
  }

  return true;
}

// CollectEfiCrash looks at /sys/fs/pstore and extracts crashes written via
// efi-pstore.
bool KernelCollector::CollectEfiCrash() {
  // List of efi crashes.
  std::vector<KernelCollector::EfiCrash> efi_crashes = FindEfiCrashes();

  LOG(INFO) << "Found " << efi_crashes.size()
            << " kernel crashes in efi-pstore.";
  // Now read each crash in buffer and cleanup pstore.
  std::vector<EfiCrash>::const_iterator efi_crash;
  for (efi_crash = efi_crashes.begin(); efi_crash != efi_crashes.end();
       ++efi_crash) {
    LOG(INFO) << "Generating kernel efi crash id:" << efi_crash->GetId();

    std::string crash_type, crash;
    if (efi_crash->GetType(&crash_type)) {
      if (crash_type == "Panic" && efi_crash->Load(&crash)) {
        LOG(INFO) << "Reporting kernel efi crash id:" << efi_crash->GetId()
                  << " type:" << crash_type;
        StripSensitiveData(&crash);
        if (!crash.empty()) {
          if (!HandleCrash(crash, std::string(),
                           ComputeKernelStackSignature(crash))) {
            LOG(ERROR) << "Failed to handle kernel efi crash id:"
                       << efi_crash->GetId();
          }
        }
      } else {
        LOG(WARNING) << "Ignoring kernel efi crash id:" << efi_crash->GetId()
                     << " type:" << crash_type;
      }
    }
    // Remove efi-pstore files corresponding to crash.
    efi_crash->Remove();
  }
  return !efi_crashes.empty();
}

bool KernelCollector::CollectRamoopsCrash() {
  std::string bios_dump;
  std::string kernel_dump;
  std::string signature;

  LoadLastBootBiosLog(&bios_dump);
  if (LoadParameters() && LoadPreservedDump(&kernel_dump)) {
    signature = ComputeKernelStackSignature(kernel_dump);
  } else {
    LoadConsoleRamoops(&kernel_dump);
    if (LastRebootWasBiosCrash(bios_dump))
      signature = BiosCrashSignature(bios_dump);
    else if (LastRebootWasWatchdog())
      signature = WatchdogSignature(kernel_dump);
    else
      return false;
  }
  StripSensitiveData(&bios_dump);
  StripSensitiveData(&kernel_dump);
  if (kernel_dump.empty() && bios_dump.empty()) {
    return false;
  }
  return HandleCrash(kernel_dump, bios_dump, signature);
}
