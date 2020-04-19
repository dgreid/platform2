// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/message_loop/message_loop.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <base/strings/string_piece.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_ptr.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <mojo/public/cpp/system/buffer.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/wilco_dtc_supportd/mock_mojo_client.h"
#include "diagnostics/wilco_dtc_supportd/mojo_service.h"

#include "mojo/wilco_dtc_supportd.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::WithArg;

using MojomWilcoDtcSupportdClient =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClient;
using MojomWilcoDtcSupportdClientPtr =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClientPtr;
using MojomWilcoDtcSupportdServiceRequest =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceRequest;
using MojomWilcoDtcSupportdWebRequestStatus =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdWebRequestStatus;
using MojomWilcoDtcSupportdWebRequestHttpMethod =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdWebRequestHttpMethod;

namespace diagnostics {

namespace {

class MockMojoServiceDelegate : public MojoService::Delegate {
 public:
  MOCK_METHOD(void,
              SendGrpcUiMessageToWilcoDtc,
              (base::StringPiece, const SendGrpcUiMessageToWilcoDtcCallback&),
              (override));
  MOCK_METHOD(void, NotifyConfigurationDataChangedToWilcoDtc, (), (override));
};

// Tests for the MojoService class.
class MojoServiceTest : public testing::Test {
 protected:
  MojoServiceTest() {
    mojo::core::Init();
    // Obtain Mojo interface pointer that talks to |mojo_client_| - the
    // connection between them will be maintained by |mojo_client_binding_|.
    MojomWilcoDtcSupportdClientPtr mojo_client_interface_ptr;
    mojo_client_binding_ =
        std::make_unique<mojo::Binding<MojomWilcoDtcSupportdClient>>(
            &mojo_client_, mojo::MakeRequest(&mojo_client_interface_ptr));
    DCHECK(mojo_client_interface_ptr);

    mojo_service_ = std::make_unique<MojoService>(
        &delegate_,
        MojomWilcoDtcSupportdServiceRequest() /* self_interface_request */,
        std::move(mojo_client_interface_ptr));
  }

  MockMojoServiceDelegate* delegate() { return &delegate_; }
  MockMojoClient* mojo_client() { return &mojo_client_; }
  MojoService* mojo_service() { return mojo_service_.get(); }

 private:
  base::MessageLoop message_loop_;

  StrictMock<MockMojoClient> mojo_client_;
  std::unique_ptr<mojo::Binding<MojomWilcoDtcSupportdClient>>
      mojo_client_binding_;

