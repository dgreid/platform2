// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Client for cert_provision library.

#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/syslog_logging.h>

#include "cryptohome/cert_provision.h"

void ProgressCallback(cert_provision::Status status,
                      int progress,
                      const std::string& message) {
  LOG(INFO) << "ProgressCallback: " << static_cast<int>(status) << ", "
            << progress << "%: " << message;
}

void PrintHelp() {
  printf("Usage: cert_provision_client <command> [--v=<log_verbosity>]\n");
  printf("Commands:\n");
  printf("  Provision a certificate:\n");
  printf("  --provision --label=<label> --pca=<type> --profile=<profile>\n");
  printf("    where type: default, test\n");
  printf("          profile: cast, jetstream\n");
  printf("  Force enroll:\n");
  printf("  --enroll --pca=<type>\n");
  printf("  Print the provisioned certificate:\n");
  printf("  --get --label=<label> --include_chain\n");
  printf("        [--out=<file_out>]\n");
  printf("  Sign using the provisioned certificate:\n");
  printf("  --sign --label=<label> --in=<file_in> [--out=<file_out>]\n");
  printf("         --mechanism=<mechanism>\n");
  printf("  where mechanism: sha1_rsa, sha256_rsa, sha256_rsa_pss\n");
}

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  if (cl->HasSwitch("h") || cl->HasSwitch("help")) {
    PrintHelp();
    return 2;
  }

  cert_provision::Status sts;
  if (cl->HasSwitch("provision")) {
    std::string cert_label = cl->GetSwitchValueASCII("label");
    if (cert_label.empty()) {
      PrintHelp();
      return 2;
    }

    cert_provision::PCAType pca_type;
    std::string pca = cl->GetSwitchValueASCII("pca");
    if (pca == "default") {
      pca_type = cert_provision::PCAType::kDefaultPCA;
    } else if (pca == "test") {
      pca_type = cert_provision::PCAType::kTestPCA;
    } else {
      PrintHelp();
      return 2;
    }

    cert_provision::CertificateProfile cert_profile;
    std::string profile = cl->GetSwitchValueASCII("profile");
    if (profile == "cast") {
      cert_profile = cert_provision::CertificateProfile::CAST_CERTIFICATE;
    } else if (profile == "jetstream") {
      cert_profile = cert_provision::CertificateProfile::JETSTREAM_CERTIFICATE;
    } else {
      PrintHelp();
      return 2;
    }

    sts = cert_provision::ProvisionCertificate(
        pca_type, cert_label, cert_profile, base::Bind(&ProgressCallback));
    if (sts != cert_provision::Status::Success) {
      LOG(ERROR) << "ProvisionCertificate returned " << static_cast<int>(sts);
      return 3;
    }
    VLOG(1) << "ProvisionCertificate returned " << static_cast<int>(sts);
  } else if (cl->HasSwitch("enroll")) {
    cert_provision::PCAType pca_type;
    std::string pca = cl->GetSwitchValueASCII("pca");
    if (pca == "default") {
      pca_type = cert_provision::PCAType::kDefaultPCA;
    } else if (pca == "test") {
      pca_type = cert_provision::PCAType::kTestPCA;
    } else {
      PrintHelp();
      return 2;
    }

    sts = cert_provision::ForceEnroll(pca_type, base::Bind(&ProgressCallback));
    if (sts != cert_provision::Status::Success) {
      LOG(ERROR) << "ForceEnroll returned " << static_cast<int>(sts);
      return 3;
    }
    VLOG(1) << "ForceEnroll returned " << static_cast<int>(sts);
  } else if (cl->HasSwitch("get")) {
    std::string cert_label = cl->GetSwitchValueASCII("label");
    if (cert_label.empty()) {
      PrintHelp();
      return 2;
    }

    std::string certificate;
    sts = cert_provision::GetCertificate(
        cert_label, cl->HasSwitch("include_chain"), &certificate);
    if (sts != cert_provision::Status::Success) {
      LOG(ERROR) << "GetCertificate returned " << static_cast<int>(sts);
      return 3;
    }
    VLOG(1) << "GetCertificate returned " << static_cast<int>(sts);
    base::FilePath out(cl->GetSwitchValueASCII("out"));
    if (!out.empty()) {
      if (base::WriteFile(out, certificate.data(), certificate.size()) < 0) {
        LOG(ERROR) << "Failed to write output file: " << out.value();
        return 1;
      }
    } else {
      puts(certificate.c_str());
    }
  } else if (cl->HasSwitch("sign")) {
    std::string cert_label = cl->GetSwitchValueASCII("label");
    if (cert_label.empty()) {
      PrintHelp();
      return 2;
    }

    base::FilePath in(cl->GetSwitchValueASCII("in"));
    if (in.empty()) {
      PrintHelp();
      return 2;
    }

    cert_provision::SignMechanism sign_mechanism;
    std::string mechanism = cl->GetSwitchValueASCII("mechanism");
    if (mechanism == "sha1_rsa") {
      sign_mechanism = cert_provision::SHA1_RSA_PKCS;
    } else if (mechanism == "sha256_rsa") {
      sign_mechanism = cert_provision::SHA256_RSA_PKCS;
    } else if (mechanism == "sha256_rsa_pss") {
      sign_mechanism = cert_provision::SHA256_RSA_PSS;
    } else {
      PrintHelp();
      return 2;
    }

    std::string data;
    if (!base::ReadFileToString(in, &data)) {
      LOG(ERROR) << "Failed to read input file: " << in.value();
      return 1;
    }
    std::string sig;
    sts = cert_provision::Sign(cert_label, sign_mechanism, data, &sig);
    if (sts != cert_provision::Status::Success) {
      LOG(ERROR) << "Sign returned " << static_cast<int>(sts);
      return 3;
    }
    VLOG(1) << "Sign returned " << static_cast<int>(sts);
    base::FilePath out(cl->GetSwitchValueASCII("out"));
    if (!out.empty()) {
      if (base::WriteFile(out, sig.data(), sig.size()) < 0) {
        LOG(ERROR) << "Failed to write output file: " << out.value();
        return 1;
      }
    } else {
      puts(base::HexEncode(sig.data(), sig.size()).c_str());
    }
  }
  return 0;
}
