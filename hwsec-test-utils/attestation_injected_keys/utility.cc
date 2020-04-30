// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/attestation_injected_keys/utility.h"

#include <string>
#include <utility>

#include <base/logging.h>

namespace hwsec_test_utils {

namespace {

constexpr char kDefaultACAPublicKey[] =
    "A2976637E113CC457013F4334312A416395B08D4B2A9724FC9BAD65D0290F39C"
    "866D1163C2CD6474A24A55403C968CF78FA153C338179407FE568C6E550949B1"
    "B3A80731BA9311EC16F8F66060A2C550914D252DB90B44D19BC6C15E923FFCFB"
    "E8A366038772803EE57C7D7E5B3D5E8090BF0960D4F6A6644CB9A456708508F0"
    "6C19245486C3A49F807AB07C65D5E9954F4F8832BC9F882E9EE1AAA2621B1F43"
    "4083FD98758745CBFFD6F55DA699B2EE983307C14C9990DDFB48897F26DF8FB2"
    "CFFF03E631E62FAE59CBF89525EDACD1F7BBE0BA478B5418E756FF3E14AC9970"
    "D334DB04A1DF267D2343C75E5D282A287060D345981ABDA0B2506AD882579FEF";
constexpr char kDefaultACAPublicKeyID[] = "\x00\xc7\x0e\x50\xb1";

constexpr char kDefaultVASigningPublicKey[] =
    "bf7fefa3a661437b26aed0801db64d7ba8b58875c351d3bdc9f653847d4a67b3"
    "b67479327724d56aa0f71a3f57c2290fdc1ff05df80589715e381dfbbda2c4ac"
    "114c30d0a73c5b7b2e22178d26d8b65860aa8dd65e1b3d61a07c81de87c1e7e4"
    "590145624936a011ece10434c1d5d41f917c3dc4b41dd8392479130c4fd6eafc"
    "3bb4e0dedcc8f6a9c28428bf8fbba8bd6438a325a9d3eabee1e89e838138ad99"
    "69c292c6d9f6f52522333b84ddf9471ffe00f01bf2de5faa1621f967f49e158b"
    "f2b305360f886826cc6fdbef11a12b2d6002d70d8d1e8f40e0901ff94c203cb2"
    "01a36a0bd6e83955f14b494f4f2f17c0c826657b85c25ffb8a73599721fa17ab";

constexpr char kDefaultVAEncryptionPublicKey[] =
    "edba5e723da811e41636f792c7a77aef633fbf39b542aa537c93c93eaba7a3b1"
    "0bc3e484388c13d625ef5573358ec9e7fbeb6baaaa87ca87d93fb61bf5760e29"
    "6813c435763ed2c81f631e26e3ff1a670261cdc3c39a4640b6bbf4ead3d6587b"
    "e43ef7f1f08e7596b628ec0b44c9b7ad71c9ee3a1258852c7a986c7614f0c4ec"
    "f0ce147650a53b6aa9ae107374a2d6d4e7922065f2f6eb537a994372e1936c87"
    "eb08318611d44daf6044f8527687dc7ce5319b51eae6ab12bee6bd16e59c499e"
    "fa53d80232ae886c7ee9ad8bc1cbd6e4ac55cb8fa515671f7e7ad66e98769f52"
    "c3c309f98bf08a3b8fbb0166e97906151b46402217e65c5d01ddac8514340e8b";
constexpr char kDefaultVAEncryptionPublicKeyID[] = "\x00\x4a\xe2\xdc\xae";

// Ignores the extra null-terminated element and converts only the effective
// part to std::string.
// This is copied from //src/platform2/attestation/server/google_keys.cc, and it
// will be removed right after we alter all the keys to the well-known ones for
// testing because the local testing infrastructure doesn't care about the key
// id so it can be set to just a human-readable string.
template <size_t size>
std::string ZeroTerminatedCharArrayToString(
    const char (&array)[size]) noexcept {
  CHECK_GE(size, 0);
  return std::string(std::begin(array), std::end(array) - 1);
}

}  // namespace

attestation::DefaultGoogleRsaPublicKeySet GenerateAttestationGoogleKeySet() {
  attestation::DefaultGoogleRsaPublicKeySet keyset;
  attestation::GoogleRsaPublicKey key;

  key.set_modulus_in_hex(kDefaultACAPublicKey);
  key.set_key_id(ZeroTerminatedCharArrayToString(kDefaultACAPublicKeyID));
  *keyset.mutable_default_ca_encryption_key() = std::move(key);

  key.set_modulus_in_hex(kDefaultVASigningPublicKey);
  *keyset.mutable_default_va_signing_key() = std::move(key);

  key.set_modulus_in_hex(kDefaultVAEncryptionPublicKey);
  key.set_key_id(
      ZeroTerminatedCharArrayToString(kDefaultVAEncryptionPublicKeyID));
  *keyset.mutable_default_va_encryption_key() = std::move(key);

  return keyset;
}

}  // namespace hwsec_test_utils
