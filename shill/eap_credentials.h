// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_EAP_CREDENTIALS_H_
#define SHILL_EAP_CREDENTIALS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/optional.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libpasswordprovider/password_provider.h>

#include "shill/data_types.h"
#include "shill/technology.h"

namespace shill {

class CertificateFile;
class Error;
class KeyValueStore;
class Metrics;
class PropertyStore;
class StoreInterface;

class EapCredentials {
 public:
  EapCredentials();
  EapCredentials(const EapCredentials&) = delete;
  EapCredentials& operator=(const EapCredentials&) = delete;

  virtual ~EapCredentials();

  // Add property accessors to the EAP credential parameters in |this| to
  // |store|.
  void InitPropertyStore(PropertyStore* store);

  // Returns true if |property| is used for authentication in EapCredentials.
  static bool IsEapAuthenticationProperty(const std::string property);

  // Returns true if a connection can be made with |this| credentials using
  // either passphrase or certificates.
  virtual bool IsConnectable() const;

  // Returns true if a connection can be made with |this| credentials using
  // only passphrase properties.
  virtual bool IsConnectableUsingPassphrase() const;

  // Loads EAP properties from |storage| in group |id|.
  virtual void Load(const StoreInterface* store, const std::string& id);

  void MigrateDeprecatedStorage(StoreInterface* storage,
                                const std::string& id) const;

  // Output metrics about this EAP connection to |metrics| with technology
  // |technology|.
  virtual void OutputConnectionMetrics(Metrics* metrics,
                                       Technology technology) const;

  // Populate the wpa_supplicant DBus parameter map |params| with the
  // credentials in |this|.  To do so, this function may use |certificate_file|
  // to export CA certificates to be passed to wpa_supplicant.
  virtual void PopulateSupplicantProperties(CertificateFile* certificate_file,
                                            KeyValueStore* params) const;

  // Save EAP properties to |storage| in group |id|.  If |save_credentials|
  // is true, passwords and identities that are a part of the credentials are
  // also saved.
  virtual void Save(StoreInterface* store,
                    const std::string& id,
                    bool save_credentials) const;

  // Restore EAP properties to their initial state.
  virtual void Reset();

  // Setter that guards against emptying the "Key Management" value.
  virtual bool SetKeyManagement(const std::string& key_management,
                                Error* error);

  // Returns true if |type| is one of the subject alternative name types
  // supported by wpa_supplicant.
  static bool ValidSubjectAlternativeNameMatchType(const std::string& type);

  // Returns subject alternative name match in the format used by
  // wpa_supplicant by translating |subject_alternative_name_match_list|.
  static base::Optional<std::string> TranslateSubjectAlternativeNameMatch(
      const std::vector<std::string>& subject_alternative_name_match_list);

  // Getters and setters.
  virtual const std::string& identity() const { return identity_; }
  void set_identity(const std::string& identity) { identity_ = identity; }
  virtual const std::string& key_management() const { return key_management_; }
  virtual void set_password(const std::string& password) {
    password_ = password;
  }
  virtual const std::string& pin() const { return pin_; }

 private:
  friend class EapCredentialsTest;
  FRIEND_TEST(EapCredentialsTest, LoadAndSave);
  FRIEND_TEST(ServiceTest, LoadEap);
  FRIEND_TEST(ServiceTest, SaveEap);

  static const char kStorageCredentialEapAnonymousIdentity[];
  static const char kStorageCredentialEapIdentity[];
  static const char kStorageCredentialEapPassword[];

  static const char kStorageEapCACertID[];
  static const char kStorageEapCACertPEM[];
  static const char kStorageEapCertID[];
  static const char kStorageEapEap[];
  static const char kStorageEapInnerEap[];
  static const char kStorageEapTLSVersionMax[];
  static const char kStorageEapKeyID[];
  static const char kStorageEapKeyManagement[];
  static const char kStorageEapPin[];
  static const char kStorageEapSubjectMatch[];
  static const char kStorageEapUseProactiveKeyCaching[];
  static const char kStorageEapUseSystemCAs[];
  static const char kStorageEapUseLoginPassword[];

  // Returns true if the current EAP authentication type requires certificate
  // authentication and any of the client credentials are provided via
  // referencea cypto token.
  bool ClientAuthenticationUsesCryptoToken() const;

  // Expose a property in |store|, with the name |name|.
  //
  // Reads of the property will be handled by invoking |get|.
  // Writes to the property will be handled by invoking |set|.
  void HelpRegisterDerivedString(
      PropertyStore* store,
      const std::string& name,
      std::string (EapCredentials::*get)(Error* error),
      bool (EapCredentials::*set)(const std::string& value, Error* error));

  // Expose a property in |store|, with the name |name|.
  //
  // Reads of the property will be handled by invoking |get|.
  //
  // Clearing the property will be handled by invoking |clear|, or
  // calling |set| with |default_value| (whichever is non-NULL).  It
  // is an error to call this method with both |clear| and
  // |default_value| non-NULL.
  void HelpRegisterWriteOnlyDerivedString(
      PropertyStore* store,
      const std::string& name,
      bool (EapCredentials::*set)(const std::string& value, Error* error),
      void (EapCredentials::*clear)(Error* error),
      const std::string* default_value);

  // Setters for write-only RPC properties.
  bool SetEapPassword(const std::string& password, Error* error);

  // RPC getter for key_management_.
  std::string GetKeyManagement(Error* error);

  // When there is an inner EAP type, use this identity for the outer.
  std::string anonymous_identity_;
  // Locator for the client certificate within the security token.
  std::string cert_id_;
  // Who we identify ourselves as to the EAP authenticator.
  std::string identity_;
  // Locator for the client private key within the security token.
  std::string key_id_;
  // Key management algorithm to use after EAP succeeds.
  std::string key_management_;
  // Password to use for EAP methods which require one.
  std::string password_;
  // PIN code for accessing the security token.
  std::string pin_;

  // Locator for the CA certificate within the security token.
  std::string ca_cert_id_;
  // Raw PEM contents of the CA certificate.
  std::vector<std::string> ca_cert_pem_;
  // The outer or only EAP authentication type.
  std::string eap_;
  // The inner EAP authentication type.
  std::string inner_eap_;
  // The highest TLS version supplicant is allowed to negotiate.
  std::string tls_version_max_;
  // If non-empty, string to match remote subject against before connecting.
  std::string subject_match_;
  std::vector<std::string> subject_alternative_name_match_list_;
  // If true, use the system-wide CA database to authenticate the remote.
  bool use_system_cas_;
  // If true, use per network proactive key caching.
  bool use_proactive_key_caching_;
  // If true, use the user's stored login password as the password.
  bool use_login_password_;

  std::unique_ptr<password_provider::PasswordProviderInterface>
      password_provider_;
};

}  // namespace shill

#endif  // SHILL_EAP_CREDENTIALS_H_
