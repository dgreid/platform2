// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/sys_byteorder.h>

#include "u2fd/tpm_vendor_cmd.h"

namespace {

// All TPM extension commands use this struct for input and output. Any other
// data follows immediately after. All values are big-endian over the wire.
struct TpmCmdHeader {
  uint16_t tag;              // TPM_ST_NO_SESSIONS
  uint32_t size;             // including this header
  uint32_t code;             // Command out, Response back.
  uint16_t subcommand_code;  // Additional command/response codes
} __attribute__((packed));

// TPMv2 Spec mandates that vendor-specific command codes have bit 29 set,
// while bits 15-0 indicate the command. All other bits should be zero. We
// define one of those 16-bit command values for Cr50 purposes, and use the
// subcommand_code in struct TpmCmdHeader to further distinguish the desired
// operation.
const uint32_t kTpmCcVendorBit = 0x20000000;

// Vendor-specific command codes
const uint32_t kTpmCcVendorCr50 = 0x0000;

// Cr50 vendor-specific subcommand codes. 16 bits available.
// TODO(louiscollard): Replace this with constants from cr50 header.
const uint16_t kVendorCcU2fApdu = 27;
const uint16_t kVendorCcU2fGenerate = 44;
const uint16_t kVendorCcU2fSign = 45;
const uint16_t kVendorCcU2fAttest = 46;

}  // namespace

namespace u2f {

TpmVendorCommandProxy::TpmVendorCommandProxy() {}
TpmVendorCommandProxy::~TpmVendorCommandProxy() {}

base::Lock& TpmVendorCommandProxy::GetLock() {
  return lock_;
}

uint32_t TpmVendorCommandProxy::VendorCommand(uint16_t cc,
                                              const std::string& input,
                                              std::string* output) {
  // Pack up the header and the input
  TpmCmdHeader header;
  header.tag = base::HostToNet16(trunks::TPM_ST_NO_SESSIONS);
  header.size = base::HostToNet32(sizeof(header) + input.size());
  header.code = base::HostToNet32(kTpmCcVendorBit | kTpmCcVendorCr50);
  header.subcommand_code = base::HostToNet16(cc);

  std::string command(reinterpret_cast<char*>(&header), sizeof(header));
  command += input;

  // Send the command, get the response
  VLOG(2) << "Out(" << command.size()
          << "): " << base::HexEncode(command.data(), command.size());
  std::string response = SendCommandAndWait(command);
  VLOG(2) << "In(" << response.size()
          << "):  " << base::HexEncode(response.data(), response.size());

  if (response.size() < sizeof(header)) {
    LOG(ERROR) << "TPM response was too short!";
    return -1;
  }

  // Unpack the response header and any output
  memcpy(&header, response.data(), sizeof(header));
  header.size = base::NetToHost32(header.size);
  header.code = base::NetToHost32(header.code);

  // Error of some sort?
  if (header.code) {
    if ((header.code & kVendorRcErr) == kVendorRcErr) {
      LOG(WARNING) << "TPM error code 0x" << std::hex << header.code;
    }
  }

  // Pass back any reply beyond the header
  *output = response.substr(sizeof(header));

  return header.code;
}

namespace {

template <typename Request>
std::string RequestToString(const Request& req) {
  return std::string(reinterpret_cast<const char*>(&req), sizeof(req));
}

template <>
std::string RequestToString(const struct u2f_attest_req& req) {
  return std::string(reinterpret_cast<const char*>(&req),
                     offsetof(struct u2f_attest_req, data) + req.dataLen);
}

}  // namespace

template <typename Request, typename Response>
uint32_t TpmVendorCommandProxy::VendorCommandStruct(uint16_t cc,
                                                    const Request& input,
                                                    Response* output) {
  std::string output_str;
  uint32_t resp_code = VendorCommand(cc, RequestToString(input), &output_str);

  if (resp_code == 0) {
    if (output_str.size() == sizeof(*output)) {
      memcpy(output, output_str.data(), sizeof(*output));
    } else {
      LOG(ERROR) << "Invalid response size for successful vendor command, "
                 << "expected: " << sizeof(*output)
                 << ", actual: " << output_str.size();
      return kVendorRcInvalidResponse;
    }
  }

  return resp_code;
}

uint32_t TpmVendorCommandProxy::SendU2fApdu(const std::string& req,
                                            std::string* resp_out) {
  return VendorCommand(kVendorCcU2fApdu, req, resp_out);
}

uint32_t TpmVendorCommandProxy::SendU2fGenerate(
    const struct u2f_generate_req& req, struct u2f_generate_resp* resp_out) {
  return VendorCommandStruct(kVendorCcU2fGenerate, req, resp_out);
}

uint32_t TpmVendorCommandProxy::SendU2fSign(const struct u2f_sign_req& req,
                                            struct u2f_sign_resp* resp_out) {
  std::string output_str;
  uint32_t resp_code =
      VendorCommand(kVendorCcU2fSign, RequestToString(req), &output_str);

