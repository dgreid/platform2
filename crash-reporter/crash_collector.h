// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The crash collector is a base class for all collectors to use. It implements
// common functionality, such as writing out .meta files.
// It is not a collector in and of itself.

#ifndef CRASH_REPORTER_CRASH_COLLECTOR_H_
#define CRASH_REPORTER_CRASH_COLLECTOR_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/optional.h>
#include <base/time/clock.h>
#include <base/time/time.h>
#include <brillo/dbus/file_descriptor.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <session_manager/dbus-proxies.h>
#include <debugd/dbus-proxies.h>
#include <metrics/metrics_library.h>

constexpr mode_t kSystemCrashFilesMode = 0660;

// User crash collector.
class CrashCollector {
 public:
  typedef bool (*IsFeedbackAllowedFunction)();

  enum CrashDirectorySelectionMethod {
    // Force reports to be stored in the user crash directory, even if we are
    // not running as the "chronos" user.
    kAlwaysUseUserCrashDirectory,
    // Use the normal crash directory selection process: Store in the user crash
    // directory if running as the "chronos" user, otherwise store in the system
    // crash directory.
    kUseNormalCrashDirectorySelectionMethod
  };

  enum CrashSendingMode {
    // Use the normal crash sending mode: Write crash files out to disk, and
    // assume crash_sender will be along later to send them out.
    kNormalCrashSendMode,
    // Use a special mode suitable when we are in a login-crash-loop. where
    // Chrome keeps crashing right after login, and we're about to log the user
    // out because we can't get into a good logged-in state. Write the crash
    // files into special in-memory locations, since the normal user crash
    // directory is in the cryptohome which will be locked out momentarily, and
    // send those in-memory files over to debugd for immediate upload, since
    // they are in volatile storage and the user may turn off their machine in
    // frustration shortly.
    kCrashLoopSendingMode
  };

  enum ErrorType {
    kErrorNone,
    kErrorSystemIssue,
    kErrorReadCoreData,
    kErrorUnusableProcFiles,
    kErrorInvalidCoreFile,
    kErrorUnsupported32BitCoreFile,
    kErrorCore2MinidumpConversion,
  };

  explicit CrashCollector(const std::string& collector_name,
                          const std::string& tag = "");

  explicit CrashCollector(
      const std::string& collector_name,
      CrashDirectorySelectionMethod crash_directory_selection_method,
      CrashSendingMode crash_sending_mode,
      const std::string& tag = "");
  CrashCollector(const CrashCollector&) = delete;
  CrashCollector& operator=(const CrashCollector&) = delete;

  virtual ~CrashCollector();

  void set_lsb_release_for_test(const base::FilePath& lsb_release) {
    lsb_release_ = lsb_release;
  }

  // For testing, set the directory always returned by
  // GetCreatedCrashDirectoryByEuid.
  void set_crash_directory_for_test(const base::FilePath& forced_directory) {
    forced_crash_directory_ = forced_directory;
  }

  // For testing, set the directory where cached files are stored instead of
  // kCrashReporterStatePath.
  void set_reporter_state_directory_for_test(
      const base::FilePath& forced_directory) {
    crash_reporter_state_path_ = forced_directory;
  }

  void set_metrics_library_for_test(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
    metrics_lib_ = std::move(metrics_lib);
  }

  // For testing, set the log config file path instead of kDefaultLogConfig.
  void set_log_config_path(const std::string& path) {
    log_config_path_ = base::FilePath(path);
  }

  // For testing, set the clock to use to get the report timestamp.
  void set_test_clock(std::unique_ptr<base::Clock> test_clock) {
    test_clock_ = std::move(test_clock);
  }

  // For testing, use to set the kernel version rather than relying on uname.
  void set_test_kernel_info(const std::string& kernel_name,
                            const std::string& kernel_version) {
    test_kernel_name_ = kernel_name;
    test_kernel_version_ = kernel_version;
  }

  // For testing, return the in-memory files generated when in
  // kCrashLoopSendingMode. Since in_memory_files_ is a move-only type, this
  // clears the in_memory_files_ member variable.
  std::vector<std::tuple<std::string, brillo::dbus_utils::FileDescriptor>>
  get_in_memory_files_for_test() {
    return std::move(in_memory_files_);
  }

