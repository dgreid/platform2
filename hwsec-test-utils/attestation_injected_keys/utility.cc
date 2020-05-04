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
    "e7cb0cc9d2f904ec3f09a379b8fe09a7ef621f15657523138e886ebbc000826e"
    "189a947a62d50679f8c19cfd84065388d627dd11f7e8e7bf77813579d6fb8a96"
    "77e4508aa26a66beb69d3c616c628d51be350c59d6988d86645c54c6ec13da9d"
    "451b44a386c9699da809a2ecec6f053ad6ddd761d3023d944f1b0b5e138543c3"
    "948f8a7f0f0684f284ed38b4cd37dc15505049f0923e2ab49fc85dc87027c5cc"
    "bd86d486616623976965877486be656427a2ee56c195ee38becc153369f8d43e"
    "2ccda18e53f763925406581adcbeb0766b898f279ea5161359bc79d300028fe8"
    "a3f52077d50aaaf82aadb7273483702ffc17d68f0f413459edca974d76ca3c9f";

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
