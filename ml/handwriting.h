// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_HANDWRITING_H_
#define ML_HANDWRITING_H_

#include <string>

#include <base/no_destructor.h>
#include <base/optional.h>
#include <base/scoped_native_library.h>
#include <chromeos/libhandwriting/interface.h>

#include "chrome/knowledge/handwriting/interface.pb.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"

namespace ml {
// A singleton proxy class for the handwriting DSO.
// Usage:
//   auto* const hwr_library = HandwritingLibrary::GetInstance();
//   if (hwr_library->GetStatus() == HandwritingLibrary::kOk) {
//     // Do the real handwriting here.
//     recognizer = hwr_library->CreateHandwritingRecognizer();
//     ...
//   } else {
//     // Otherwise, use HandwritingLibrary::GetStatus() to get the error type.
//     // Maybe return "not installed".
//     ...
//   }
class HandwritingLibrary {
 public:
  enum class Status {
    kOk = 0,
    kUninitialized = 1,
    kLoadLibraryFailed = 2,
    kFunctionLookupFailed = 3,
    kNotSupported = 4,
  };

  ~HandwritingLibrary() = default;

  // Returns whether HandwritingLibrary is supported.
  static constexpr bool IsHandwritingLibrarySupported() {
    return (IsUseLibHandwritingEnabled() || IsUseLibHandwritingDlcEnabled()) &&
           !IsAsan();
  }

  // Returns whether HandwritingLibrary is supported for unit tests.
  static constexpr bool IsHandwritingLibraryUnitTestSupported() {
    return IsUseLibHandwritingEnabled() && !IsAsan();
  }

  // Gets the singleton HandwritingLibrary.
  static HandwritingLibrary* GetInstance();

  // Get whether the library is successfully initialized.
  // Initially, the status is `Status::kUninitialized` (this value should never
  // be returned).
  // If libhandwriting.so can not be loaded, return `kLoadLibraryFailed`. This
  // usually means on-device handwriting is not supported.
  // If the functions can not be successfully looked up, return
  // `kFunctionLookupFailed`.
  // Return `Status::kOk` if everything works fine.
  Status GetStatus() const;

  // The following public member functions define the interface functions of
  // the libhandwriting.so library. Function `InitHandwritingRecognizerLibrary`
  // and `DeleteHandwritingResultData` do not need interfaces because the client
  // won't call it.

  // Creates and returns a handwriting recognizer which is needed for using the
  // other interface. The memory is owned by the user and should be deleted
  // using `DestroyHandwritingRecognizer` after usage.
  HandwritingRecognizer CreateHandwritingRecognizer() const;
  // Load the models with `spec` stores the language, the path to the data files
  // of the model (machine learning models, configurations etc.).
  // Returns true if HandwritingRecognizer is correctly loaded and
  // initialized. Returns false otherwise.
  bool LoadHandwritingRecognizer(
      HandwritingRecognizer recognizer,
      chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec)
      const;
  // Sends the specified `request` to `recognizer`, if succeeds, `result` (which
  // should not be null) is populated with the recognition result.
  // Returns true if succeeds, otherwise returns false.
  bool RecognizeHandwriting(
      HandwritingRecognizer recognizer,
      const chrome_knowledge::HandwritingRecognizerRequest& request,
      chrome_knowledge::HandwritingRecognizerResult* result) const;
  // Destroys the handwriting recognizer created by
  // `CreateHandwritingRecognizer`. Must be called if the handwriting recognizer
  // will not be used anymore, otherwise there will be memory leak.
  void DestroyHandwritingRecognizer(HandwritingRecognizer recognizer) const;

 private:
  friend class base::NoDestructor<HandwritingLibrary>;
  FRIEND_TEST(HandwritingLibraryTest, CanLoadLibrary);

  // Initialize the handwriting library.
  explicit HandwritingLibrary();

  // Currently HandwritingLibrary is supported only when the "sanitizer" is not
  // enabled (see https://crbug.com/1082632).
  static constexpr bool IsAsan() { return __has_feature(address_sanitizer); }

  // Returns bool of use.ondevice_handwriting.
  static constexpr bool IsUseLibHandwritingEnabled() {
    return USE_ONDEVICE_HANDWRITING;
  }

  // Returns bool of use.ondevice_handwriting_dlc.
  static constexpr bool IsUseLibHandwritingDlcEnabled() {
    return USE_ONDEVICE_HANDWRITING_DLC;
  }

  base::Optional<base::ScopedNativeLibrary> library_;
  Status status_;

  // Store the interface function pointers.
  // TODO(honglinyu) as pointed out by cjmcdonald@, we should group the pointers
  // into a single `HandwritingInterface` struct and make it optional, i.e.,
  // declaring something like |base::Optional<HandwritingInterface> interface_|.
  CreateHandwritingRecognizerFn create_handwriting_recognizer_;
  LoadHandwritingRecognizerFn load_handwriting_recognizer_;
  RecognizeHandwritingFn recognize_handwriting_;
  DeleteHandwritingResultDataFn delete_handwriting_result_data_;
  DestroyHandwritingRecognizerFn destroy_handwriting_recognizer_;

  DISALLOW_COPY_AND_ASSIGN(HandwritingLibrary);
};

}  // namespace ml

#endif  // ML_HANDWRITING_H_
