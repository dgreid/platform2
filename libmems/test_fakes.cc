// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/test_fakes.h"

#include <sys/eventfd.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include "base/posix/eintr_wrapper.h"
#include <base/stl_util.h>

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

base::Optional<int64_t> FakeIioChannel::GetData(int index) {
  if (!enabled_ || index < 0 || index >= base::size(kFakeAccelSamples))
    return base::nullopt;

  auto raw = ReadNumberAttribute(kRawAttr);
  if (raw.has_value())
    return raw;

  for (int i = 0; i < base::size(kFakeAccelChns); ++i) {
    if (id_.compare(kFakeAccelChns[i]) == 0)
      return kFakeAccelSamples[index][i];
  }

  return base::nullopt;
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
  for (const auto& channel_data : channels_)
    channels.push_back(channel_data.chn);

  return channels;
}

IioChannel* FakeIioDevice::GetChannel(int32_t index) {
  if (index < 0 || index >= channels_.size())
    return nullptr;

  return channels_[index].chn;
}

IioChannel* FakeIioDevice::GetChannel(const std::string& id) {
  for (size_t i = 0; i < channels_.size(); ++i) {
    if (id == channels_[i].chn_id)
      return channels_[i].chn;
  }

  return nullptr;
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

base::Optional<int32_t> FakeIioDevice::GetBufferFd() {
  if (disabled_fd_)
    return base::nullopt;

  if (!CreateBuffer())
    return base::nullopt;

  return sample_fd_.get();
}
base::Optional<IioDevice::IioSample> FakeIioDevice::ReadSample() {
  if (is_paused_ || disabled_fd_)
    return base::nullopt;

  if (!failed_read_queue_.empty()) {
    CHECK_GE(failed_read_queue_.top(), sample_index_);
    if (failed_read_queue_.top() == sample_index_) {
      failed_read_queue_.pop();
      return base::nullopt;
    }
  }

  if (!CreateBuffer())
    return base::nullopt;

  if (!ReadByte())
    return base::nullopt;

  base::Optional<double> freq_opt = ReadDoubleAttribute(kSamplingFrequencyAttr);
  if (!freq_opt.has_value()) {
    LOG(ERROR) << "sampling_frequency not set";
    return base::nullopt;
  }
  double frequency = freq_opt.value();
  if (frequency <= 0.0) {
    LOG(ERROR) << "Invalid frequency: " << frequency;
    return base::nullopt;
  }

  IioDevice::IioSample sample;
  for (int32_t i = 0; i < channels_.size(); ++i) {
    auto value = channels_[i].chn->GetData(sample_index_);
    if (!value.has_value()) {
      LOG(ERROR) << "Channel: " << channels_[i].chn_id << " has no sample";
      return base::nullopt;
    }

    sample[i] = value.value();
  }

  sample_index_ += 1;

  if (sample_index_ < base::size(kFakeAccelSamples)) {
    if (pause_index_.has_value() && sample_index_ == pause_index_.value())
      SetPause();
    else if (!WriteByte())
      return base::nullopt;
  }

  return sample;
}

void FakeIioDevice::DisableFd() {
  disabled_fd_ = true;
  if (readable_fd_)
    CHECK(ReadByte());
}

void FakeIioDevice::AddFailedReadAtKthSample(int k) {
  CHECK_GE(k, sample_index_);

  failed_read_queue_.push(k);
}

void FakeIioDevice::SetPauseCallbackAtKthSamples(
    int k, base::OnceCallback<void()> callback) {
  CHECK_GE(k, sample_index_);
  CHECK_LE(k, base::size(kFakeAccelSamples));
  CHECK(!pause_index_.has_value());  // pause callback hasn't been set

  pause_index_ = k;
  pause_callback_ = std::move(callback);

  if (pause_index_.value() != sample_index_)
    return;

  SetPause();
}

void FakeIioDevice::ResumeReadingSamples() {
  CHECK(is_paused_);

  is_paused_ = false;
  if (sample_fd_.is_valid() && !readable_fd_)
    CHECK(WriteByte());
}

bool FakeIioDevice::CreateBuffer() {
  CHECK(!disabled_fd_);

  if (sample_fd_.is_valid())
    return true;

  int fd = eventfd(0, 0);
  CHECK_GE(fd, 0);
  sample_fd_.reset(fd);

  if (sample_index_ >= base::size(kFakeAccelSamples) || is_paused_)
    return true;

  if (!WriteByte()) {
    ClosePipe();
    return false;
  }

  return true;
}

bool FakeIioDevice::WriteByte() {
  if (!sample_fd_.is_valid())
    return false;

  CHECK(!readable_fd_);
  uint64_t val = 1;
  CHECK_EQ(write(sample_fd_.get(), &val, sizeof(uint64_t)), sizeof(uint64_t));
  readable_fd_ = true;

  return true;
}

bool FakeIioDevice::ReadByte() {
  if (!sample_fd_.is_valid())
    return false;

  CHECK(readable_fd_);
  int64_t val = 1;
  CHECK_EQ(read(sample_fd_.get(), &val, sizeof(uint64_t)), sizeof(uint64_t));
  readable_fd_ = false;

  return true;
}

void FakeIioDevice::ClosePipe() {
  sample_fd_.reset();
}

void FakeIioDevice::SetPause() {
  is_paused_ = true;
  pause_index_.reset();
  std::move(pause_callback_).Run();
  if (readable_fd_)
    CHECK(ReadByte());
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
