// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service_provider.h"

#include <set>
#include <string>
#include <vector>

#include "shill/cellular/cellular_service.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/store_interface.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static std::string ObjectID(const CellularServiceProvider* e) {
  return "(cellular_service_provider)";
}
}  // namespace Logging

namespace {

bool GetServiceParametersFromArgs(const KeyValueStore& args,
                                  std::string* imsi,
                                  std::string* iccid,
                                  std::string* sim_card_id,
                                  Error* error) {
  *imsi =
      args.Lookup<std::string>(CellularService::kStorageImsi, std::string());
  if (imsi->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing IMSI");
    return false;
  }
  *iccid =
      args.Lookup<std::string>(CellularService::kStorageIccid, std::string());
  if (iccid->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing ICCID");
    return false;
  }
  *sim_card_id = args.Lookup<std::string>(CellularService::kStorageSimCardId,
                                          std::string());
  if (sim_card_id->empty()) {
    // If SIM Card Id is unset, fall back to ICCID.
    *sim_card_id = *iccid;
  }
  return true;
}

bool GetServiceParametersFromStorage(const StoreInterface* storage,
                                     const std::string& entry_name,
                                     std::string* imsi,
                                     std::string* iccid,
                                     std::string* sim_card_id,
                                     Error* error) {
  if (!storage->GetString(entry_name, CellularService::kStorageImsi, imsi) ||
      imsi->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing or empty IMSI");
    return false;
  }
  if (!storage->GetString(entry_name, CellularService::kStorageIccid, iccid) ||
      iccid->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing or empty ICCID");
    return false;
  }
  if (!storage->GetString(entry_name, CellularService::kStorageSimCardId,
                          sim_card_id) ||
      sim_card_id->empty()) {
    // If SIM Card Id is unset or empty, fall back to ICCID.
    *sim_card_id = *iccid;
  }
  return true;
}

}  // namespace

CellularServiceProvider::CellularServiceProvider(Manager* manager)
    : manager_(manager) {}

CellularServiceProvider::~CellularServiceProvider() = default;

void CellularServiceProvider::CreateServicesFromProfile(
    const ProfileRefPtr& profile) {
  SLOG(this, 2) << __func__ << ": " << profile->GetFriendlyName();
  // A Cellular Device may not exist yet, so we do not load services here.
  // Cellular services associated with a Device are loaded in
  // LoadServicesForDevice when the Device is created. We store |profile| here
  // so that we always use the default profile (see comment in header).
  if (!profile_)
    profile_ = profile;
}

ServiceRefPtr CellularServiceProvider::FindSimilarService(
    const KeyValueStore& args, Error* error) const {
  SLOG(this, 2) << __func__;
  CHECK_EQ(kTypeCellular, args.Lookup<std::string>(kTypeProperty, ""))
      << "Service type must be Cellular!";
  // This is called from Manager::ConfigureServiceForProfile when the Manager
  // dbus api call is made (e.g. from Chrome) for a new service (i.e without
  // an existing GUID). For Cellular, this should never happen.
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Only existing Cellular services can be configured.");
  return nullptr;
}

ServiceRefPtr CellularServiceProvider::GetService(const KeyValueStore& args,
                                                  Error* error) {
  SLOG(this, 2) << __func__;
  // This is called from Manager::GetService or Manager::ConfigureService when
  // the corresponding Manager dbus api call is made (e.g. from Chrome) for a
  // new service (i.e without an existing GUID). For Cellular, this should never
  // happen.
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "GetService must be called with an existing Cellular Service GUID.");
  return nullptr;
}

ServiceRefPtr CellularServiceProvider::CreateTemporaryService(
    const KeyValueStore& args, Error* error) {
  SLOG(this, 2) << __func__;
  std::string imsi, iccid, sim_card_id;
  if (GetServiceParametersFromArgs(args, &imsi, &iccid, &sim_card_id, error))
    return new CellularService(manager_, imsi, iccid, sim_card_id);
  return nullptr;
}

ServiceRefPtr CellularServiceProvider::CreateTemporaryServiceFromProfile(
    const ProfileRefPtr& profile, const std::string& entry_name, Error* error) {
  SLOG(this, 2) << __func__ << ": " << profile->GetFriendlyName();
  std::string imsi, iccid, sim_card_id;
  if (GetServiceParametersFromStorage(profile->GetConstStorage(), entry_name,
                                      &imsi, &iccid, &sim_card_id, error)) {
    return new CellularService(manager_, imsi, iccid, sim_card_id);
  }
  return nullptr;
}

