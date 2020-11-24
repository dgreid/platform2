// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <algorithm>

#include <base/containers/adapters.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/strcat.h>

#include "cros-disks/quote.h"

namespace cros_disks {

const char MountOptions::kOptionBind[] = "bind";
const char MountOptions::kOptionDirSync[] = "dirsync";
const char MountOptions::kOptionFlush[] = "flush";
const char MountOptions::kOptionNoDev[] = "nodev";
const char MountOptions::kOptionNoExec[] = "noexec";
const char MountOptions::kOptionNoSuid[] = "nosuid";
const char MountOptions::kOptionNoSymFollow[] = "nosymfollow";
const char MountOptions::kOptionReadOnly[] = "ro";
const char MountOptions::kOptionReadWrite[] = "rw";
const char MountOptions::kOptionRemount[] = "remount";
const char MountOptions::kOptionSynchronous[] = "sync";
const char MountOptions::kOptionUtf8[] = "utf8";

namespace {
const char kOptionUidPrefix[] = "uid=";
const char kOptionGidPrefix[] = "gid=";
const char kOptionShortNamePrefix[] = "shortname=";
const char kOptionTimeOffsetPrefix[] = "time_offset=";

bool FindLastElementStartingWith(const std::vector<std::string>& container,
                                 base::StringPiece prefix,
                                 std::string* result) {
  for (const auto& element : base::Reversed(container)) {
    if (base::StartsWith(element, prefix, base::CompareCase::SENSITIVE)) {
      *result = element;
      return true;
    }
  }
  return false;
}

}  // namespace

MountOptions::MountOptions()
    : allow_exact_(
          {kOptionDirSync, kOptionFlush, kOptionSynchronous, kOptionUtf8}),
      allow_prefix_({kOptionShortNamePrefix, kOptionTimeOffsetPrefix}),
      enforced_options_({kOptionNoDev, kOptionNoExec, kOptionNoSuid}) {}

MountOptions::~MountOptions() = default;

void MountOptions::Initialize(const std::vector<std::string>& options,
                              bool set_user_and_group_id,
                              const std::string& default_user_id,
                              const std::string& default_group_id) {
  options_.clear();
  options_.reserve(options.size());

  bool option_read_only = false, option_read_write = false;
  bool option_remount = false;
  std::string option_user_id, option_group_id;

  for (const auto& option : options) {
    // Skip early if |option| contains a comma.
    if (option.find(",") != std::string::npos) {
      LOG(WARNING) << "Ignoring invalid mount option " << quote(option);
      continue;
    }

    if (option == kOptionReadOnly) {
      option_read_only = true;
    } else if (option == kOptionReadWrite) {
      option_read_write = true;
    } else if (option == kOptionRemount) {
      option_remount = true;
    } else if (base::StartsWith(option, kOptionUidPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      option_user_id = option;
    } else if (base::StartsWith(option, kOptionGidPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      option_group_id = option;
    } else if (base::Contains(enforced_options_, option)) {
      // We'll add these options unconditionally below.
      continue;
    } else if (base::Contains(allow_exact_, option)) {
      // Only add options in the allowlist.
      options_.push_back(option);
    } else if (std::find_if(allow_prefix_.begin(), allow_prefix_.end(),
                            [option](const auto& s) {
                              return base::StartsWith(
                                  option, s,
                                  base::CompareCase::INSENSITIVE_ASCII);
                            }) != allow_prefix_.end()) {
      // Only add options in the allowlist.
      options_.push_back(option);
    } else {
      // Never add unknown/non-allowed options.
      LOG(WARNING) << "Ignoring unsupported mount option " << quote(option);
    }
  }

  if (option_read_only || !option_read_write) {
    options_.push_back(kOptionReadOnly);
  } else {
    options_.push_back(kOptionReadWrite);
  }

  if (option_remount) {
    options_.push_back(kOptionRemount);
  }

  if (set_user_and_group_id) {
    if (!option_user_id.empty()) {
      options_.push_back(option_user_id);
    } else if (!default_user_id.empty()) {
      options_.push_back(kOptionUidPrefix + default_user_id);
    }

    if (!option_group_id.empty()) {
      options_.push_back(option_group_id);
    } else if (!default_group_id.empty()) {
      options_.push_back(kOptionGidPrefix + default_group_id);
    }
  }

  std::copy(enforced_options_.begin(), enforced_options_.end(),
            std::back_inserter(options_));
}

bool MountOptions::IsReadOnlyOptionSet() const {
  for (const std::string& option : base::Reversed(options_)) {
    if (option == kOptionReadOnly)
      return true;

    if (option == kOptionReadWrite)
      return false;
  }

  return true;
}

void MountOptions::SetReadOnlyOption() {
  std::replace(options_.begin(), options_.end(), kOptionReadWrite,
               kOptionReadOnly);
}

std::pair<MountOptions::Flags, std::string> MountOptions::ToMountFlagsAndData()
    const {
  Flags flags = MS_RDONLY;
  std::vector<std::string> data;
  data.reserve(options_.size());

  for (const auto& option : options_) {
    if (option == kOptionReadOnly) {
      flags |= MS_RDONLY;
    } else if (option == kOptionReadWrite) {
      flags &= ~static_cast<Flags>(MS_RDONLY);
    } else if (option == kOptionRemount) {
      flags |= MS_REMOUNT;
    } else if (option == kOptionBind) {
      flags |= MS_BIND;
    } else if (option == kOptionDirSync) {
      flags |= MS_DIRSYNC;
    } else if (option == kOptionNoDev) {
      flags |= MS_NODEV;
    } else if (option == kOptionNoExec) {
      flags |= MS_NOEXEC;
    } else if (option == kOptionNoSuid) {
      flags |= MS_NOSUID;
    } else if (option == kOptionSynchronous) {
      flags |= MS_SYNCHRONOUS;
    } else if (option == kOptionNoSymFollow) {
      flags |= MS_NOSYMFOLLOW;
      // Pass the nosymfollow option as both a flag and a string option for
      // compatibility across kernels.  The mount syscall ignores unknown flags,
      // so kernels that don't have MS_NOSYMFOLLOW will pick up nosymfollow from
      // the data parameter through the chromiumos LSM.  Kernels that do have
      // MS_NOSYMFOLLOW will pick up the same behavior directly from the flag;
      // our LSM ignores the string option in that case.
      //
      // TODO(b/152074038): Remove the string option once all devices have been
      // upreved to a kernel that supports MS_NOSYMFOLLOW (currently 5.4+).
      data.push_back(option);
    } else {
      data.push_back(option);
    }
  }
  return std::make_pair(flags, base::JoinString(data, ","));
}

std::string MountOptions::ToFuseMounterOptions() const {
  std::string result;

  const char* sep = "";
  for (const std::string& option : options_) {
    // Do not pass the nosymfollow option to the FUSE mounter.
    if (option == MountOptions::kOptionNoSymFollow)
      continue;

    result += sep;
    result += option;
    sep = ",";
  }

  if (result.empty())
    result = MountOptions::kOptionReadOnly;

  return result;
}

std::string MountOptions::ToString() const {
  return options_.empty() ? kOptionReadOnly : base::JoinString(options_, ",");
}

void MountOptions::AllowOption(const std::string& option) {
  allow_exact_.push_back(option);
}

void MountOptions::AllowOptionPrefix(const std::string& prefix) {
  allow_prefix_.push_back(prefix);
}

void MountOptions::EnforceOption(const std::string& option) {
  enforced_options_.push_back(option);
}

bool MountOptions::HasOption(const std::string& option) const {
  return base::Contains(options_, option);
}

bool IsReadOnlyMount(const std::vector<std::string>& options) {
  for (const auto& option : base::Reversed(options)) {
    if (option == MountOptions::kOptionReadOnly)
      return true;
    if (option == MountOptions::kOptionReadWrite)
      return false;
  }
  return false;
}

bool GetParamValue(const std::vector<std::string>& params,
                   base::StringPiece name,
                   std::string* value) {
  if (!FindLastElementStartingWith(params, base::StrCat({name, "="}), value)) {
    return false;
  }
  *value = value->substr(name.length() + 1);
  return true;
}

void SetParamValue(std::vector<std::string>* params,
                   base::StringPiece name,
                   base::StringPiece value) {
  params->emplace_back(base::StrCat({name, "=", value}));
}

}  // namespace cros_disks
