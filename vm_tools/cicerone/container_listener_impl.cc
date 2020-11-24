// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/container_listener_impl.h"

#include <arpa/inet.h>
#include <inttypes.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <vm_applications/proto_bindings/apps.pb.h>
#include <vm_cicerone/proto_bindings/cicerone_service.pb.h>

#include "vm_tools/cicerone/service.h"

namespace {
// These rate limit settings ensure that calls that open a new window/tab can't
// be made more than 10 times in a 15 second interval approximately.
constexpr base::TimeDelta kOpenRateWindow = base::TimeDelta::FromSeconds(15);
constexpr uint32_t kOpenRateLimit = 10;
}  // namespace

namespace vm_tools {
namespace cicerone {

ContainerListenerImpl::ContainerListenerImpl(
    base::WeakPtr<vm_tools::cicerone::Service> service)
    : service_(service),
      task_runner_(base::ThreadTaskRunnerHandle::Get()),
      open_count_(0),
      open_rate_window_start_(base::TimeTicks::Now()) {}

void ContainerListenerImpl::OverridePeerAddressForTesting(
    const std::string& testing_peer_address) {
  base::AutoLock lock_scope(testing_peer_address_lock_);
  testing_peer_address_ = testing_peer_address;
}

grpc::Status ContainerListenerImpl::ContainerReady(
    grpc::ServerContext* ctx,
    const vm_tools::container::ContainerStartupInfo* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  // Plugin VMs (i.e. containerless) can call this, so allow a zero value CID.
  bool result = false;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::ContainerStartupCompleted,
                 service_, request->token(), cid, request->garcon_port(),
                 &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Received ContainerReady but could not find matching VM: "
               << ctx->peer();
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Cannot find VM for ContainerListener");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::ContainerShutdown(
    grpc::ServerContext* ctx,
    const vm_tools::container::ContainerShutdownInfo* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }

