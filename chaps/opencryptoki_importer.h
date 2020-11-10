// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_OPENCRYPTOKI_IMPORTER_H_
#define CHAPS_OPENCRYPTOKI_IMPORTER_H_

#include "chaps/object_importer.h"

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "chaps/object.h"

namespace chaps {

class ChapsFactory;
class TPMUtility;

// OpencryptokiImporter imports token objects from an opencryptoki database.
class OpencryptokiImporter : public ObjectImporter {
 public:
  OpencryptokiImporter(int slot,
                       const base::FilePath& path,
                       TPMUtility* tpm,
                       ChapsFactory* factory);
  OpencryptokiImporter(const OpencryptokiImporter&) = delete;
  OpencryptokiImporter& operator=(const OpencryptokiImporter&) = delete;

  ~OpencryptokiImporter() override;

  // ObjectImporter interface.
  bool ImportObjects(ObjectPool* object_pool) override;
  bool FinishImportAsync(ObjectPool* object_pool) override;

 private:
  // Parses an opencryptoki object file and extracts the object data and whether
  // or not it is encrypted. Returns true on success.
  bool ExtractObjectData(const std::string& object_file_content,
                         bool* is_encrypted,
                         std::string* object_data);

  // Parses an object flattened by opencryptoki.
  bool UnflattenObject(const std::string& object_data,
                       const std::string& object_name,
                       bool is_encrypted,
                       AttributeMap* attributes);

  // Determines if an object is an internal opencryptoki object and processes it
  // if so. Returns true if the object was processed.
  bool ProcessInternalObject(const AttributeMap& attributes,
                             ObjectPool* object_pool);

  // Loads the opencryptoki key hierarchy so it is available for unbinding and
  // unwrapping other objects. This can only succeed if all internal objects
  // that make up the hierarchy have been found and processed by
  // ProcessInternalObject. Typically, these objects are '00000000' through
  // '70000000' in the TOK_OBJ directory. Returns true on success.
  // Parameters:
  //   load_private - Specifies whether to load the public or private hierarchy.
  bool LoadKeyHierarchy(bool load_private);

  // Uses the TPM to decrypt the opencryptoki master key. Returns true on
  // success.
  bool DecryptMasterKey(const std::string& encrypted_master_key,
                        brillo::SecureBlob* master_key);

  // Decrypts an object that was encrypted with the opencryptoki master key.
  // Returns true on success.
  bool DecryptObject(const brillo::SecureBlob& key,
                     const std::string& encrypted_object_data,
                     std::string* object_data);

  // Converts all attributes of an object to Chaps format. This includes
  // unwrapping keys with the opencryptoki hierarchy and wrapping again with the
  // Chaps hierarchy. Returns true on success.
  bool ConvertToChapsFormat(AttributeMap* attributes);

  // Create an object instance complete with policies. Returns true on success.
  bool CreateObjectInstance(const AttributeMap& attributes, Object** object);

  // Returns whether a given set of attributes represents a private key.
  bool IsPrivateKey(const AttributeMap& attributes);

  // Decrypts and unflattens all pending encrypted objects.
  bool DecryptPendingObjects();

  // The token slot id. We need this to associate with our key handles.
  int slot_;
  base::FilePath path_;
  TPMUtility* tpm_;
  ChapsFactory* factory_;
  // Opencrytoki hierarchy key handles.
  int private_root_key_;
  int private_leaf_key_;
  int public_root_key_;
  int public_leaf_key_;
  // Opencryptoki hierarchy blobs.
  std::string private_root_blob_;
  std::string private_leaf_blob_;
  std::string public_root_blob_;
  std::string public_leaf_blob_;
  // The path to the encrypted master key file.
  base::FilePath master_key_path_;
  // Stores encrypted objects to be imported pending decryption.
  std::map<std::string, std::string> encrypted_objects_;
  // Stores decrypted, unflattened objects ready for import.
  std::vector<AttributeMap> unflattened_objects_;
};

}  // namespace chaps

#endif  // CHAPS_OPENCRYPTOKI_IMPORTER_H_
