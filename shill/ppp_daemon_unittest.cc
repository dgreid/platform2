// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/external_task.h"
#include "shill/glib.h"
#include "shill/mock_control.h"
#include "shill/mock_glib.h"
#include "shill/ppp_daemon.h"
#include "shill/rpc_task.h"

namespace shill {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::Test;
using testing::WithArg;

class PPPDaemonTest : public Test, public RPCTaskDelegate {
 public:
  PPPDaemonTest() : weak_ptr_factory_(this) {}
  virtual ~PPPDaemonTest() {}

  std::unique_ptr<ExternalTask> Start(const PPPDaemon::Options &options,
                                      const std::string &device,
                                      Error *error) {
    PPPDaemon::DeathCallback callback(base::Bind(&PPPDaemonTest::DeathCallback,
                                                 base::Unretained(this)));
    return PPPDaemon::Start(&control_, &glib_, weak_ptr_factory_.GetWeakPtr(),
                            options, device, callback, error);
  }

  gboolean CaptureArgv(gchar **argv) {
    for (gchar **i = argv; *i != nullptr; ++i) {
      argv_.push_back(*i);
    }
    return TRUE;
  }

  MOCK_METHOD2(GetLogin, void(std::string *user, std::string *password));
  MOCK_METHOD2(Notify, void(const std::string &reason,
                            const std::map<std::string, std::string> &dict));

 protected:
  MockControl control_;
  MockGLib glib_;

  std::vector<std::string> argv_;
  base::WeakPtrFactory<PPPDaemonTest> weak_ptr_factory_;

  MOCK_METHOD2(DeathCallback, void(pid_t pid, int status));

 private:
  DISALLOW_COPY_AND_ASSIGN(PPPDaemonTest);
};

TEST_F(PPPDaemonTest, PluginUsed) {
  EXPECT_CALL(glib_, SpawnAsync(_, _, _, _, _, _, _, _))
      .WillOnce(WithArg<1>(Invoke(this, &PPPDaemonTest::CaptureArgv)));

  Error error;
  PPPDaemon::Options options;
  std::unique_ptr<ExternalTask> task(Start(options, "eth0", &error));

  for (size_t i = 0; i < argv_.size(); ++i) {
    if (argv_[i] == "plugin") {
      EXPECT_EQ(argv_[i + 1], PPPDaemon::kShimPluginPath);
    }
  }
}

TEST_F(PPPDaemonTest, OptionsConverted) {
  EXPECT_CALL(glib_, SpawnAsync(_, _, _, _, _, _, _, _))
      .WillOnce(WithArg<1>(Invoke(this, &PPPDaemonTest::CaptureArgv)));

  PPPDaemon::Options options;
  options.no_detach = true;
  options.no_default_route = true;
  options.use_peer_dns = true;
  options.lcp_echo_interval = 1;
  options.lcp_echo_failure = 1;

  Error error;
  std::unique_ptr<ExternalTask> task(Start(options, "eth0", &error));

  std::set<std::string> expected_arguments = {
    "nodetach", "nodefaultroute", "usepeerdns", "lcp-echo-interval",
    "lcp-echo-failure",
  };
  for (const auto &argument : argv_) {
    expected_arguments.erase(argument);
  }
  EXPECT_TRUE(expected_arguments.empty());
}

TEST_F(PPPDaemonTest, ErrorPropagated) {
  EXPECT_CALL(glib_, SpawnAsync(_, _, _, _, _, _, _, _))
      .WillOnce(Return(false));

  PPPDaemon::Options options;
  Error error;
  std::unique_ptr<ExternalTask> task(Start(options, "eth0", &error));

  EXPECT_NE(error.type(), Error::kSuccess);
  EXPECT_EQ(task.get(), nullptr);
}

}  // namespace shill