  if (request->token().empty()) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "`token` cannot be empty");
  }

  // Calls coming from garcon should not be trusted to set container_name and
  // must use container_token.
  std::string container_name = "";
  bool result = false;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::ContainerShutdown, service_,
                 container_name, request->token(), cid, &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Received ContainerShutdown but could not find matching VM: "
               << ctx->peer();
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Cannot find VM for ContainerListener");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::PendingUpdateApplicationListCalls(
    grpc::ServerContext* ctx,
    const vm_tools::container::PendingAppListUpdateCount* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }

  if (request->token().empty()) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "`token` cannot be empty");
  }

  bool result = false;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(
          &vm_tools::cicerone::Service::PendingUpdateApplicationListCalls,
          service_, request->token(), cid, request->count(), &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Received ContainerShutdown but could not find matching VM: "
               << ctx->peer();
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Cannot find VM for ContainerListener");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::UpdateApplicationList(
    grpc::ServerContext* ctx,
    const vm_tools::container::UpdateApplicationListRequest* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  // Plugin VMs (i.e. containerless) can call this, so allow a zero value CID.
  vm_tools::apps::ApplicationList app_list;
  // vm_name and container_name are set in the UpdateApplicationList call but
  // we need to copy everything else out of the incoming protobuf here.
  for (const auto& app_in : request->application()) {
    auto app_out = app_list.add_apps();
    // Set the non-repeating fields first.
    app_out->set_desktop_file_id(app_in.desktop_file_id());
    app_out->set_no_display(app_in.no_display());
    app_out->set_startup_wm_class(app_in.startup_wm_class());
    app_out->set_startup_notify(app_in.startup_notify());
    app_out->set_package_id(app_in.package_id());
    app_out->set_exec(app_in.exec());
    app_out->set_executable_file_name(app_in.executable_file_name());
    // Set the mime types.
    for (const auto& mime_type : app_in.mime_types()) {
      app_out->add_mime_types(mime_type);
    }
    // Set the names, comments & keywords.
    if (app_in.has_name()) {
      auto name_out = app_out->mutable_name();
      for (const auto& names : app_in.name().values()) {
        auto curr_name = name_out->add_values();
        curr_name->set_locale(names.locale());
        curr_name->set_value(names.value());
      }
    }
    if (app_in.has_comment()) {
      auto comment_out = app_out->mutable_comment();
      for (const auto& comments : app_in.comment().values()) {
        auto curr_comment = comment_out->add_values();
        curr_comment->set_locale(comments.locale());
        curr_comment->set_value(comments.value());
      }
    }
    if (app_in.has_keywords()) {
      auto keywords_out = app_out->mutable_keywords();
      for (const auto& keyword : app_in.keywords().values()) {
        auto curr_keywords = keywords_out->add_values();
        curr_keywords->set_locale(keyword.locale());
        for (const auto& curr_value : keyword.value()) {
          curr_keywords->add_value(curr_value);
        }
      }
    }
    // Set the extensions.
    for (const auto& extension : app_in.extensions()) {
      app_out->add_extensions(extension);
    }
  }
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::UpdateApplicationList, service_,
                 request->token(), cid, &app_list, &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure updating application list from ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failure in UpdateApplicationList");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::OpenUrl(
    grpc::ServerContext* ctx,
    const vm_tools::container::OpenUrlRequest* request,
    vm_tools::EmptyMessage* response) {
  // Check on rate limiting before we process this.
  if (!CheckOpenRateLimit()) {
    return grpc::Status(grpc::RESOURCE_EXHAUSTED,
                        "OpenUrl rate limit exceeded, blocking request");
  }
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  // Plugin VMs (i.e. containerless) can call this, so allow a zero value CID.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::OpenUrl, service_,
                 request->token(), request->url(), cid, &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure opening URL from ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION, "Failure in OpenUrl");
  }
  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::InstallLinuxPackageProgress(
    grpc::ServerContext* ctx,
    const vm_tools::container::InstallLinuxPackageProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }
  InstallLinuxPackageProgressSignal progress_signal;
  if (!InstallLinuxPackageProgressSignal::Status_IsValid(
          static_cast<int>(request->status()))) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Invalid status field in protobuf request");
  }
  progress_signal.set_status(
      static_cast<InstallLinuxPackageProgressSignal::Status>(
          request->status()));
  progress_signal.set_progress_percent(request->progress_percent());
  progress_signal.set_failure_details(request->failure_details());
  progress_signal.set_command_uuid(request->command_uuid());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::InstallLinuxPackageProgress,
                 service_, request->token(), cid, &progress_signal, &result,
                 &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure updating Linux package install progress from "
                  "ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failure in InstallLinuxPackageProgress");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::UninstallPackageProgress(
    grpc::ServerContext* ctx,
    const vm_tools::container::UninstallPackageProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }
  UninstallPackageProgressSignal progress_signal;
  switch (request->status()) {
    case vm_tools::container::UninstallPackageProgressInfo::SUCCEEDED:
      progress_signal.set_status(UninstallPackageProgressSignal::SUCCEEDED);
      break;
    case vm_tools::container::UninstallPackageProgressInfo::FAILED:
      progress_signal.set_status(UninstallPackageProgressSignal::FAILED);
      progress_signal.set_failure_details(request->failure_details());
      break;
    case vm_tools::container::UninstallPackageProgressInfo::UNINSTALLING:
      progress_signal.set_status(UninstallPackageProgressSignal::UNINSTALLING);
      progress_signal.set_progress_percent(request->progress_percent());
      break;
    default:
      return grpc::Status(grpc::FAILED_PRECONDITION,
                          "Invalid status field in protobuf request");
  }
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::UninstallPackageProgress,
                 service_, request->token(), cid, &progress_signal, &result,
                 &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure updating Linux package uninstall progress from "
                  "ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failure in UninstallPackageProgress");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::ApplyAnsiblePlaybookProgress(
    grpc::ServerContext* ctx,
    const vm_tools::container::ApplyAnsiblePlaybookProgressInfo* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }
  ApplyAnsiblePlaybookProgressSignal progress_signal;
  if (!ApplyAnsiblePlaybookProgressSignal::Status_IsValid(
          static_cast<int>(request->status()))) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Invalid status field in protobuf request");
  }
  progress_signal.set_status(
      static_cast<ApplyAnsiblePlaybookProgressSignal::Status>(
          request->status()));
  progress_signal.set_failure_details(request->failure_details());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::ApplyAnsiblePlaybookProgress,
                 service_, request->token(), cid, &progress_signal, &result,
                 &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure updating Ansible playbook application progress from "
                  "ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failure in ApplyAnsiblePlaybookProgress");
  }

  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::OpenTerminal(
    grpc::ServerContext* ctx,
    const vm_tools::container::OpenTerminalRequest* request,
    vm_tools::EmptyMessage* response) {
  // Check on rate limiting before we process this.
  if (!CheckOpenRateLimit()) {
    return grpc::Status(grpc::RESOURCE_EXHAUSTED,
                        "OpenTerminal rate limit exceeded, blocking request");
  }
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }
  vm_tools::apps::TerminalParams terminal_params;
  terminal_params.mutable_params()->CopyFrom(request->params());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&vm_tools::cicerone::Service::OpenTerminal,
                            service_, request->token(),
                            std::move(terminal_params), cid, &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure opening terminal from ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION, "Failure in OpenTerminal");
  }
  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::UpdateMimeTypes(
    grpc::ServerContext* ctx,
    const vm_tools::container::UpdateMimeTypesRequest* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }
  vm_tools::apps::MimeTypes mime_types;
  mime_types.mutable_mime_type_mappings()->insert(
      request->mime_type_mappings().begin(),
      request->mime_type_mappings().end());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&vm_tools::cicerone::Service::UpdateMimeTypes,
                            service_, request->token(), std::move(mime_types),
                            cid, &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure updating MIME types from ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failure in UpdateMimeTypes");
  }
  return grpc::Status::OK;
}