  if (resp_code == 0) {
    // A success response may or may not have a body, depending on whether the
    // request was a full sign request, or simply a 'check only' request, to
    // test ownership of the specified key handle.
    if (req.flags == U2F_AUTH_CHECK_ONLY && output_str.size() == 0) {
      // We asked to test ownership of a key handle; success response code
      // indicates it is owned. No response body expected.
      return resp_code;
    } else if (output_str.size() == sizeof(struct u2f_sign_resp)) {
      DCHECK(resp_out);  // It is a programming error for this to fail.
      memcpy(resp_out, output_str.data(), sizeof(*resp_out));
    } else {
      LOG(ERROR) << "Invalid response size for successful vendor command, "
                 << "expected: "
                 << (resp_out ? sizeof(struct u2f_sign_resp) : 0)
                 << ", actual: " << output_str.size();
      return kVendorRcInvalidResponse;
    }
  }

  return resp_code;
}

uint32_t TpmVendorCommandProxy::SendU2fAttest(
    const struct u2f_attest_req& req, struct u2f_attest_resp* resp_out) {
  return VendorCommandStruct(kVendorCcU2fAttest, req, resp_out);
}

uint32_t TpmVendorCommandProxy::GetG2fCertificate(std::string* cert_out) {
  constexpr std::array<uint8_t, 0x23> kCertRequest{
      0x80, 0x02,                    // TPM_ST_SESSIONS
      0x00, 0x00, 0x00, 0x23,        // size
      0x00, 0x00, 0x01, 0x4e,        // TPM_CC_NV_READ
      0x01, 0x3f, 0xff, 0x02,        // authHandle : TPMI_RH_NV_AUTH
      0x01, 0x3f, 0xff, 0x02,        // nvIndex    : TPMI_RH_NV_INDEX
      0x00, 0x00, 0x00, 0x09,        // authorizationSize : UINT32
      0x40, 0x00, 0x00, 0x09,        // sessionHandle : empty password
      0x00, 0x00, 0x00, 0x00, 0x00,  // nonce, sessionAttributes, hmac
      0x01, 0x3b,                    // nvSize   : UINT16
      0x00, 0x00                     // nvOffset : UINT16
  };

  constexpr std::array<uint8_t, 16> kExpectedCertResponseHeader{
      0x80, 0x02,              // TPM_ST_SESSIONS
      0x00, 0x00, 0x01, 0x50,  // responseSize
      0x00, 0x00, 0x00, 0x00,  // responseCode : TPM_RC_SUCCESS
      0x00, 0x00, 0x01, 0x3d,  // parameterSize
      0x01, 0x3b,              // TPM2B_MAX_NV_BUFFER : size
  };

  constexpr int kCertSize = 0x013b;
  constexpr int kTpmResponseHeaderSize = 10;
  constexpr int kExpectedCertResponseSize = 0x0150;

  std::string req(kCertRequest.begin(), kCertRequest.end());

  VLOG(2) << "Out(" << req.size()
          << "): " << base::HexEncode(req.data(), req.size());

  std::string resp = SendCommandAndWait(req);

  VLOG(2) << "In(" << resp.size()
          << "):  " << base::HexEncode(resp.data(), resp.size());

  if (resp.size() < kTpmResponseHeaderSize) {
    return kVendorRcInvalidResponse;
  }

  // TODO(louiscollard): This, in a less horrible way.
  if (resp.size() != kExpectedCertResponseSize ||
      resp.compare(0, 16,
                   std::string(kExpectedCertResponseHeader.begin(),
                               kExpectedCertResponseHeader.end())) != 0) {
    return base::NetToHost32(
        *reinterpret_cast<const uint32_t*>(&resp.data()[6]));
  }

  *cert_out = resp.substr(kExpectedCertResponseHeader.size(), kCertSize);

  return 0;
}

void TpmVendorCommandProxy::LogIndividualCertificate() {
  std::string cert;

  uint32_t cert_status = GetG2fCertificate(&cert);

  if (cert_status) {
    VLOG(1) << "Failed to retrieve G2F certificate: " << std::hex
            << cert_status;
  } else {
    VLOG(1) << "Certificate: " << base::HexEncode(cert.data(), cert.size());
  }
}

}  // namespace u2f
