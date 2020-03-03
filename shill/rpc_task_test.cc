// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/rpc_task.h"

#include <gtest/gtest.h>

#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"

using std::map;
using std::string;

namespace shill {

class RpcTaskTest : public testing::Test, public RpcTaskDelegate {
 public:
  RpcTaskTest()
      : get_login_calls_(0), notify_calls_(0), task_(&control_, this) {}

  // Inherited from RpcTaskDelegate.
  virtual void GetLogin(string* user, string* password);
  virtual void Notify(const string& reason, const map<string, string>& dict);

 protected:
  int get_login_calls_;
  int notify_calls_;
  string* last_user_;
  string* last_password_;
  string last_notify_reason_;
  map<string, string> last_notify_dict_;
  MockControl control_;
  RpcTask task_;
};

void RpcTaskTest::GetLogin(string* user, string* password) {
  get_login_calls_++;
  last_user_ = user;
  last_password_ = password;
}

void RpcTaskTest::Notify(const string& reason,
                         const map<string, string>& dict) {
  notify_calls_++;
  last_notify_reason_ = reason;
  last_notify_dict_ = dict;
}

TEST_F(RpcTaskTest, GetEnvironment) {
  map<string, string> env = task_.GetEnvironment();
  ASSERT_EQ(2, env.size());
  EXPECT_EQ(env[kRpcTaskServiceVariable],
            RpcTaskMockAdaptor::kRpcConnId.value());
  EXPECT_EQ(env[kRpcTaskPathVariable], RpcTaskMockAdaptor::kRpcId.value());
}

TEST_F(RpcTaskTest, GetRpcIdentifiers) {
  EXPECT_EQ(RpcTaskMockAdaptor::kRpcId, task_.GetRpcIdentifier());
  EXPECT_EQ(RpcTaskMockAdaptor::kRpcConnId, task_.GetRpcConnectionIdentifier());
}

TEST_F(RpcTaskTest, GetLogin) {
  string user, password;
  task_.GetLogin(&user, &password);
  EXPECT_EQ(1, get_login_calls_);
  EXPECT_EQ(&user, last_user_);
  EXPECT_EQ(&password, last_password_);
}

TEST_F(RpcTaskTest, Notify) {
  static const char kReason[] = "up";
  map<string, string> dict;
  dict["foo"] = "bar";
  task_.Notify(kReason, dict);
  EXPECT_EQ(1, notify_calls_);
  EXPECT_EQ(kReason, last_notify_reason_);
  EXPECT_EQ("bar", last_notify_dict_["foo"]);
}

}  // namespace shill
