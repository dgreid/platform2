#ifndef HIDL_GENERATED_ANDROID_HARDWARE_NEURALNETWORKS_V1_3_AEXECUTIONCALLBACK_H
#define HIDL_GENERATED_ANDROID_HARDWARE_NEURALNETWORKS_V1_3_AEXECUTIONCALLBACK_H

#include <android/hardware/neuralnetworks/1.3/IExecutionCallback.h>
namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_3 {

class AExecutionCallback
    : public ::android::hardware::neuralnetworks::V1_3::IExecutionCallback {
 public:
  typedef ::android::hardware::neuralnetworks::V1_3::IExecutionCallback Pure;
  AExecutionCallback(
      const ::android::sp<
          ::android::hardware::neuralnetworks::V1_3::IExecutionCallback>& impl);
  // Methods from ::android::hardware::neuralnetworks::V1_0::IExecutionCallback
  // follow.
  virtual ::android::hardware::Return<void> notify(
      ::android::hardware::neuralnetworks::V1_0::ErrorStatus status) override;

  // Methods from ::android::hardware::neuralnetworks::V1_2::IExecutionCallback
  // follow.
  virtual ::android::hardware::Return<void> notify_1_2(
      ::android::hardware::neuralnetworks::V1_0::ErrorStatus status,
      const ::android::hardware::hidl_vec<
          ::android::hardware::neuralnetworks::V1_2::OutputShape>& outputShapes,
      const ::android::hardware::neuralnetworks::V1_2::Timing& timing) override;

  // Methods from ::android::hardware::neuralnetworks::V1_3::IExecutionCallback
  // follow.
  virtual ::android::hardware::Return<void> notify_1_3(
      ::android::hardware::neuralnetworks::V1_3::ErrorStatus status,
      const ::android::hardware::hidl_vec<
          ::android::hardware::neuralnetworks::V1_2::OutputShape>& outputShapes,
      const ::android::hardware::neuralnetworks::V1_2::Timing& timing) override;

  // Methods from ::android::hidl::base::V1_0::IBase follow.

 private:
  ::android::sp<::android::hardware::neuralnetworks::V1_3::IExecutionCallback>
      mImpl;
};

}  // namespace V1_3
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android
#endif  // HIDL_GENERATED_ANDROID_HARDWARE_NEURALNETWORKS_V1_3_AEXECUTIONCALLBACK_H
