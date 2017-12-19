// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class abstracts away the details about the layout of the component dir
// and how to verify/copy it.
//
// A component directory contains the following files:
//
// imageloader.json       Manifest JSON file
// imageloader.sig.1      Manifest signature
// manifest.fingerprint   Fingerprint file (used for delta updates)
// image.squash           squashfs image
// table                  dm-verity table, including parameters
#ifndef IMAGELOADER_COMPONENT_H_
#define IMAGELOADER_COMPONENT_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <base/macros.h>
#include <crypto/secure_hash.h>

#include "imageloader/helper_process.h"
#include "imageloader/imageloader_impl.h"

namespace imageloader {

// The permissions that the component update directory must use.
constexpr int kComponentDirPerms = 0755;
// The permissions that files in the component should have.
constexpr int kComponentFilePerms = 0644;

// The supported file systems for images.
enum class FileSystem { kExt4, kSquashFS };

class Component {
 public:
  // This is a parsed version of the imageloader.json manifest.
  struct Manifest {
    int manifest_version;
    std::vector<uint8_t> image_sha256;
    std::vector<uint8_t> table_sha256;
    std::string version;
    FileSystem fs_type;
    bool is_removable;
    std::map<std::string, std::string> metadata;
  };

  // Creates a Component. Returns nullptr if initialization and verification
  // fails.
  static std::unique_ptr<Component> Create(const base::FilePath& component_dir,
                                           const Keys& public_keys);

  // Copies the component into |dest_dir|. |dest_dir| must already exist. In
  // order to be robust against files being modified on disk, this function
  // verifies the files it copies against the manifest (which is loaded into
  // memory).
  bool CopyTo(const base::FilePath& dest_dir);

  // Mounts the component into |mount_point|. |mount_point| must already exist.
  bool Mount(HelperProcess* mounter, const base::FilePath& mount_point);

  // Return a reference to the parsed manifest object, which is stored in
  // memory.
  const Manifest& manifest();

 private:
  // Constructs a Component. We want to avoid using this where possible since
  // you need to load the manifest before doing anything anyway, so use the
  // static factory method above.
  Component(const base::FilePath& component_dir, int key_number);

  // Loads and verifies the manfiest. Returns false on failure. |public_key| is
  // the public key used to check the manifest signature.
  bool LoadManifest(const std::vector<uint8_t>& public_key);
  bool ParseManifest();
  bool CopyComponentFile(const base::FilePath& src, const base::FilePath& dest,
                         const std::vector<uint8_t>& expected_hash);
  // This reads the contents of |file|, hashes it with |sha256|, and if
  // |out_file| is not null, copies it into |out_file|.
  bool ReadHashAndCopyFile(base::File* file, std::vector<uint8_t>* sha256,
                           base::File* out_file);
  // Copies the fingerprint file that Chrome users for delta updates.
  bool CopyFingerprintFile(const base::FilePath& src,
                           const base::FilePath& dest);
  // Sanity check the fingerprint file.
  static bool IsValidFingerprintFile(const std::string& contents);

  FRIEND_TEST_ALL_PREFIXES(ComponentTest, IsValidFingerprintFile);
  FRIEND_TEST_ALL_PREFIXES(ComponentTest, CopyValidImage);

  const base::FilePath component_dir_;
  size_t key_number_;
  std::string manifest_raw_;
  std::string manifest_sig_;
  Manifest manifest_;

  DISALLOW_COPY_AND_ASSIGN(Component);
};

}  // namespace imageloader

#endif  // IMAGELOADER_COMPONENT_H_
