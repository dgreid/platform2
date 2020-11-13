// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <unordered_map>

#include "ml/soda.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/native_library.h>
namespace ml {

namespace {

constexpr char kSodaLibraryPath[] =
    "/opt/google/chrome/ml_models/soda/libsoda.so";

}  // namespace

SodaLibrary::SodaLibrary(const std::string& library_path)
    : status_(Status::kUninitialized),
      create_soda_async_(nullptr),
      add_audio_(nullptr),
      delete_soda_async_(nullptr) {
  // Load the library with an option preferring own symbols. Otherwise the
  // library will try to call, e.g., external tflite, which leads to crash.
  base::NativeLibraryOptions native_library_options;
  native_library_options.prefer_own_symbols = true;
  library_.emplace(base::LoadNativeLibraryWithOptions(
      base::FilePath(library_path), native_library_options, nullptr));
  if (!library_->is_valid()) {
    status_ = Status::kLoadLibraryFailed;
    return;
  }

// Helper macro to look up functions from the library, assuming the function
// pointer type is named as (name+"Fn"), which is the case in
// "libhandwriting/interface.h".
#define ML_SODA_LOOKUP_FUNCTION(function_ptr, name)                    \
  function_ptr =                                                       \
      reinterpret_cast<name##Fn>(library_->GetFunctionPointer(#name)); \
  if (function_ptr == NULL) {                                          \
    status_ = Status::kFunctionLookupFailed;                           \
    return;                                                            \
  }
  // Look up the function pointers.
  ML_SODA_LOOKUP_FUNCTION(create_soda_async_, CreateSodaAsync);
  ML_SODA_LOOKUP_FUNCTION(add_audio_, AddAudio);
  ML_SODA_LOOKUP_FUNCTION(delete_soda_async_, DeleteSodaAsync);

  ML_SODA_LOOKUP_FUNCTION(create_extended_soda_async_, CreateExtendedSodaAsync);
  ML_SODA_LOOKUP_FUNCTION(delete_extended_soda_async_, DeleteExtendedSodaAsync);
  ML_SODA_LOOKUP_FUNCTION(extended_add_audio_, ExtendedAddAudio);
  ML_SODA_LOOKUP_FUNCTION(extended_soda_stop_, ExtendedSodaStop);
  ML_SODA_LOOKUP_FUNCTION(extended_soda_start_, ExtendedSodaStart);
  ML_SODA_LOOKUP_FUNCTION(extended_soda_mark_done_, ExtendedSodaMarkDone);
#undef ML_SODA_LOOKUP_FUNCTION

  status_ = Status::kOk;
}

SodaLibrary::Status SodaLibrary::GetStatus() const {
  return status_;
}

SodaLibrary* SodaLibrary::GetInstance() {
  return GetInstanceAt(kSodaLibraryPath);
}

SodaLibrary* SodaLibrary::GetInstanceAt(const std::string& library_path) {
  static base::NoDestructor<std::unordered_map<std::string, SodaLibrary*>>
      instances;
  auto* const std_map = instances.get();
  auto it = std_map->find(library_path);
  SodaLibrary* instance;
  if (it == std_map->end()) {
    // make a new one!
    instance = new SodaLibrary(library_path);
    std_map->insert({library_path, instance});
  } else {
    instance = it->second;
  }
  return instance;
}

// Proxy functions to the library function pointers.
void* SodaLibrary::CreateSodaAsync(const SodaConfig& config) const {
  DCHECK(status_ == Status::kOk);
  return (*create_soda_async_)(config);
}

void SodaLibrary::AddAudio(void* soda_async_handle,
                           const char* audio_buffer,
                           int audio_buffer_size) const {
  DCHECK(status_ == Status::kOk);
  (*add_audio_)(soda_async_handle, audio_buffer, audio_buffer_size);
}

void SodaLibrary::AddAudio(void* soda_async_handle,
                           const std::string& audio_buffer) const {
  AddAudio(soda_async_handle, audio_buffer.c_str(), audio_buffer.size());
}

void SodaLibrary::DeleteSodaAsync(void* soda_async_handle) const {
  DCHECK(status_ == Status::kOk);
  (*delete_soda_async_)(soda_async_handle);
}

// Extended APIs
void* SodaLibrary::CreateExtendedSodaAsync(
    const ExtendedSodaConfig& config) const {
  DCHECK(status_ == Status::kOk);
  return (*create_extended_soda_async_)(config);
}

void SodaLibrary::DeleteExtendedSodaAsync(
    void* extended_soda_async_handle) const {
  DCHECK(status_ == Status::kOk);
  (*delete_extended_soda_async_)(extended_soda_async_handle);
}

void SodaLibrary::ExtendedAddAudio(void* extended_soda_async_handle,
                                   const std::string& audio) const {
  DCHECK(status_ == Status::kOk);
  (*extended_add_audio_)(extended_soda_async_handle, audio.c_str(),
                         audio.size());
}

void SodaLibrary::ExtendedSodaStop(void* extended_soda_async_handle) const {
  DCHECK(status_ == Status::kOk);
  (*extended_soda_stop_)(extended_soda_async_handle);
}

void SodaLibrary::ExtendedSodaStart(void* extended_soda_async_handle) const {
  DCHECK(status_ == Status::kOk);
  (*extended_soda_start_)(extended_soda_async_handle);
}

void SodaLibrary::ExtendedSodaMarkDone(void* extended_soda_async_handle) const {
  DCHECK(status_ == Status::kOk);
  (*extended_soda_mark_done_)(extended_soda_async_handle);
}

}  // namespace ml
