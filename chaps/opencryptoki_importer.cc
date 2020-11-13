// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/opencryptoki_importer.h"

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "chaps/chaps_factory.h"
#include "chaps/chaps_utility.h"
#include "chaps/object.h"
#include "chaps/object_pool.h"
#include "chaps/tpm_utility.h"
#include "pkcs11/cryptoki.h"

using base::FilePath;
using brillo::SecureBlob;
using std::map;
using std::string;
using std::vector;

namespace {

// Extracts a 32-bit integer by reinterpreting bytes.
uint32_t ExtractUint32(const void* data) {
  uint32_t value;
  memcpy(&value, data, sizeof(value));
  return value;
}

}  // namespace

namespace chaps {

OpencryptokiImporter::OpencryptokiImporter(int slot,
                                           const FilePath& path,
                                           TPMUtility* tpm,
                                           ChapsFactory* factory)
    : slot_(slot),
      path_(path),
      tpm_(tpm),
      factory_(factory),
      private_root_key_(0),
      private_leaf_key_(0),
      public_root_key_(0),
      public_leaf_key_(0) {}

OpencryptokiImporter::~OpencryptokiImporter() {}

bool OpencryptokiImporter::ImportObjects(ObjectPool* object_pool) {
  const char kOpencryptokiDir[] = ".tpm";
  const char kOpencryptokiObjectDir[] = "TOK_OBJ";
  const char kOpencryptokiMasterKey[] = "MK_PRIVATE";
  const char kOpencryptokiObjectIndex[] = "OBJ.IDX";

  LOG(INFO) << "Importing opencryptoki objects.";
  FilePath base_path = path_.DirName().Append(kOpencryptokiDir);
  FilePath object_path = base_path.Append(kOpencryptokiObjectDir);
  FilePath index_path = object_path.Append(kOpencryptokiObjectIndex);
  master_key_path_ = base_path.Append(kOpencryptokiMasterKey);
  if (!base::PathExists(index_path)) {
    LOG(WARNING) << "Did not find any opencryptoki objects to import.";
    return true;
  }
  string index;
  if (!base::ReadFileToString(index_path, &index)) {
    LOG(ERROR) << "Failed to read object index.";
    return false;
  }
  vector<string> object_files =
      base::SplitString(index, base::kWhitespaceASCII, base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  vector<AttributeMap> ready_for_import;
  LOG(INFO) << "Found " << object_files.size() << " object files.";
  // Try to read and process each file listed in the index file. If a problem
  // occurs just move on the next one.
  for (size_t i = 0; i < object_files.size(); ++i) {
    string object_file_content;
    if (!base::ReadFileToString(object_path.Append(object_files[i]),
                                &object_file_content)) {
      LOG(WARNING) << "Failed to read object file: " << object_files[i];
      continue;
    }
    bool is_encrypted = false;
    string flat_object;
    if (!ExtractObjectData(object_file_content, &is_encrypted, &flat_object)) {
      LOG(WARNING) << "Failed to parse object file: " << object_files[i];
      continue;
    }
    if (is_encrypted) {
      // We can't process encrypted files until we have the master key.
      encrypted_objects_[object_files[i]] = flat_object;
      continue;
    }
    AttributeMap attributes;
    if (!UnflattenObject(flat_object, object_files[i], false, &attributes)) {
      LOG(WARNING) << "Failed to parse object attributes: " << object_files[i];
      continue;
    }
    if (!ProcessInternalObject(attributes, object_pool)) {
      // This is an ordinary object.
      ready_for_import.push_back(attributes);
    }
  }
  if (encrypted_objects_.size() == 0 && ready_for_import.size() == 0) {
    // Nothing to import, our job is done.
    LOG(INFO) << "Did not find any opencryptoki objects to import.";
    return true;
  }
  LOG(INFO) << "Found objects: " << encrypted_objects_.size() << " private, "
            << ready_for_import.size() << " public.";
  // Objects that have opencryptoki internal attributes such as tpm-protected
  // blobs need to be moved to the chaps format.
  int num_imported = 0;
  for (size_t i = 0; i < ready_for_import.size(); ++i) {
    if (IsPrivateKey(ready_for_import[i])) {
      // Private keys need authorization data decrypted which requires the TPM.
      // Queue up the object for later processing.
      unflattened_objects_.push_back(ready_for_import[i]);
      continue;
    }
    Object* object = NULL;
    if (!CreateObjectInstance(ready_for_import[i], &object)) {
      LOG(WARNING) << "Failed to create an object instance.";
      continue;
    }
    if (object_pool->Import(object) == ObjectPool::Result::Success)
      ++num_imported;
  }
  LOG(INFO) << "Imported: " << num_imported << "; Pending: "
            << encrypted_objects_.size() + unflattened_objects_.size();
  return true;
}

bool OpencryptokiImporter::FinishImportAsync(ObjectPool* object_pool) {
  // If there are any encrypted objects, now is the time to decrypt them.
  if (!DecryptPendingObjects()) {
    LOG(WARNING) << "Failed to decrypt encrypted objects. Only public objects"
                 << " can be imported.";
  }
  // Objects that have opencryptoki internal attributes such as tpm-protected
  // blobs need to be moved to the chaps format.
  int num_imported = 0;
  for (size_t i = 0; i < unflattened_objects_.size(); ++i) {
    if (!ConvertToChapsFormat(&unflattened_objects_[i])) {
      LOG(WARNING) << "Failed to convert an object to Chaps format.";
      continue;
    }
    Object* object = NULL;
    if (!CreateObjectInstance(unflattened_objects_[i], &object)) {
      LOG(WARNING) << "Failed to create an object instance.";
      continue;
    }
    if (object_pool->Import(object) == ObjectPool::Result::Success)
      ++num_imported;
  }
  LOG(INFO) << "Finished importing " << num_imported << " pending objects.";
  return true;
}

bool OpencryptokiImporter::ExtractObjectData(const string& object_file_content,
                                             bool* is_encrypted,
                                             string* object_data) {
  // An object file has a header formatted as follows:
  // * Total Length - 4 bytes
  // * Private Indicator - 1 byte
  // * Object Data - All remaining bytes.
  if (object_file_content.length() < 5)
    return false;
  size_t total_length = ExtractUint32(object_file_content.data());
  if (total_length != object_file_content.length())
    return false;
  *is_encrypted = (0 != object_file_content[4]);
  *object_data = object_file_content.substr(5);
  return true;
}

bool OpencryptokiImporter::UnflattenObject(const string& object_data,
                                           const string& object_name,
                                           bool is_encrypted,
                                           AttributeMap* attributes) {
  // A flattened object is laid out as follows:
  // * Object Class - 4 bytes (ignored).
  // * Number of Attributes - 4 bytes.
  // * Object Name - 8 bytes.
  // * Attribute List - No padding between items, each item as follows:
  //   ** CK_ATTRIBUTE with 32-bit fields - 12 bytes.
  //   ** CK_ATTRIBUTE::ulValueLen bytes of data.
  if (object_data.length() < 16)
    return false;
  // If this exact number of attributes cannot be extracted the object will be
  // considered corrupted.
  int num_attrs = ExtractUint32(&object_data[4]);
  // This is not needed but we'll consider the object corrupted if it doesn't
  // match the file name from where this object data was extracted.
  string stored_object_name(object_data.substr(8, 8));
  if (stored_object_name != object_name) {
    LOG(ERROR) << "Object name mismatch: " << object_name << ", "
               << stored_object_name;
    return false;
  }
  size_t pos = 16;
  for (int i = 0; i < num_attrs; ++i) {
    if (object_data.size() < pos + 12)
      return false;
    CK_ATTRIBUTE_TYPE type = ExtractUint32(&object_data[pos]);
    size_t length = ExtractUint32(&object_data[pos + 8]);
    pos += 12;
    if (object_data.size() < pos + length)
      return false;
    string data = object_data.substr(pos, length);
    pos += length;
    if (type == CKA_PRIVATE) {
      if (data.size() < 1)
        return false;
      bool is_private = (data[0] != CK_FALSE);
      if (is_encrypted != is_private) {
        LOG(ERROR) << "Object privacy mismatch.";
        return false;
      }
    }
    (*attributes)[type] = data;
  }
  return true;
}

bool OpencryptokiImporter::ProcessInternalObject(const AttributeMap& attributes,
                                                 ObjectPool* object_pool) {
  const CK_ATTRIBUTE_TYPE kOpencryptokiHidden = CKA_VENDOR_DEFINED + 0x01000000;
  const CK_ATTRIBUTE_TYPE kOpencryptokiOpaque = CKA_VENDOR_DEFINED + 1;
  const char kPrivateRootKeyID[] = "PRIVATE ROOT KEY";
  const char kPrivateLeafKeyID[] = "PRIVATE LEAF KEY";
  const char kPublicRootKeyID[] = "PUBLIC ROOT KEY";
  const char kPublicLeafKeyID[] = "PUBLIC LEAF KEY";

  // The primary indicator we use to determine if this object is an internal
  // object is the opencryptoki hidden attribute (aka CKA_HIDDEN).
  AttributeMap::const_iterator it = attributes.find(kOpencryptokiHidden);
  if (it == attributes.end() || !it->second[0])
    return false;

  // From here on we will return 'true' even if we fail to import the object
  // because we don't want an internal object to get treated as an ordinary
  // object. The public key objects are not useful so we'll discard them. From
  // the private key objects we'll extract the TPM-wrapped blobs.
  it = attributes.find(CKA_CLASS);
  if (it == attributes.end() || it->second.size() != 4)
    return true;
  CK_OBJECT_CLASS object_class = ExtractUint32(it->second.data());
  if (object_class != CKO_PRIVATE_KEY)
    return true;
  // Extract the TPM-wrapped blob.
  it = attributes.find(kOpencryptokiOpaque);
  if (it == attributes.end())
    return true;
  string blob = it->second;
  // Extract the ID so we can determine which object this is. If we don't
  // recognize the ID then the object will be discarded.
  it = attributes.find(CKA_ID);
  if (it == attributes.end())
    return true;
  string id = it->second;
  if (id == kPrivateRootKeyID) {
    if (!object_pool->SetInternalBlob(kLegacyPrivateRootKey, blob)) {
      LOG(ERROR) << "Failed to write private root key blob.";
      return true;
    }
    private_root_blob_ = blob;
  } else if (id == kPrivateLeafKeyID) {
    private_leaf_blob_ = blob;
  } else if (id == kPublicRootKeyID) {
    if (!object_pool->SetInternalBlob(kLegacyPublicRootKey, blob)) {
      LOG(ERROR) << "Failed to write public root key blob.";
      return true;
    }
    public_root_blob_ = blob;
  } else if (id == kPublicLeafKeyID) {
    public_leaf_blob_ = blob;
  }
  return true;
}

bool OpencryptokiImporter::LoadKeyHierarchy(bool load_private) {
  const char kDefaultAuthData[] = "111111";
  string& root_blob = load_private ? private_root_blob_ : public_root_blob_;
  string& leaf_blob = load_private ? private_leaf_blob_ : public_leaf_blob_;
  int& root_key = load_private ? private_root_key_ : public_root_key_;
  int& leaf_key = load_private ? private_leaf_key_ : public_leaf_key_;
  // Check if the requested hierarchy is already loaded.
  if (root_key && leaf_key)
    return true;
  // We need both the root and leaf blobs in order to proceed.
  if (root_blob.empty() || leaf_blob.empty())
    return false;
  // Load the root key.
  if (!tpm_->LoadKey(slot_, root_blob, SecureBlob(), &root_key))
    return false;
  // Load the leaf key.
  SecureBlob leaf_auth_data = Sha1(
      SecureBlob(std::begin(kDefaultAuthData), std::end(kDefaultAuthData)));
  if (!tpm_->LoadKeyWithParent(slot_, leaf_blob, leaf_auth_data, root_key,
                               &leaf_key))
    return false;
  return true;
}

bool OpencryptokiImporter::DecryptMasterKey(const string& encrypted_master_key,
                                            SecureBlob* master_key) {
  if (!LoadKeyHierarchy(true)) {
    LOG(ERROR) << "Failed to load private key hierarchy.";
    return false;
  }
  // Trousers defines the handle value 0 as NULL_HKEY so this check works.
  CHECK(private_leaf_key_);
  // The master key is encrypted with a simple bind to the private leaf key.
  string master_key_str;
  if (!tpm_->Unbind(private_leaf_key_, encrypted_master_key, &master_key_str)) {
    LOG(ERROR) << "Failed to decrypt master key.";
    return false;
  }
  *master_key = SecureBlob(master_key_str.begin(), master_key_str.end());
  brillo::SecureClear(&master_key_str);
  return true;
}

bool OpencryptokiImporter::DecryptObject(const SecureBlob& key,
                                         const string& encrypted_object_data,
                                         string* object_data) {
  // Objects are encrypted with AES-256-CBC and a hard-coded IV.
  const char kOpencryptokiIV[] = ")#%&!*)^!()$&!&N";
  const size_t kSha1OutputBytes = 20;
  string decrypted;
  if (!RunCipher(false, key, kOpencryptokiIV, encrypted_object_data,
                 &decrypted))
    return false;
  // The data is formatted as follows:
  // * Length of object data - 4 bytes.
  // * Object data - 'length' bytes.
  // * SHA-1 of object data - 20 bytes.
  if (decrypted.length() < 24)
    return false;
  size_t length = ExtractUint32(decrypted.data());
  if (decrypted.size() != 4 + length + kSha1OutputBytes)
    return false;
  *object_data = decrypted.substr(4, length);
  if (Sha1(*object_data) != decrypted.substr(4 + length))
    return false;
  return true;
}

bool OpencryptokiImporter::ConvertToChapsFormat(AttributeMap* attributes) {
  // There are two special attributes of private keys that need to be converted:
  // 1. The tpm-wrapped blob (aka CKA_IBM_OPAQUE).
  // 2. The encrypted authorization data (aka CKA_ENC_AUTHDATA).
  const CK_ATTRIBUTE_TYPE kOpencryptokiOpaque = CKA_VENDOR_DEFINED + 1;
  const CK_ATTRIBUTE_TYPE kOpencryptokiAuthData =
      CKA_VENDOR_DEFINED + 0x01000001;

  // Sanity check for mandatory attributes that we need.
  AttributeMap::iterator class_it = attributes->find(CKA_CLASS);
  AttributeMap::iterator private_it = attributes->find(CKA_PRIVATE);
  if (class_it == attributes->end() || class_it->second.size() < 4 ||
      private_it == attributes->end() || private_it->second.size() < 1)
    return false;

  // If the object is not a private key, we can leave it as is.
  CK_OBJECT_CLASS object_class = ExtractUint32(class_it->second.data());
  if (object_class != CKO_PRIVATE_KEY)
    return true;

  // It is possible that these two attributes are missing and in that case we
  // can leave the object untouched. If the blob attribute exists but the
  // authorization data attribute doesn't, then it is considered a failure.
  AttributeMap::iterator blob_it = attributes->find(kOpencryptokiOpaque);
  if (blob_it == attributes->end())
    return true;
  string tpm_wrapped_blob = blob_it->second;
  AttributeMap::iterator auth_it = attributes->find(kOpencryptokiAuthData);
  if (auth_it == attributes->end())
    return false;
  string encrypted_auth_data = auth_it->second;

  // The value of CKA_PRIVATE tells us which hierarchy we're working with.
  CK_BBOOL is_private = private_it->second[0];
  int leaf_key_handle = is_private ? private_leaf_key_ : public_leaf_key_;
  if (!LoadKeyHierarchy(is_private)) {
    LOG(ERROR) << "Failed to load key hierarchy: private=" << is_private;
    return false;
  }
  // Trousers defines the handle value 0 as NULL_HKEY so this check works.
  CHECK(leaf_key_handle);

  // Decrypt the authorization data.
  string auth_data;
  if (!tpm_->Unbind(leaf_key_handle, encrypted_auth_data, &auth_data)) {
    LOG(ERROR) << "Failed to unbind authorization data.";
    return false;
  }

  // Remove the opencryptoki-specific attributes from the object and insert the
  // expected chaps-specific attributes.
  attributes->erase(kOpencryptokiOpaque);
  attributes->erase(kOpencryptokiAuthData);
  (*attributes)[kKeyBlobAttribute] = tpm_wrapped_blob;
  (*attributes)[kAuthDataAttribute] = auth_data;
  (*attributes)[kKeyInSoftware] = string(1, CK_FALSE);
  (*attributes)[kLegacyAttribute] = string(1, CK_TRUE);
  return true;
}

bool OpencryptokiImporter::CreateObjectInstance(const AttributeMap& attributes,
                                                Object** object) {
  *object = factory_->CreateObject();
  for (AttributeMap::const_iterator it = attributes.begin();
       it != attributes.end(); ++it) {
    (*object)->SetAttributeString(it->first, it->second);
  }
  CK_RV result = (*object)->FinalizeNewObject();
  if (result != CKR_OK) {
    LOG(ERROR) << "Failed to validate new object: " << CK_RVToString(result);
    return false;
  }
  return true;
}

bool OpencryptokiImporter::IsPrivateKey(const AttributeMap& attributes) {
  AttributeMap::const_iterator class_it = attributes.find(CKA_CLASS);
  if (class_it == attributes.end() || class_it->second.size() < 4)
    return false;
  CK_OBJECT_CLASS object_class = ExtractUint32(class_it->second.data());
  return (object_class == CKO_PRIVATE_KEY);
}

bool OpencryptokiImporter::DecryptPendingObjects() {
  if (encrypted_objects_.size() > 0) {
    string encrypted_master_key;
    if (!base::PathExists(master_key_path_) ||
        !base::ReadFileToString(master_key_path_, &encrypted_master_key)) {
      LOG(ERROR) << "Failed to read encrypted master key.";
      return false;
    }
    SecureBlob master_key;
    if (!DecryptMasterKey(encrypted_master_key, &master_key)) {
      LOG(ERROR) << "Failed to decrypt the master key.";
      return false;
    }
    for (map<string, string>::iterator iter = encrypted_objects_.begin();
         iter != encrypted_objects_.end(); ++iter) {
      string flat_object;
      if (!DecryptObject(master_key, iter->second, &flat_object)) {
        LOG(WARNING) << "Failed to decrypt an encrypted object: "
                     << iter->first;
        continue;
      }
      AttributeMap attributes;
      if (!UnflattenObject(flat_object, iter->first, true, &attributes)) {
        LOG(WARNING) << "Failed to parse object attributes: " << iter->first;
        continue;
      }
      unflattened_objects_.push_back(attributes);
    }
  }
  return true;
}

}  // namespace chaps
