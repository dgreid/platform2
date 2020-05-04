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
    "bc435db064ecf44b650ead16f2934035a0e6ecfc76c4f3f7c26ce459482c66f6"
    "747b8e510c03e94808608f076b4d3ad3470d710c1b8d731cbe2d4c53e2df7367"
    "7ced201df57c8c86503cc2442faa71c88a66f86726b5791b8d7888df1357defb"
    "d1b5cddffe10e2ec9ef7a47eede4d74c33ca4e34f0801bed065188f035e729ff"
    "f10b46432ed320f993d75ecccebff88d197a0f20dfefa438d5f58c69578e6037"
    "821943721c21daeab845716f4823748ea8080a4bb43786e1cc70f3363bfb98d5"
    "1a3b77a5b3a44b18a029296ad075e93df31abe2105c68a6fafb8b47ad52ec01e"
    "adde56c522e1369a9fb5175ea5e8ebd8c35c0cd16ee1d6930f34821f12f46459";
constexpr char kDefaultVAEncryptionPublicKeyID[] = "VaEnc";

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
  key.set_key_id(kDefaultVAEncryptionPublicKeyID);
  *keyset.mutable_default_va_encryption_key() = std::move(key);

  return keyset;
}

}  // namespace hwsec_test_utils
