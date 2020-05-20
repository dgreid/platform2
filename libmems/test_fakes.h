// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBMEMS_TEST_FAKES_H_
#define LIBMEMS_TEST_FAKES_H_

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "libmems/export.h"
#include "libmems/iio_channel.h"
#include "libmems/iio_context.h"
#include "libmems/iio_device.h"

namespace libmems {
namespace fakes {
class LIBMEMS_EXPORT FakeIioChannel : public IioChannel {
 public:
  FakeIioChannel(const std::string& id, bool enabled);

  const char* GetId() const override { return id_.c_str(); }

  bool IsEnabled() const override { return enabled_; }
  bool SetEnabled(bool en) override;

  base::Optional<std::string> ReadStringAttribute(
      const std::string& name) const override;

  base::Optional<int64_t> ReadNumberAttribute(
      const std::string& name) const override;

  void WriteStringAttribute(const std::string& name,
                            const std::string& value);
  void WriteNumberAttribute(const std::string& name, int64_t value);

 private:
  std::string id_;
  bool enabled_;
  std::map<std::string, int64_t> numeric_attributes_;
  std::map<std::string, std::string> text_attributes_;
};

class FakeIioContext;

class LIBMEMS_EXPORT FakeIioDevice : public IioDevice {
 public:
  FakeIioDevice(FakeIioContext* ctx, const std::string& name, int id);

  IioContext* GetContext() const override {
    return reinterpret_cast<IioContext*>(context_);
  }

  base::FilePath GetPath() const override;
  iio_device* GetUnderlyingIioDevice() const override { return nullptr; }

  const char* GetName() const override { return name_.c_str(); }
  int GetId() const override { return id_; }

  base::Optional<std::string> ReadStringAttribute(
      const std::string& name) const override;
  base::Optional<int64_t> ReadNumberAttribute(
      const std::string& name) const override;
  base::Optional<double> ReadDoubleAttribute(
      const std::string& name) const override;

  bool WriteStringAttribute(const std::string& name,
                            const std::string& value) override;
  bool WriteNumberAttribute(const std::string& name, int64_t value) override;
  bool WriteDoubleAttribute(const std::string& name, double value) override;

  bool SetTrigger(IioDevice* trigger) override;
  IioDevice* GetTrigger() override { return trigger_; }

  void AddChannel(IioChannel* chan) { channels_.emplace(chan->GetId(), chan); }

  std::vector<IioChannel*> GetAllChannels() override;
  IioChannel* GetChannel(const std::string& id) override;

  bool EnableBuffer(size_t n) override;
  bool DisableBuffer() override;
  bool IsBufferEnabled(size_t* n = nullptr) const override;

  base::Optional<size_t> GetSampleSize() const override {
    return base::nullopt;
  }

  base::Optional<int32_t> GetBufferFd() override { return base::nullopt; }
  base::Optional<IioSample> ReadSample() override { return base::nullopt; }

 private:
  FakeIioContext* context_;
  std::string name_;
  int id_;
  std::map<std::string, int64_t> numeric_attributes_;
  std::map<std::string, std::string> text_attributes_;
  std::map<std::string, double> double_attributes_;
  IioDevice* trigger_;
  std::map<std::string, IioChannel*> channels_;
  size_t buffer_length_ = 0;
  bool buffer_enabled_ = false;
};

class LIBMEMS_EXPORT FakeIioContext : public IioContext {
 public:
  FakeIioContext() = default;

  void AddDevice(FakeIioDevice* device);
  void AddTrigger(FakeIioDevice* trigger);

  iio_context* GetCurrentContext() const override { return nullptr; };
  void Reload() override {}
  bool SetTimeout(uint32_t timeout) override {
    timeout_ = timeout;
    return true;
  }

  std::vector<IioDevice*> GetDevicesByName(const std::string& name) override;
  IioDevice* GetDeviceById(int id) override;
  std::vector<IioDevice*> GetAllDevices() override;

  std::vector<IioDevice*> GetTriggersByName(const std::string& name) override;
  IioDevice* GetTriggerById(int id) override;
  std::vector<IioDevice*> GetAllTriggers() override;

 private:
  IioDevice* GetFakeById(int id,
                         const std::map<int, FakeIioDevice*>& devices_map);
  std::vector<IioDevice*> GetFakeByName(
      const std::string& name,
      const std::map<int, FakeIioDevice*>& devices_map);
  std::vector<IioDevice*> GetFakeAll(
      const std::map<int, FakeIioDevice*>& devices_map);

  std::map<int, FakeIioDevice*> devices_;
  std::map<int, FakeIioDevice*> triggers_;

  uint32_t timeout_;
};

}  // namespace fakes

}  // namespace libmems

#endif  // LIBMEMS_TEST_FAKES_H_