  // Initialize the crash collector for detection of crashes, given a
  // metrics collection enabled oracle.
  void Initialize(IsFeedbackAllowedFunction is_metrics_allowed, bool early);

  // Return the number of bytes successfully written by all calls to
  // WriteNewFile() and WriteNewCompressedFile() so far. For
  // WriteNewCompressedFile(), the count is of bytes on disk, after compression.
  off_t get_bytes_written() const { return bytes_written_; }

  // Initialize the system crash paths.
  static bool InitializeSystemCrashDirectories(bool early);

  // Initialize metrics path. Returns true if flag directory is created.
  static bool InitializeSystemMetricsDirectories();

  // Add non-standard meta data to the crash metadata file.  Call
  // before calling FinishCrash.  Key must not contain "=" or "\n" characters.
  // Value must not contain "\n" characters.
  void AddCrashMetaData(const std::string& key, const std::string& value);

 protected:
  friend class CrashCollectorTest;
  FRIEND_TEST(ArcContextTest, GetAndroidVersion);
  FRIEND_TEST(ChromeCollectorTest, HandleCrash);
  FRIEND_TEST(CrashCollectorTest, CrashLoopModeCreatesInMemoryCompressedFiles);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_CrashLoopModeCreatesInMemoryCompressedFiles);
  FRIEND_TEST(CrashCollectorTest, CrashLoopModeCreatesInMemoryFiles);
  FRIEND_TEST(CrashCollectorTest, DISABLED_CrashLoopModeCreatesInMemoryFiles);
  FRIEND_TEST(CrashCollectorTest, CrashLoopModeCreatesMultipleInMemoryFiles);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_CrashLoopModeCreatesMultipleInMemoryFiles);
  FRIEND_TEST(CrashCollectorTest,
              CrashLoopModeWillNotCreateDuplicateCompressedFileNames);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_CrashLoopModeWillNotCreateDuplicateCompressedFileNames);
  FRIEND_TEST(CrashCollectorTest, CrashLoopModeWillNotCreateDuplicateFileNames);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_CrashLoopModeWillNotCreateDuplicateFileNames);
  FRIEND_TEST(CrashCollectorTest, CheckHasCapacityCorrectBasename);
  FRIEND_TEST(CrashCollectorTest, CheckHasCapacityStrangeNames);
  FRIEND_TEST(CrashCollectorTest, CheckHasCapacityUsual);
  FRIEND_TEST(CrashCollectorTest, CreateDirectoryWithSettingsMode);
  FRIEND_TEST(CrashCollectorTest, CreateDirectoryWithSettingsNonDir);
  FRIEND_TEST(CrashCollectorTest, CreateDirectoryWithSettingsSubdir);
  FRIEND_TEST(CrashCollectorTest, CreateDirectoryWithSettingsSymlinks);
  FRIEND_TEST(CrashCollectorTest,
              CreateDirectoryWithSettings_FixPermissionsShallow);
  FRIEND_TEST(CrashCollectorTest,
              CreateDirectoryWithSettings_FixPermissionsRecursive);
  FRIEND_TEST(CrashCollectorTest,
              RunAsRoot_CreateDirectoryWithSettings_FixOwners);
  FRIEND_TEST(CrashCollectorTest,
              CreateDirectoryWithSettings_FixSubdirPermissions);
  FRIEND_TEST(CrashCollectorTest, FormatDumpBasename);
  FRIEND_TEST(CrashCollectorTest, GetCrashDirectoryInfo);
  FRIEND_TEST(CrashCollectorTest, GetCrashDirectoryInfoLoggedOut);
  FRIEND_TEST(CrashCollectorTest, GetCrashPath);
  FRIEND_TEST(CrashCollectorTest, GetLogContents);
  FRIEND_TEST(CrashCollectorTest, GetMultipleLogContents);
  FRIEND_TEST(CrashCollectorTest, GetProcessTree);
  FRIEND_TEST(CrashCollectorTest, GetUptime);
  FRIEND_TEST(CrashCollectorTest, Initialize);
  FRIEND_TEST(CrashCollectorParameterizedTest, MetaData);
  FRIEND_TEST(CrashCollectorTest, ErrorCollectionMetaData);
  FRIEND_TEST(CrashCollectorTest, MetaDataDoesntCreateSymlink);
  FRIEND_TEST(CrashCollectorTest, MetaDataDoesntOverwriteSymlink);
  FRIEND_TEST(CrashCollectorTest, CollectionLogsToUMA);
  FRIEND_TEST(CrashCollectorTest, ParseProcessTicksFromStat);
  FRIEND_TEST(CrashCollectorTest, Sanitize);
  FRIEND_TEST(CrashCollectorTest, StripMacAddressesBasic);
  FRIEND_TEST(CrashCollectorTest, StripMacAddressesBulk);
  FRIEND_TEST(CrashCollectorTest, StripSensitiveDataSample);
  FRIEND_TEST(CrashCollectorTest, StripEmailAddresses);
  FRIEND_TEST(CrashCollectorTest, StripSerialNumbers);
  FRIEND_TEST(CrashCollectorTest, RemoveNewFileFailsOnNonExistantFiles);
  FRIEND_TEST(CrashCollectorTest,
              RemoveNewFileFailsOnNonExistantFilesInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest, RemoveNewFileRemovesCompressedFiles);
  FRIEND_TEST(CrashCollectorTest,
              RemoveNewFileRemovesCompressedFilesInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_RemoveNewFileRemovesCompressedFilesInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest,
              RemoveNewFileRemovesCorrectFileInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_RemoveNewFileRemovesCorrectFileInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest, RemoveNewFileRemovesNormalFiles);
  FRIEND_TEST(CrashCollectorTest,
              RemoveNewFileRemovesNormalFilesInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest,
              DISABLED_RemoveNewFileRemovesNormalFilesInCrashLoopMode);
  FRIEND_TEST(CrashCollectorTest, TruncatedLog);
  FRIEND_TEST(CrashCollectorTest, WriteNewFile);
  FRIEND_TEST(CrashCollectorTest, WriteNewCompressedFile);
  FRIEND_TEST(CrashCollectorTest, WriteNewCompressedFileFailsIfFileExists);

  // Default value if OS version/description cannot be determined.
  static const char* const kUnknownValue;

  // Set maximum enqueued crashes in a crash directory.
  static const int kMaxCrashDirectorySize;

  // UID for root account.
  static const uid_t kRootUid;

  // Set up D-Bus.
  virtual void SetUpDBus();

  // Creates a new file and returns a file descriptor to it.
  base::ScopedFD GetNewFileHandle(const base::FilePath& filename);

  // Writes |data| of |size| to |filename|, which must be a new file.
  // If the file already exists or writing fails, return a negative value.
  // Otherwise returns the number of bytes written.
  int WriteNewFile(const base::FilePath& filename, const char* data, int size);

  // Writes |data| of |size| to |filename|, which must be a new file ending in
  // ".gz". File will be a gzip-compressed file. Returns true on success,
  // false on failure.
  bool WriteNewCompressedFile(const base::FilePath& filename,
                              const char* data,
                              size_t size);

  // Deletes a file created by WriteNewFile() or WriteNewCompressedFile(). Also
  // decrements get_bytes_written() by the file size. Needed because
  // base::DeleteFile() doesn't work on files created when in
  // kCrashLoopSendingMode.
  bool RemoveNewFile(const base::FilePath& filename);

  // Return a filename that has only [a-z0-1_] characters by mapping
  // all others into '_'.
  std::string Sanitize(const std::string& name);

  // Strip any data that the user might not want sent up to the crash server.
  // |contents| is modified in-place.
  void StripSensitiveData(std::string* contents);
  void StripMacAddresses(std::string* contents);
  void StripEmailAddresses(std::string* contents);
  void StripSerialNumbers(std::string* contents);

  bool GetUserCrashDirectories(std::vector<base::FilePath>* directories,
                               bool use_non_chronos_cryptohome);
  base::FilePath GetUserCrashDirectory(bool use_non_chronos_cryptohome);
  base::Optional<base::FilePath> GetCrashDirectoryInfo(
      uid_t process_euid,
      uid_t default_user_id,
      bool use_non_chronos_cryptohome,
      mode_t* mode,
      uid_t* directory_owner,
      gid_t* directory_group);

  // Determines the crash directory for given euid, and creates the directory if
  // necessary with appropriate permissions.  If |out_of_capacity| is not
  // nullptr, it is set to indicate if the call failed due to not having
  // capacity in the crash directory. Returns true whether or not directory
  // needed to be created, false on any failure.  If the crash directory is at
  // capacity, returns false.  If |use_non_chronos_cryptohome| is set, use the
  // new crash directory under /run/daemon-store/crash/<user-hash>.
  bool GetCreatedCrashDirectoryByEuid(uid_t euid,
                                      base::FilePath* crash_file_path,
                                      bool* out_of_capacity,
                                      bool use_non_chronos_cryptohome = false);

  // Create a directory using the specified mode/user/group, and make sure it
  // is actually a directory with the specified permissions.
  // If |files_mode| is set, the call will recursively change permissions on
  // |dir| such that:
  //   * any directories under it in the file heirarchy have mode |mode|
  //   * any files under it in the heirarchy have mode |files_mode|
  //   * all files AND directories under it are owned by owner:group
  static bool CreateDirectoryWithSettings(const base::FilePath& dir,
                                          mode_t mode,
                                          uid_t owner,
                                          gid_t group,
                                          int* dir_fd,
                                          mode_t files_mode = 0);

  // Format crash name based on components.
  std::string FormatDumpBasename(const std::string& exec_name,
                                 time_t timestamp,
                                 pid_t pid);

  // Create a file path to a file in |crash_directory| with the given
  // |basename| and |extension|.
  base::FilePath GetCrashPath(const base::FilePath& crash_directory,
                              const std::string& basename,
                              const std::string& extension);

  // Returns the path /proc/<pid>.
  static base::FilePath GetProcessPath(pid_t pid);

  static bool GetUptime(base::TimeDelta* uptime);
  static bool GetUptimeAtProcessStart(pid_t pid, base::TimeDelta* uptime);

  virtual bool GetExecutableBaseNameFromPid(pid_t pid, std::string* base_name);

  // Check given crash directory still has remaining capacity for another
  // crash.
  bool CheckHasCapacity(const base::FilePath& crash_directory);
  bool CheckHasCapacity(const base::FilePath& crash_directory,
                        const std::string& display_path);

  // Write a log applicable to |exec_name| to |output_file| based on the
  // log configuration file at |config_path|. If |output_file| ends in .gz, it
  // will be compressed in gzip format, otherwise it will be plaintext.
  bool GetLogContents(const base::FilePath& config_path,
                      const std::string& exec_name,
                      const base::FilePath& output_file);

  // Write logs applicable to |exec_names| to |output_file| based on the
  // log configuration file at |config_path|. If |output_file| ends in .gz, it
  // will be compressed in gzip format, otherwise it will be plaintext.
  // This function returns false only if all of the log commands fail to run.
  bool GetMultipleLogContents(const base::FilePath& config_path,
                              const std::vector<std::string>& exec_names,
                              const base::FilePath& output_file);

  // Write details about the process tree of |pid| to |output_file|.
  bool GetProcessTree(pid_t pid, const base::FilePath& output_file);

  // Add a file to be uploaded to the crash reporter server. The file must
  // persist until the crash report is sent; ideally it should live in the same
  // place as the .meta file, so it can be cleaned up automatically.
  void AddCrashMetaUploadFile(const std::string& key, const std::string& path);

  // Add non-standard meta data to the crash metadata file.
  // Data added though this call will be uploaded to the crash reporter server,
  // appearing as a form field. Virtual for testing.
  virtual void AddCrashMetaUploadData(const std::string& key,
                                      const std::string& value);

  // Like AddCrashMetaUploadData, but loads the value from the file at |path|.
  // The file is not uploaded as an attachment, unlike AddCrashMetaUploadFile.
  void AddCrashMetaUploadText(const std::string& key, const std::string& path);

  // Gets the corresponding value for |key| from the lsb-release file.
  std::string GetLsbReleaseValue(const std::string& key) const;

  // Returns the OS version written to the metadata file.
  virtual std::string GetOsVersion() const;

  // Returns the OS milestone written to the metadata file.
  virtual std::string GetOsMilestone() const;

  // Returns the OS description written to the metadata file.
  virtual std::string GetOsDescription() const;

  // Returns the kernel name from uname (e.g. "Linux").
  std::string GetKernelName() const;

  // Returns the uname string formatted as
  // 3.8.11 #1 SMP Wed Aug 22 02:18:30 PDT 2018
  std::string GetKernelVersion() const;

  // Called after all files have been written and we want to send out this
  // crash. Write a file of metadata about crash and, if in crash-loop mode,
  // sends the UploadSingleCrash message to debugd. Not called if we failed to
  // make a good crash report.
  virtual void FinishCrash(const base::FilePath& meta_path,
                           const std::string& exec_name,
                           const std::string& payload_name);

  // Returns true if chrome crashes should be handled.
  bool ShouldHandleChromeCrashes();

  IsFeedbackAllowedFunction is_feedback_allowed_function_;
  std::string extra_metadata_;
  const std::string collector_name_;
  base::FilePath forced_crash_directory_;
  base::FilePath lsb_release_;
  base::FilePath system_crash_path_;
  base::FilePath crash_reporter_state_path_;
  base::FilePath log_config_path_;
  size_t max_log_size_;
  std::unique_ptr<base::Clock> test_clock_;
  std::string test_kernel_name_;
  std::string test_kernel_version_;

  scoped_refptr<dbus::Bus> bus_;

  // D-Bus proxy for session manager interface.
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;

  // D-Bus proxy for debugd interface.
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;

  // If kCrashLoopSendingMode, reports are stored in memory and sent over DBus
  // to debugd when finished. Otherwise, we store the crash reports on disk and
  // rely on crash_sender to later pick it up and send it.
  const CrashSendingMode crash_sending_mode_;

  // Hash a string to a number.  We define our own hash function to not
  // be dependent on a C++ library that might change.
  static unsigned HashString(base::StringPiece input);

  // Record information about a crash collector failure in a new crash report.
  // Clears metadata for existing report.
  // orig_exec: Name of the executable in which we were processing a crash when
  // the failure happened.
  void EnqueueCollectionErrorLog(ErrorType error_type,
                                 const std::string& orig_exec);

  // Logs a |message| detailing a crash, along with the |reason| for which the
  // collector handled or ignored it.
  void LogCrash(const std::string& message, const std::string& reason) const;

 private:
  static bool ParseProcessTicksFromStat(base::StringPiece stat,
                                        uint64_t* ticks);

  // Should reports always be stored in the user crash directory, or can they be
  // stored in the system directory if we are not running as "chronos"?
  const CrashDirectorySelectionMethod crash_directory_selection_method_;

  // True when FinishCrash has been called. Once true, no new files should be
  // created.
  bool is_finished_;

  // If crash_loop_mode_ is true, all files are collected in here instead of
  // being written to disk. The first element of the tuple is the base filename,
  // the second is a memfd_create file descriptor with the file contents.
  std::vector<std::tuple<std::string, brillo::dbus_utils::FileDescriptor>>
      in_memory_files_;

  // Number of bytes successfully written by all calls to WriteNewFile() and
  // WriteNewCompressedFile() so far. For WriteNewCompressedFile(), the count is
  // of bytes on disk, after compression.
  off_t bytes_written_;

  // Returns true if there is already a file in in_memory_files_ with
  // filename.BaseName().
  bool InMemoryFileExists(const base::FilePath& filename) const;

  // Returns an error type signature for a given |error_type| value,
  // which is reported to the crash server along with the
  // crash_reporter-user-collection signature.
  std::string GetErrorTypeSignature(ErrorType error_type) const;

  // Prepended to log messages to differentiate between collectors.
  const std::string tag_;

  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
};

#endif  // CRASH_REPORTER_CRASH_COLLECTOR_H_