  StrictMock<MockMojoServiceDelegate> delegate_;
  std::unique_ptr<MojoService> mojo_service_;
};

TEST_F(MojoServiceTest, SendUiMessageToWilcoDtc) {
  constexpr base::StringPiece kJsonMessageToWilcoDtc("{\"message\": \"ping\"}");
  constexpr base::StringPiece kJsonMessageFromWilcoDtc(
      "{\"message\": \"pong\"}");

  EXPECT_CALL(*delegate(),
              SendGrpcUiMessageToWilcoDtc(kJsonMessageToWilcoDtc, _))
      .WillOnce(WithArg<1>(
          Invoke([kJsonMessageFromWilcoDtc](
                     const base::Callback<void(std::string)>& callback) {
            callback.Run(kJsonMessageFromWilcoDtc.as_string());
          })));

  base::RunLoop run_loop;
  mojo_service()->SendUiMessageToWilcoDtc(
      CreateReadOnlySharedMemoryMojoHandle(kJsonMessageToWilcoDtc),
      base::Bind(
          [](const base::Closure& quit_closure,
             base::StringPiece expected_json_message,
             mojo::ScopedHandle json_message_handle) {
            ASSERT_TRUE(json_message_handle.is_valid());
            std::unique_ptr<base::SharedMemory> json_message_shm =
                GetReadOnlySharedMemoryFromMojoHandle(
                    std::move(json_message_handle));
            ASSERT_TRUE(json_message_shm);
            const std::string json_message = std::string(
                static_cast<const char*>(json_message_shm->memory()),
                json_message_shm->mapped_size());

            EXPECT_EQ(json_message, expected_json_message);
            quit_closure.Run();
          },
          run_loop.QuitClosure(), kJsonMessageFromWilcoDtc));
  run_loop.Run();
}

TEST_F(MojoServiceTest, SendUiMessageToWilcoDtcInvalidJSON) {
  constexpr base::StringPiece kJsonMessage("{'message': 'Hello world!'}");

  base::RunLoop run_loop;
  mojo_service()->SendUiMessageToWilcoDtc(
      CreateReadOnlySharedMemoryMojoHandle(kJsonMessage),
      base::Bind(
          [](const base::Closure& quit_closure,
             mojo::ScopedHandle json_message_handle) {
            EXPECT_FALSE(json_message_handle.is_valid());
            quit_closure.Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(MojoServiceTest, SendWilcoDtcMessageToUi) {
  constexpr base::StringPiece kJsonMessageToUi("{\"message\": \"ping\"}");
  constexpr base::StringPiece kJsonMessageFromUi("{\"message\": \"pong\"}");

  EXPECT_CALL(*mojo_client(),
              SendWilcoDtcMessageToUiImpl(kJsonMessageToUi.as_string(), _))
      .WillOnce(WithArg<1>(
          Invoke([kJsonMessageFromUi](
                     MockMojoClient::SendWilcoDtcMessageToUiCallback callback) {
            std::move(callback).Run(
                CreateReadOnlySharedMemoryMojoHandle(kJsonMessageFromUi));
          })));

  base::RunLoop run_loop;
  mojo_service()->SendWilcoDtcMessageToUi(
      kJsonMessageToUi.as_string(),
      base::Bind(
          [](const base::Closure& quit_closure,
             base::StringPiece expected_json_message,
             base::StringPiece json_message) {
            EXPECT_EQ(json_message, expected_json_message);
            quit_closure.Run();
          },
          run_loop.QuitClosure(), kJsonMessageFromUi));
  run_loop.Run();
}

TEST_F(MojoServiceTest, SendWilcoDtcMessageToUiEmptyMessage) {
  base::RunLoop run_loop;
  auto callback = base::Bind(
      [](const base::Closure& quit_closure, base::StringPiece json_message) {
        EXPECT_TRUE(json_message.empty());
        quit_closure.Run();
      },
      run_loop.QuitClosure());
  mojo_service()->SendWilcoDtcMessageToUi("", std::move(callback));
  run_loop.Run();
}

TEST_F(MojoServiceTest, PerformWebRequest) {
  constexpr auto kHttpMethod = MojomWilcoDtcSupportdWebRequestHttpMethod::kPost;
  constexpr char kHttpsUrl[] = "https://www.google.com";
  constexpr char kHeader1[] = "Accept-Language: en-US";
  constexpr char kHeader2[] = "Accept: text/html";
  constexpr char kBodyRequest[] = "<html>Request</html>";

  constexpr auto kWebRequestStatus = MojomWilcoDtcSupportdWebRequestStatus::kOk;
  constexpr int kHttpStatusOk = 200;
  constexpr char kBodyResponse[] = "<html>Response</html>";

  EXPECT_CALL(*mojo_client(), PerformWebRequestImpl(
                                  kHttpMethod, kHttpsUrl,
                                  std::vector<std::string>{kHeader1, kHeader2},
                                  kBodyRequest, _))
      .WillOnce(WithArg<4>(
          Invoke([kBodyResponse](
                     MockMojoClient::MojoPerformWebRequestCallback callback) {
            std::move(callback).Run(
                kWebRequestStatus, kHttpStatusOk,
                CreateReadOnlySharedMemoryMojoHandle(kBodyResponse));
          })));

  base::RunLoop run_loop;
  mojo_service()->PerformWebRequest(
      kHttpMethod, kHttpsUrl, {kHeader1, kHeader2}, kBodyRequest,
      base::Bind(
          [](const base::Closure& quit_closure,
             MojomWilcoDtcSupportdWebRequestStatus expected_status,
             int expected_http_status, std::string expected_response_body,
             MojomWilcoDtcSupportdWebRequestStatus status, int http_status,
             base::StringPiece response_body) {
            EXPECT_EQ(expected_status, status);
            EXPECT_EQ(expected_http_status, http_status);
            EXPECT_EQ(expected_response_body, response_body);
            quit_closure.Run();
          },
          run_loop.QuitClosure(), kWebRequestStatus, kHttpStatusOk,
          kBodyResponse));
  run_loop.Run();
}

TEST_F(MojoServiceTest, GetConfigurationData) {
  constexpr char kFakeJsonConfigurationData[] = "Fake JSON configuration data";

  EXPECT_CALL(*mojo_client(), GetConfigurationData(_))
      .WillOnce(WithArg<0>(
          Invoke([kFakeJsonConfigurationData](
                     base::OnceCallback<void(const std::string&)> callback) {
            std::move(callback).Run(kFakeJsonConfigurationData);
          })));

  base::RunLoop run_loop;
  mojo_service()->GetConfigurationData(base::Bind(
      [](const base::Closure& quit_closure, const std::string& expected_data,
         const std::string& json_configuration_data) {
        EXPECT_EQ(json_configuration_data, expected_data);
        quit_closure.Run();
      },
      run_loop.QuitClosure(), kFakeJsonConfigurationData));
  run_loop.Run();
}

TEST_F(MojoServiceTest, NotifyConfigurationDataChanged) {
  EXPECT_CALL(*delegate(), NotifyConfigurationDataChangedToWilcoDtc());
  mojo_service()->NotifyConfigurationDataChanged();
}

}  // namespace

}  // namespace diagnostics
