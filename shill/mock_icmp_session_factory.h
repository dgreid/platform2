// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_ICMP_SESSION_FACTORY_H_
#define SHILL_MOCK_ICMP_SESSION_FACTORY_H_

#include <memory>

#include <base/macros.h>
#include <base/no_destructor.h>
#include <gmock/gmock.h>

#include "shill/icmp_session_factory.h"

namespace shill {

class MockIcmpSessionFactory : public IcmpSessionFactory {
 public:
  ~MockIcmpSessionFactory() override;

  // This is a singleton. Use MockIcmpSessionFactory::GetInstance()->Foo().
  static MockIcmpSessionFactory* GetInstance();

  MOCK_METHOD(std::unique_ptr<IcmpSession>,
              CreateIcmpSession,
              (EventDispatcher*),
              (override));

 protected:
  MockIcmpSessionFactory();
  MockIcmpSessionFactory(const MockIcmpSessionFactory&) = delete;
  MockIcmpSessionFactory& operator=(const MockIcmpSessionFactory&) = delete;

 private:
  friend class base::NoDestructor<MockIcmpSessionFactory>;
};

}  // namespace shill

#endif  // SHILL_MOCK_ICMP_SESSION_FACTORY_H_
