// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IMAGELOADER_MANIFEST_H_
#define IMAGELOADER_MANIFEST_H_

#include <map>
#include <string>
#include <vector>

#include <base/macros.h>
#include <brillo/brillo_export.h>

namespace imageloader {

// The supported file systems for images.
enum class BRILLO_EXPORT FileSystem { kExt4, kSquashFS };

// A class to parse and store imageloader.json manifest. See manifest.md.
class BRILLO_EXPORT Manifest {
 public:
  Manifest();
  Manifest(const Manifest&) = delete;
  Manifest& operator=(const Manifest&) = delete;

  // Parse the manifest raw string. Return true if successful.
  bool ParseManifest(const std::string& manifest_raw);

  // Getters for required manifest fields:
  int manifest_version() const { return manifest_version_; }
  const std::vector<uint8_t>& image_sha256() const { return image_sha256_; }
  const std::vector<uint8_t>& table_sha256() const { return table_sha256_; }
  const std::string& version() const { return version_; }

  // Getters for optional manifest fields:
  FileSystem fs_type() const { return fs_type_; }
  const std::string& id() const { return id_; }
  const std::string& package() const { return package_; }
  const std::string& name() const { return name_; }
  const std::string& image_type() const { return image_type_; }
  int64_t preallocated_size() const { return preallocated_size_; }
  int64_t size() const { return size_; }
  bool is_removable() const { return is_removable_; }
  // Indicator for |dlcservice| to allow preloading at a per DLC level.
  bool preload_allowed() const { return preload_allowed_; }
  bool mount_file_required() const { return mount_file_required_; }
  const std::string& used_by() const { return used_by_; }
  const std::string& description() const { return description_; }
  const std::map<std::string, std::string> metadata() const {
    return metadata_;
  }

 private:
  // Required manifest fields:
  int manifest_version_;
  std::vector<uint8_t> image_sha256_;
  std::vector<uint8_t> table_sha256_;
  std::string version_;

  // Optional manifest fields:
  FileSystem fs_type_;
  std::string id_;
  std::string package_;
  std::string name_;
  std::string image_type_;
  int64_t preallocated_size_;
  int64_t size_;
  bool is_removable_;
  bool preload_allowed_;
  bool mount_file_required_;
  std::string used_by_;
  std::string description_;
  std::map<std::string, std::string> metadata_;
};

}  // namespace imageloader

#endif  // IMAGELOADER_MANIFEST_H_
