/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_METADATA_INSPECTOR_H_
#define CAMERA_HAL_ADAPTER_CAMERA_METADATA_INSPECTOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/sequence_checker.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <camera/camera_metadata.h>
#include <hardware/camera3.h>
#include <re2/re2.h>

namespace cros {

struct DiffData {
  std::string key;
  std::string old_val;
  std::string new_val;

  std::string FormatKey(int width);
  std::string FormatValue(int width);
};

class CameraMetadataInspector {
 public:
  // Holds the string representation of the entries of a metadata.
  using DataMap = std::map<std::string, std::string>;

  // The factory function that creates a CameraMetadataInspector from the
  // command line switches of the current process:
  // --metadata_inspector_output=<path/to/output/file>
  // --metadata_inspector_allowlist=<regex_filter> (optional)
  // --metadata_inspector_denylist=<regex_filter> (optional)
  // See the comments of |output_file_|, |allowlist_| and |denylist_| below for
  // more details.  Returns nullptr on error.
  static std::unique_ptr<CameraMetadataInspector> Create(
      int partial_result_count);

  // Disallow copy constructor and assign operator.
  CameraMetadataInspector(const CameraMetadataInspector&) = delete;
  CameraMetadataInspector& operator=(const CameraMetadataInspector&) = delete;

  // Inspect a capture request and dump the difference from the previous one
  // in |output_file_|.
  void InspectRequest(const camera3_capture_request_t* request);

  // Inspect a capture result and dump the difference from the previous one
  // in |output_file_|.  Partial results would be aggregated automatically, but
  // the caller needs to guarantee it's called on the same sequence.
  void InspectResult(const camera3_capture_result_t* result);

 private:
  enum class Kind { kRequest, kResult, kNumberOfKinds };

  // The private constructor used in the factory function.
  CameraMetadataInspector(int partial_result_count,
                          base::File output_file,
                          std::unique_ptr<RE2> allowlist,
                          std::unique_ptr<RE2> denylist,
                          std::unique_ptr<base::Thread> thread);

  // Writes and flushes |msg| to |output_file_|.
  void Write(base::StringPiece msg);

  // Returns true if |key| should be ignored according to |allowlist_| and
  // |denylist_|.
  bool ShouldIgnoreKey(const std::string& key);

  // Converts a metadata into the stringified DataMap.
  DataMap MapFromMetadata(const camera_metadata_t* metadata);

  // Compares two maps and returns a list of differences.
  std::vector<DiffData> Compare(const DataMap& old_map, const DataMap& new_map);

  // Compares the metadata with the previous one, and writes the formatted
  // difference into |output_file_|.
  void InspectOnThread(Kind kind,
                       const std::string& kind_tag,
                       int color,
                       base::Time time,
                       int frame_number,
                       camera_metadata_t* metadata);

  // How many sub-components a result will be composed of at most.
  int partial_result_count_;

  // The output file for the inspector.  Could be a special file such as
  // /dev/stdout.
  base::File output_file_;

  // If specified, only metadata with keys matching the regular expression
  // filter would be logged.
  std::unique_ptr<RE2> allowlist_;

  // If specified, only metadata with keys not matching the regular expression
  // filter would be logged.  Denylist could be used with allowlist, and only
  // keys (in allowlist && not in the denylist) would be logged.
  std::unique_ptr<RE2> denylist_;

  // The latest DataMap for each kind of metadata.
  DataMap latest_map[static_cast<size_t>(Kind::kNumberOfKinds)];

  // The aggregated capture result for all current partial results.  It's only
  // accessed in InspectResult() and guarded by |result_sequence_checker_|.
  android::CameraMetadata pending_result_;

  // Ensures InspectResult() are called on the same sequence.
  base::SequenceChecker result_sequence_checker_;

  // The real work such as InspectOnThread() is running on |thread_|, so the
  // capture flow won't be blocked by InspectRequest() and InspectResult().
  std::unique_ptr<base::Thread> thread_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_METADATA_INSPECTOR_H_
