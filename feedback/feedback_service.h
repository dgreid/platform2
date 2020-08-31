// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEEDBACK_FEEDBACK_SERVICE_H_
#define FEEDBACK_FEEDBACK_SERVICE_H_

#include "dbus/exported_object.h"

#include <string>

namespace userfeedback {
class ExtensionSubmit;
}

namespace feedback {

class FeedbackUploader;

class FeedbackService : public base::RefCounted<FeedbackService> {
 public:
  explicit FeedbackService(feedback::FeedbackUploader* uploader);
  virtual ~FeedbackService();

  // Send the given report to the server |uploader_| is configured for.
  // The callback will be called with
  void SendFeedback(
      const userfeedback::ExtensionSubmit& feedback,
      base::OnceCallback<void(bool, const std::string&)> callback);

  void QueueExistingReport(const std::string& data);

 private:
  feedback::FeedbackUploader* uploader_;

  DISALLOW_COPY_AND_ASSIGN(FeedbackService);
};

class DBusFeedbackServiceImpl : public FeedbackService {
 public:
  explicit DBusFeedbackServiceImpl(feedback::FeedbackUploader* uploader);
  virtual ~DBusFeedbackServiceImpl();

  bool Start(dbus::Bus* bus);

 private:
  void DBusSendFeedback(dbus::MethodCall* method_call,
                        dbus::ExportedObject::ResponseSender sender);

  void DBusFeedbackSent(dbus::MethodCall* method_call,
                        dbus::ExportedObject::ResponseSender sender,
                        bool status, const std::string& message);

  DISALLOW_COPY_AND_ASSIGN(DBusFeedbackServiceImpl);
};

}  // namespace feedback

#endif  // FEEDBACK_FEEDBACK_SERVICE_H_
