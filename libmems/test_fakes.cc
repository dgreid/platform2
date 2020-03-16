// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/test_fakes.h"

#include <base/logging.h>

#include "libmems/common_types.h"

namespace libmems {
namespace fakes {

FakeIioChannel::FakeIioChannel(const std::string& id, bool enabled)
    : id_(id), enabled_(enabled) {}

bool FakeIioChannel::SetEnabled(bool en) {
  enabled_ = en;
  return true;
}

template <typename T> base::Optional<T> FakeReadAttributes(
    const std::string& name,
    std::map<std::string, T> attributes) {
  auto k = attributes.find(name);
  if (k == attributes.end())
    return base::nullopt;
  return k->second;
}

base::Optional<std::string> FakeIioChannel::ReadStringAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, text_attributes_);
}
base::Optional<int64_t> FakeIioChannel::ReadNumberAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, numeric_attributes_);
}
base::Optional<double> FakeIioChannel::ReadDoubleAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, double_attributes_);
}

bool FakeIioChannel::WriteStringAttribute(const std::string& name,
                                          const std::string& value) {
  text_attributes_[name] = value;
  return true;
}
bool FakeIioChannel::WriteNumberAttribute(const std::string& name,
                                          int64_t value) {
  numeric_attributes_[name] = value;
  return true;
}
bool FakeIioChannel::WriteDoubleAttribute(const std::string& name,
                                          double value) {
  double_attributes_[name] = value;
  return true;
}

FakeIioDevice::FakeIioDevice(FakeIioContext* ctx,
                             const std::string& name,
                             int id)
    : IioDevice(), context_(ctx), name_(name), id_(id) {}

base::FilePath FakeIioDevice::GetPath() const {
  std::string id_str(kDeviceIdPrefix);
  id_str.append(std::to_string(GetId()));
  return base::FilePath("/sys/bus/iio/devices").Append(id_str);
}

base::Optional<std::string> FakeIioDevice::ReadStringAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, text_attributes_);
}
base::Optional<int64_t> FakeIioDevice::ReadNumberAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, numeric_attributes_);
}
base::Optional<double> FakeIioDevice::ReadDoubleAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, double_attributes_);
}

bool FakeIioDevice::WriteStringAttribute(const std::string& name,
                                         const std::string& value) {
  text_attributes_[name] = value;
  return true;
}
bool FakeIioDevice::WriteNumberAttribute(const std::string& name,
                                         int64_t value) {
  numeric_attributes_[name] = value;
  return true;
}
bool FakeIioDevice::WriteDoubleAttribute(const std::string& name,
                                         double value) {
  double_attributes_[name] = value;
  return true;
}

bool FakeIioDevice::SetTrigger(IioDevice* trigger) {
  trigger_ = trigger;
  return true;
}

std::vector<IioChannel*> FakeIioDevice::GetAllChannels() {
  std::vector<IioChannel*> channels;
  for (auto channel : channels_)
    channels.push_back(channel.second);

  return channels;
}

IioChannel* FakeIioDevice::GetChannel(const std::string& id) {
  auto k = channels_.find(id);
  if (k == channels_.end())
    return nullptr;
  return k->second;
}

bool FakeIioDevice::EnableBuffer(size_t n) {
  buffer_length_ = n;
  buffer_enabled_ = true;
  return true;
}
bool FakeIioDevice::DisableBuffer() {
  buffer_enabled_ = false;
  return true;
}
bool FakeIioDevice::IsBufferEnabled(size_t* n) const {
  if (n && buffer_enabled_)
    *n = buffer_length_;
  return buffer_enabled_;
}

void FakeIioContext::AddDevice(FakeIioDevice* device) {
  CHECK(device);
  devices_.emplace(device->GetId(), device);
}

void FakeIioContext::AddTrigger(FakeIioDevice* trigger) {
  CHECK(trigger);
  triggers_.emplace(trigger->GetId(), trigger);
}

std::vector<IioDevice*> FakeIioContext::GetDevicesByName(
    const std::string& name) {
  return GetFakeByName(name, devices_);
}

IioDevice* FakeIioContext::GetDeviceById(int id) {
  return GetFakeById(id, devices_);
}

std::vector<IioDevice*> FakeIioContext::GetAllDevices() {
  return GetFakeAll(devices_);
}

std::vector<IioDevice*> FakeIioContext::GetTriggersByName(
    const std::string& name) {
  return GetFakeByName(name, triggers_);
}

IioDevice* FakeIioContext::GetTriggerById(int id) {
  return GetFakeById(id, triggers_);
}

std::vector<IioDevice*> FakeIioContext::GetAllTriggers() {
  return GetFakeAll(triggers_);
}

IioDevice* FakeIioContext::GetFakeById(
    int id, const std::map<int, FakeIioDevice*>& devices_map) {
  auto k = devices_map.find(id);
  return (k == devices_map.end()) ? nullptr : k->second;
}

std::vector<IioDevice*> FakeIioContext::GetFakeByName(
    const std::string& name, const std::map<int, FakeIioDevice*>& devices_map) {
  std::vector<IioDevice*> devices;
  for (auto const& it : devices_map) {
    if (name.compare(it.second->GetName()) == 0)
      devices.push_back(it.second);
  }

  return devices;
}

std::vector<IioDevice*> FakeIioContext::GetFakeAll(
    const std::map<int, FakeIioDevice*>& devices_map) {
  std::vector<IioDevice*> devices;
  for (auto const& it : devices_map)
    devices.push_back(it.second);

  return devices;
}

}  // namespace fakes
}  // namespace libmems
