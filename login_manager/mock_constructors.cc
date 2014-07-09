// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/message_loop/message_loop_proxy.h>
#include <base/time/time.h>

#include "login_manager/fake_generator_job.h"
#include "login_manager/mock_dbus_signal_emitter.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_key_generator.h"
#include "login_manager/mock_liveness_checker.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_mitigator.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_policy_store.h"
#include "login_manager/mock_process_manager_service.h"
#include "login_manager/mock_session_manager.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/mock_user_policy_service_factory.h"

// Per the gmock documentation, the vast majority of the time spent on
// compiling a mock class is in generating its constructor and
// destructor, as they perform non-trivial tasks.  To combat this, the
// docs recommend moving the definition of your mock class'
// constructor and destructor out of the class body and into a .cc
// file. This way, even if you #include your mock class in N files,
// the compiler only needs to generate its constructor and destructor
// once, resulting in a much faster compilation.
//
// To avoid having to add a bunch of boilerplate and a .cc file for every
// mock I define, I will simply collect the constructors all here.

namespace login_manager {
MockDevicePolicyService::MockDevicePolicyService()
    : DevicePolicyService(base::FilePath(), base::FilePath(), base::FilePath(),
                          scoped_ptr<PolicyStore>(),
                          NULL, NULL, NULL, NULL, NULL) {}
MockDevicePolicyService::~MockDevicePolicyService() {}

MockDBusSignalEmitter::MockDBusSignalEmitter() {}
MockDBusSignalEmitter::~MockDBusSignalEmitter() {}

MockFileChecker::MockFileChecker() : FileChecker(base::FilePath()) {}
MockFileChecker::~MockFileChecker() {}

MockKeyGenerator::MockKeyGenerator() : KeyGenerator(-1, NULL) {}
MockKeyGenerator::~MockKeyGenerator() {}

MockLivenessChecker::MockLivenessChecker() {}
MockLivenessChecker::~MockLivenessChecker() {}

MockMetrics::MockMetrics() : LoginMetrics(base::FilePath()) {}
MockMetrics::~MockMetrics() {}

MockMitigator::MockMitigator() {}
MockMitigator::~MockMitigator() {}

MockPolicyKey::MockPolicyKey() : PolicyKey(base::FilePath(), NULL) {}
MockPolicyKey::~MockPolicyKey() {}

MockPolicyService::MockPolicyService()
    : PolicyService(scoped_ptr<PolicyStore>(), NULL, NULL) {}
MockPolicyService::~MockPolicyService() {}

MockPolicyServiceCompletion::MockPolicyServiceCompletion() {}
MockPolicyServiceCompletion::~MockPolicyServiceCompletion() {}

MockPolicyServiceDelegate::MockPolicyServiceDelegate() {}
MockPolicyServiceDelegate::~MockPolicyServiceDelegate() {}

MockPolicyStore::MockPolicyStore() : PolicyStore(base::FilePath()) {}
MockPolicyStore::~MockPolicyStore() {}

MockProcessManagerService::MockProcessManagerService() {}
MockProcessManagerService::~MockProcessManagerService() {}

MockSessionManager::MockSessionManager() {}
MockSessionManager::~MockSessionManager() {}

MockUserPolicyServiceFactory::MockUserPolicyServiceFactory()
    : UserPolicyServiceFactory(0, NULL, NULL, NULL) {}
MockUserPolicyServiceFactory::~MockUserPolicyServiceFactory() {}

}  // namespace login_manager
