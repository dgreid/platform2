// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "patchpanel/counters_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/firewall.h"
#include "patchpanel/minijailed_process_runner.h"

namespace patchpanel {
namespace {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
  }
  base::AtExitManager at_exit;
};

class RandomProcessRunner : public MinijailedProcessRunner {
 public:
  explicit RandomProcessRunner(FuzzedDataProvider* data_provider)
      : data_provider_{data_provider} {}
  RandomProcessRunner(const RandomProcessRunner&) = delete;
  RandomProcessRunner& operator=(const RandomProcessRunner&) = delete;
  ~RandomProcessRunner() = default;

  int RunSync(const std::vector<std::string>& argv,
              bool log_failures,
              std::string* output) override {
    if (output) {
      *output = data_provider_->ConsumeRandomLengthString(10000);
    }
    return data_provider_->ConsumeBool();
  }

 private:
  FuzzedDataProvider* data_provider_;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider provider(data, size);
  RandomProcessRunner runner(&provider);
  Firewall firewall;
  Datapath datapath(&runner, &firewall);
  CountersService counters_svc(&datapath, &runner);

  while (provider.remaining_bytes() > 0) {
    counters_svc.GetCounters({});
  }

  return 0;
}

}  // namespace
}  // namespace patchpanel
