#ifndef HIDL_GENERATED_ANDROID_HARDWARE_NEURALNETWORKS_V1_3_ADEVICE_H
#define HIDL_GENERATED_ANDROID_HARDWARE_NEURALNETWORKS_V1_3_ADEVICE_H

#include <android/hardware/neuralnetworks/1.3/IDevice.h>
namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_3 {

class ADevice : public ::android::hardware::neuralnetworks::V1_3::IDevice {
 public:
  typedef ::android::hardware::neuralnetworks::V1_3::IDevice Pure;
  ADevice(
      const ::android::sp<::android::hardware::neuralnetworks::V1_3::IDevice>&
          impl);
  // Methods from ::android::hardware::neuralnetworks::V1_0::IDevice follow.
  virtual ::android::hardware::Return<void> getCapabilities(
      getCapabilities_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getSupportedOperations(
      const ::android::hardware::neuralnetworks::V1_0::Model& model,
      getSupportedOperations_cb _hidl_cb) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_0::ErrorStatus>
  prepareModel(
      const ::android::hardware::neuralnetworks::V1_0::Model& model,
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_0::IPreparedModelCallback>&
          callback) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_0::DeviceStatus>
  getStatus() override;

  // Methods from ::android::hardware::neuralnetworks::V1_1::IDevice follow.
  virtual ::android::hardware::Return<void> getCapabilities_1_1(
      getCapabilities_1_1_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getSupportedOperations_1_1(
      const ::android::hardware::neuralnetworks::V1_1::Model& model,
      getSupportedOperations_1_1_cb _hidl_cb) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_0::ErrorStatus>
  prepareModel_1_1(
      const ::android::hardware::neuralnetworks::V1_1::Model& model,
      ::android::hardware::neuralnetworks::V1_1::ExecutionPreference preference,
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_0::IPreparedModelCallback>&
          callback) override;

  // Methods from ::android::hardware::neuralnetworks::V1_2::IDevice follow.
  virtual ::android::hardware::Return<void> getVersionString(
      getVersionString_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getType(
      getType_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getCapabilities_1_2(
      getCapabilities_1_2_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getSupportedExtensions(
      getSupportedExtensions_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getSupportedOperations_1_2(
      const ::android::hardware::neuralnetworks::V1_2::Model& model,
      getSupportedOperations_1_2_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getNumberOfCacheFilesNeeded(
      getNumberOfCacheFilesNeeded_cb _hidl_cb) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_0::ErrorStatus>
  prepareModel_1_2(
      const ::android::hardware::neuralnetworks::V1_2::Model& model,
      ::android::hardware::neuralnetworks::V1_1::ExecutionPreference preference,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          modelCache,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          dataCache,
      const ::android::hardware::hidl_array<
          uint8_t,
          32 /* Constant:BYTE_SIZE_OF_CACHE_TOKEN */>& token,
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_2::IPreparedModelCallback>&
          callback) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_0::ErrorStatus>
  prepareModelFromCache(
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          modelCache,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          dataCache,
      const ::android::hardware::hidl_array<
          uint8_t,
          32 /* Constant:BYTE_SIZE_OF_CACHE_TOKEN */>& token,
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_2::IPreparedModelCallback>&
          callback) override;

  // Methods from ::android::hardware::neuralnetworks::V1_3::IDevice follow.
  virtual ::android::hardware::Return<void> getCapabilities_1_3(
      getCapabilities_1_3_cb _hidl_cb) override;
  virtual ::android::hardware::Return<void> getSupportedOperations_1_3(
      const ::android::hardware::neuralnetworks::V1_3::Model& model,
      getSupportedOperations_1_3_cb _hidl_cb) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_3::ErrorStatus>
  prepareModel_1_3(
      const ::android::hardware::neuralnetworks::V1_3::Model& model,
      ::android::hardware::neuralnetworks::V1_1::ExecutionPreference preference,
      ::android::hardware::neuralnetworks::V1_3::Priority priority,
      const ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&
          deadline,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          modelCache,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          dataCache,
      const ::android::hardware::hidl_array<
          uint8_t,
          32 /* Constant:BYTE_SIZE_OF_CACHE_TOKEN */>& token,
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_3::IPreparedModelCallback>&
          callback) override;
  virtual ::android::hardware::Return<
      ::android::hardware::neuralnetworks::V1_3::ErrorStatus>
  prepareModelFromCache_1_3(
      const ::android::hardware::neuralnetworks::V1_3::OptionalTimePoint&
          deadline,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          modelCache,
      const ::android::hardware::hidl_vec<::android::hardware::hidl_handle>&
          dataCache,
      const ::android::hardware::hidl_array<
          uint8_t,
          32 /* Constant:BYTE_SIZE_OF_CACHE_TOKEN */>& token,
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_3::IPreparedModelCallback>&
          callback) override;
  virtual ::android::hardware::Return<void> allocate(
      const ::android::hardware::neuralnetworks::V1_3::BufferDesc& desc,
      const ::android::hardware::hidl_vec<::android::sp<
          ::android::hardware::neuralnetworks::V1_3::IPreparedModel>>&
          preparedModels,
      const ::android::hardware::hidl_vec<
          ::android::hardware::neuralnetworks::V1_3::BufferRole>& inputRoles,
      const ::android::hardware::hidl_vec<
          ::android::hardware::neuralnetworks::V1_3::BufferRole>& outputRoles,
      allocate_cb _hidl_cb) override;

  // Methods from ::android::hidl::base::V1_0::IBase follow.

 private:
  ::android::sp<::android::hardware::neuralnetworks::V1_3::IDevice> mImpl;
};

}  // namespace V1_3
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android
#endif  // HIDL_GENERATED_ANDROID_HARDWARE_NEURALNETWORKS_V1_3_ADEVICE_H