void CellularServiceProvider::Start() {
  SLOG(this, 2) << __func__;
}

void CellularServiceProvider::Stop() {
  SLOG(this, 2) << __func__;
  while (!services_.empty())
    RemoveService(services_.back());
}

CellularServiceRefPtr CellularServiceProvider::LoadServicesForDevice(
    Cellular* device) {
  CellularServiceRefPtr active_service = nullptr;

  std::string sim_card_id = device->GetSimCardId();

  // Find Cellular profile entries matching the sim card identifier.
  CHECK(profile_);
  StoreInterface* storage = profile_->GetStorage();
  DCHECK(storage);
  KeyValueStore args;
  args.Set<std::string>(kTypeProperty, kTypeCellular);
  args.Set<std::string>(CellularService::kStorageSimCardId, sim_card_id);
  std::set<std::string> groups = storage->GetGroupsWithProperties(args);

  LOG(INFO) << __func__ << ": " << device->iccid() << ": " << groups.size();
  for (const std::string& group : groups) {
    std::string imsi, iccid, service_sim_card_id;
    if (!GetServiceParametersFromStorage(storage, group, &imsi, &iccid,
                                         &service_sim_card_id,
                                         /*error=*/nullptr)) {
      LOG(ERROR) << "Unable to load service properties for: " << sim_card_id
                 << ", removing old or invalid profile entry.";
      storage->DeleteGroup(group);
      continue;
    }
    DCHECK_EQ(service_sim_card_id, sim_card_id);
    CellularServiceRefPtr service = FindService(imsi);
    if (!service) {
      SLOG(this, 1) << "Loading Cellular service for " << imsi;
      service = new CellularService(manager_, imsi, iccid, sim_card_id);
      service->Load(storage);
      service->SetDevice(device);
      AddService(service);
    } else {
      SLOG(this, 1) << "Cellular service exists: " << imsi;
      service->SetDevice(device);
    }
    if (imsi == device->imsi())
      active_service = service;
  }
  if (!active_service) {
    SLOG(this, 1) << "No existing Cellular service for " << device->imsi();
    active_service = new CellularService(manager_, device->imsi(),
                                         device->iccid(), sim_card_id);
    active_service->SetDevice(device);
    AddService(active_service);
  }

  // Remove any remaining services not associated with |device|.
  std::vector<CellularServiceRefPtr> services_to_remove;
  for (CellularServiceRefPtr& service : services_) {
    if (!service->cellular())
      services_to_remove.push_back(service);
  }
  for (CellularServiceRefPtr& service : services_to_remove)
    RemoveService(service);
  return active_service;
}

void CellularServiceProvider::RemoveServicesForDevice(Cellular* device) {
  std::string sim_card_id = device->GetSimCardId();
  LOG(INFO) << __func__ << ": " << sim_card_id;
  // Set |device| to null for services associated with |device|. When a new
  // Cellular device is created (e.g. after a modem resets after a sim swap),
  // services not matching the new device will be removed in
  // LoadServicesForDevice(). This allows services to continue to exist during a
  // modem reset when Modem and Cellular may get temporarily destroyed.
  for (CellularServiceRefPtr& service : services_) {
    if (service->cellular() == device)
      service->SetDevice(nullptr);
  }
}

void CellularServiceProvider::AddService(CellularServiceRefPtr service) {
  SLOG(this, 1) << __func__ << ": " << service->imsi();

  // See comment in header for |profile_|.
  service->SetProfile(profile_);
  // Save any changes to device properties (iccid, sim_card_id).
  profile_->UpdateService(service);
  manager_->RegisterService(service);
  services_.push_back(service);
}

void CellularServiceProvider::RemoveService(CellularServiceRefPtr service) {
  SLOG(this, 1) << __func__ << ": " << service->imsi();
  manager_->DeregisterService(service);
  auto iter = std::find(services_.begin(), services_.end(), service);
  if (iter == services_.end()) {
    LOG(ERROR) << "RemoveService: Not found: ";
    return;
  }
  services_.erase(iter);
}

CellularServiceRefPtr CellularServiceProvider::FindService(
    const std::string& imsi) {
  const auto iter = std::find_if(
      services_.begin(), services_.end(),
      [imsi](const auto& service) { return service->imsi() == imsi; });
  if (iter != services_.end())
    return *iter;
  return nullptr;
}

}  // namespace shill