grpc::Status ContainerListenerImpl::FileWatchTriggered(
    grpc::ServerContext* ctx,
    const vm_tools::container::FileWatchTriggeredInfo* request,
    vm_tools::EmptyMessage* response) {
  uint32_t cid = ExtractCidFromPeerAddress(ctx);
  if (cid == 0) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failed parsing cid for ContainerListener");
  }
  FileWatchTriggeredSignal triggered_signal;
  triggered_signal.set_path(request->path());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool result = false;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&vm_tools::cicerone::Service::FileWatchTriggered, service_,
                 request->token(), cid, &triggered_signal, &result, &event));
  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failure notifying FileWatchTriggered from ContainerListener";
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Failure in FileWatchTriggered");
  }
  return grpc::Status::OK;
}

uint32_t ContainerListenerImpl::ExtractCidFromPeerAddress(
    grpc::ServerContext* ctx) {
  uint32_t cid = 0;
  std::string peer_address = ctx->peer();
  {
    base::AutoLock lock_scope(testing_peer_address_lock_);
    if (!testing_peer_address_.empty()) {
      peer_address = testing_peer_address_;
    }
  }
  if (sscanf(peer_address.c_str(), "vsock:%" SCNu32, &cid) != 1) {
    // This is not necessarily a failure if this is a unix socket.
    return 0;
  }
  return cid;
}

bool ContainerListenerImpl::CheckOpenRateLimit() {
  base::TimeTicks now = base::TimeTicks::Now();
  if (now - open_rate_window_start_ > kOpenRateWindow) {
    // Beyond the window, reset the window start time and counter.
    open_rate_window_start_ = now;
    open_count_ = 1;
    return true;
  }
  if (++open_count_ <= kOpenRateLimit)
    return true;
  // Only log the first one over the limit to prevent log spam if this is
  // getting hit quickly.
  LOG_IF(ERROR, open_count_ == kOpenRateLimit + 1)
      << "OpenUrl/Terminal rate limit hit, blocking requests until window "
         "closes";
  return false;
}

}  // namespace cicerone
}  // namespace vm_tools
