// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/json/json_string_value_serializer.h>
#include <base/strings/string_number_conversions.h>

#include "imageloader/manifest.h"

namespace imageloader {

namespace {
// The current version of the manifest file.
constexpr int kCurrentManifestVersion = 1;
// The name of the version field in the manifest.
constexpr char kManifestVersionField[] = "manifest-version";
// The name of the component version field in the manifest.
constexpr char kVersionField[] = "version";
// The name of the field containing the image hash.
constexpr char kImageHashField[] = "image-sha256-hash";
// The name of the bool field indicating whether component is removable.
constexpr char kIsRemovableField[] = "is-removable";
// The name of the metadata field.
constexpr char kMetadataField[] = "metadata";
// The name of the field containing the table hash.
constexpr char kTableHashField[] = "table-sha256-hash";
// Optional manifest fields.
constexpr char kFSType[] = "fs-type";
constexpr char kId[] = "id";
constexpr char kPackage[] = "package";
constexpr char kName[] = "name";
constexpr char kImageType[] = "image-type";
constexpr char kPreallocatedSize[] = "pre-allocated-size";
constexpr char kSize[] = "size";
constexpr char kPreloadAllowed[] = "preload-allowed";
constexpr char kUsedBy[] = "used-by";
constexpr char kDescription[] = "description";

bool GetSHA256FromString(const std::string& hash_str,
                         std::vector<uint8_t>* bytes) {
  if (!base::HexStringToBytes(hash_str, bytes))
    return false;
  return bytes->size() == 32;
}

// Ensure the metadata entry is a dictionary mapping strings to strings and
// parse it into |out_metadata| and return true if so.
bool ParseMetadata(const base::Value* metadata_element,
                   std::map<std::string, std::string>* out_metadata) {
  DCHECK(out_metadata);

  const base::DictionaryValue* metadata_dict = nullptr;
  if (!metadata_element->GetAsDictionary(&metadata_dict))
    return false;

  base::DictionaryValue::Iterator it(*metadata_dict);
  for (; !it.IsAtEnd(); it.Advance()) {
    std::string parsed_value;
    if (!it.value().GetAsString(&parsed_value)) {
      LOG(ERROR) << "Key \"" << it.key() << "\" did not map to string value";
      return false;
    }

    (*out_metadata)[it.key()] = std::move(parsed_value);
  }

  return true;
}

}  // namespace

Manifest::Manifest()
    : manifest_version_(0),
      fs_type_(FileSystem::kExt4),
      preallocated_size_(0),
      size_(0),
      is_removable_(false) {}

bool Manifest::ParseManifest(const std::string& manifest_raw) {
  // Now deserialize the manifest json and read out the rest of the component.
  int error_code;
  std::string error_message;
  JSONStringValueDeserializer deserializer(manifest_raw);
  std::unique_ptr<base::Value> value =
      deserializer.Deserialize(&error_code, &error_message);

  if (!value) {
    LOG(ERROR) << "Could not deserialize the manifest file. Error "
               << error_code << ": " << error_message;
    return false;
  }

  base::DictionaryValue* manifest_dict = nullptr;
  if (!value->GetAsDictionary(&manifest_dict)) {
    LOG(ERROR) << "Could not parse manifest file as JSON.";
    return false;
  }

  // This will have to be changed if the manifest version is bumped.
  int version;
  if (!manifest_dict->GetInteger(kManifestVersionField, &version)) {
    LOG(ERROR) << "Could not parse manifest version field from manifest.";
    return false;
  }
  if (version != kCurrentManifestVersion) {
    LOG(ERROR) << "Unsupported version of the manifest.";
    return false;
  }
  manifest_version_ = version;

  std::string image_hash_str;
  if (!manifest_dict->GetString(kImageHashField, &image_hash_str)) {
    LOG(ERROR) << "Could not parse image hash from manifest.";
    return false;
  }

  if (!GetSHA256FromString(image_hash_str, &(image_sha256_))) {
    LOG(ERROR) << "Could not convert image hash to bytes.";
    return false;
  }

  std::string table_hash_str;
  if (!manifest_dict->GetString(kTableHashField, &table_hash_str)) {
    LOG(ERROR) << "Could not parse table hash from manifest.";
    return false;
  }

  if (!GetSHA256FromString(table_hash_str, &(table_sha256_))) {
    LOG(ERROR) << "Could not convert table hash to bytes.";
    return false;
  }

  if (!manifest_dict->GetString(kVersionField, &(version_))) {
    LOG(ERROR) << "Could not parse component version from manifest.";
    return false;
  }

  // The fs_type field is optional, and squashfs by default.
  fs_type_ = FileSystem::kSquashFS;
  std::string fs_type;
  if (manifest_dict->GetString(kFSType, &fs_type)) {
    if (fs_type == "ext4") {
      fs_type_ = FileSystem::kExt4;
    } else if (fs_type == "squashfs") {
      fs_type_ = FileSystem::kSquashFS;
    } else {
      LOG(ERROR) << "Unsupported file system type: " << fs_type;
      return false;
    }
  }

  if (!manifest_dict->GetBoolean(kIsRemovableField, &is_removable_)) {
    // If |is-removable| field does not exist, by default it is false.
    is_removable_ = false;
  }

  if (!manifest_dict->GetBoolean(kPreloadAllowed, &preload_allowed_)) {
    // If |preaload-allowed| field does not exist, by default it is false.
    preload_allowed_ = false;
  }

  // All of these fields are optional.
  manifest_dict->GetString(kId, &id_);
  manifest_dict->GetString(kPackage, &package_);
  manifest_dict->GetString(kName, &name_);
  manifest_dict->GetString(kImageType, &image_type_);
  manifest_dict->GetString(kUsedBy, &used_by_);
  manifest_dict->GetString(kDescription, &description_);

  std::string preallocated_size_str;
  if (manifest_dict->GetString(kPreallocatedSize, &preallocated_size_str)) {
    if (!base::StringToInt64(preallocated_size_str, &preallocated_size_)) {
      LOG(ERROR) << "Manifest pre-allocated-size was malformed: "
                 << preallocated_size_str;
      return false;
    }
  }

  std::string size_str;
  if (manifest_dict->GetString(kSize, &size_str)) {
    if (!base::StringToInt64(size_str, &size_)) {
      LOG(ERROR) << "Manifest size was malformed: " << size_str;
      return false;
    }
  }

  // Copy out the metadata, if it's there.
  const base::Value* metadata = nullptr;
  if (manifest_dict->Get(kMetadataField, &metadata)) {
    if (!ParseMetadata(metadata, &metadata_)) {
      LOG(ERROR) << "Manifest metadata was malformed";
      return false;
    }
  }

  return true;
}

}  // namespace imageloader
