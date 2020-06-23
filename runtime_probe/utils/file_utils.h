// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_FILE_UTILS_H_
#define RUNTIME_PROBE_UTILS_FILE_UTILS_H_

#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

namespace runtime_probe {

// Maps files listed in |keys| and |optional_keys| under |dir_path| into key
// value pairs.
//
// If |KeyType| is |std::string|, the key will be same as file name; if
// |KeyType| is |std::pair<std::string, std::string>|, the first item will be
// key name and the second item will be file name.
//
// |keys| represents the set of must have, if any |keys| is missed in the
// |dir_path|, an empty dictionary will be returned.
template <typename KeyType>
base::Optional<base::Value> MapFilesToDict(
    const base::FilePath& dir_path,
    const std::vector<KeyType>& keys,
    const std::vector<KeyType>& optional_keys);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_FILE_UTILS_H_
