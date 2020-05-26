// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <tuple>

#include <base/barrier_closure.h>
#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/wilco_dtc_supportd/ec_constants.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/ec_event_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/ec_event_test_utils.h"
#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

namespace {

using testing::_;
using testing::Invoke;
using testing::StrictMock;

using EcEvent = EcEventService::EcEvent;
using EcEventReason = EcEventService::EcEvent::Reason;
using MojoEvent = chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdEvent;

// Tests for EcEvent.
//
// This is a parametrized test with the following parameters:
// * |source_ec_event| - the ec event subject to test.
// * |expected_event_reason| - the expected reason of the EC event.
class EcEventTest
    : public testing::Test,
      public testing::WithParamInterface<std::tuple<EcEvent, EcEventReason>> {
 protected:
  const EcEvent& source_ec_event() const { return std::get<0>(GetParam()); }

  EcEventReason expected_event_reason() const {
    return std::get<1>(GetParam());
  }
};

// Tests that |EcEvent::GetReason| correctly extracts reason from the EC event.
TEST_P(EcEventTest, GetReason) {
  EXPECT_EQ(source_ec_event().GetReason(), expected_event_reason());
}

INSTANTIATE_TEST_CASE_P(
    _,
    EcEventTest,
    testing::Values(
        std::make_tuple(kEcEventNonWilcoCharger,
                        EcEventReason::kNonWilcoCharger),
        std::make_tuple(kEcEventBatteryAuth, EcEventReason::kBatteryAuth),
        std::make_tuple(kEcEventDockDisplay, EcEventReason::kDockDisplay),
        std::make_tuple(kEcEventDockThunderbolt,
                        EcEventReason::kDockThunderbolt),
        std::make_tuple(kEcEventIncompatibleDock,
                        EcEventReason::kIncompatibleDock),
        std::make_tuple(kEcEventDockError, EcEventReason::kDockError),
        std::make_tuple(kEcEventNonSysNotification,
                        EcEventReason::kNonSysNotification),
        std::make_tuple(kEcEventAcAdapterNoFlags,
                        EcEventReason::kSysNotification),
        std::make_tuple(kEcEventChargerNoFlags,
                        EcEventReason::kSysNotification),
        std::make_tuple(kEcEventUsbCNoFlags, EcEventReason::kSysNotification),
        std::make_tuple(kEcEventNonWilcoChargerBadSubType,
                        EcEventReason::kSysNotification)));

class MockEcEventServiceObserver : public EcEventService::Observer {
 public:
  MOCK_METHOD(void, OnEcEvent, (const EcEvent&), (override));
};

class EcEventServiceTest : public testing::Test {
 protected:
  EcEventServiceTest() = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    service()->set_root_dir_for_testing(temp_dir_.GetPath());
    service()->set_event_fd_events_for_testing(POLLIN);
    service()->AddObserver(&observer_);
    EXPECT_TRUE(service()->HasObserver(&observer_));
  }

  void TearDown() override {
    service()->RemoveObserver(&observer_);
    EXPECT_FALSE(service()->HasObserver(&observer_));
    base::RunLoop run_loop;
    service_.ShutDown(run_loop.QuitClosure());
    run_loop.Run();
  }

  void CreateEcEventFile() {
    base::FilePath file_path = ec_event_file_path();
    ASSERT_TRUE(base::CreateDirectory(file_path.DirName()));
    ASSERT_EQ(mkfifo(file_path.value().c_str(), 0600), 0);
  }

  base::FilePath ec_event_file_path() {
    return temp_dir_.GetPath().Append(kEcEventFilePath);
  }

  // Must be open only after |service_.Start()| call. Otherwise, it will
  // block thread.
  void InitFifoWriteEnd() {
    ASSERT_EQ(fifo_write_end_.get(), -1);
    fifo_write_end_.reset(open(ec_event_file_path().value().c_str(), O_WRONLY));
    ASSERT_NE(fifo_write_end_.get(), -1);
  }

  void EmitEcEventAndSetObserverExpectations(
      const EcEvent& ec_event, const base::RepeatingClosure& callback) {
    ASSERT_EQ(write(fifo_write_end_.get(), &ec_event, sizeof(ec_event)),
              sizeof(ec_event));

    EXPECT_CALL(observer_, OnEcEvent(ec_event))
        .WillOnce(
            Invoke([callback](const EcEvent& ec_event) { callback.Run(); }));
  }

  EcEventService* service() { return &service_; }

 private:
  base::MessageLoop message_loop_;
  StrictMock<MockEcEventServiceObserver> observer_;
  EcEventService service_;

  base::ScopedTempDir temp_dir_;

  base::ScopedFD fifo_write_end_;
};

TEST_F(EcEventServiceTest, Start) {
  CreateEcEventFile();
  ASSERT_TRUE(service()->Start());
}

TEST_F(EcEventServiceTest, StartFailure) {
  ASSERT_FALSE(service()->Start());
}

// Tests for the EcEventService class that started successfully.
class StartedEcEventServiceTest : public EcEventServiceTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(EcEventServiceTest::SetUp());
    CreateEcEventFile();
    ASSERT_TRUE(service()->Start());
    InitFifoWriteEnd();
  }
};

TEST_F(StartedEcEventServiceTest, ReadEvent) {
  base::RunLoop run_loop;
  const uint16_t data[] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd, 0xeeee, 0xffff};
  EmitEcEventAndSetObserverExpectations(
      EcEvent(0x8888, static_cast<EcEvent::Type>(0x9999), data),
      run_loop.QuitClosure());
  run_loop.Run();
}

TEST_F(StartedEcEventServiceTest, ReadManyEvent) {
  base::RunLoop run_loop;
  base::RepeatingClosure callback = base::BarrierClosure(
      2 /* num_closures */, run_loop.QuitClosure() /* done closure */);
  const uint16_t data1[] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd, 0xeeee, 0xffff};
  EmitEcEventAndSetObserverExpectations(
      EcEvent(0x8888, static_cast<EcEvent::Type>(0x9999), data1), callback);
  const uint16_t data2[] = {0x0000, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555};
  EmitEcEventAndSetObserverExpectations(
      EcEvent(0x6666, static_cast<EcEvent::Type>(0x7777), data2), callback);
  run_loop.Run();
}

}  // namespace

}  // namespace diagnostics
