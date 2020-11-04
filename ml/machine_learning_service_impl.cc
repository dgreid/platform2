// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/machine_learning_service_impl.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/memory_mapped_file.h>
#include <tensorflow/lite/model.h>
#include <unicode/putil.h>
#include <unicode/udata.h>
#include <utils/memory/mmap.h>

#include "ml/grammar_checker_impl.h"
#include "ml/grammar_library.h"
#include "ml/handwriting.h"
#include "ml/handwriting_recognizer_impl.h"
#include "ml/model_impl.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/mojom/soda.mojom.h"
#include "ml/request_metrics.h"
#include "ml/soda_recognizer_impl.h"
#include "ml/text_classifier_impl.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpecPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizer;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerSpec;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr;
using ::chromeos::machine_learning::mojom::LoadHandwritingModelResult;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningService;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::SodaClient;
using ::chromeos::machine_learning::mojom::SodaConfigPtr;
using ::chromeos::machine_learning::mojom::SodaRecognizer;
using ::chromeos::machine_learning::mojom::TextClassifier;

constexpr char kSystemModelDir[] = "/opt/google/chrome/ml_models/";
// Base name for UMA metrics related to model loading (`LoadBuiltinModel`,
// `LoadFlatBufferModel`, `LoadTextClassifier` or LoadHandwritingModel).
constexpr char kMetricsRequestName[] = "LoadModelResult";

constexpr char kIcuDataFilePath[] = "/opt/google/chrome/icudtl.dat";

// Used to hold the mmap object of the icu data file. Each process should only
// have one instance of it. Intentionally never close it.
// We can not make it as a member of `MachineLearningServiceImpl` because it
// will crash the unit test (because in that case, when the
// `MachineLearningServiceImpl` object is destructed, the file will be
// unmapped but the icu data can not be reset in the testing process).
base::MemoryMappedFile* g_icu_data_mmap_file = nullptr;

void InitIcuIfNeeded() {
  if (!g_icu_data_mmap_file) {
    g_icu_data_mmap_file = new base::MemoryMappedFile();
    CHECK(g_icu_data_mmap_file->Initialize(
        base::FilePath(kIcuDataFilePath),
        base::MemoryMappedFile::Access::READ_ONLY));
    // Init the Icu library.
    UErrorCode err = U_ZERO_ERROR;
    udata_setCommonData(const_cast<uint8_t*>(g_icu_data_mmap_file->data()),
                        &err);
    DCHECK(err == U_ZERO_ERROR);
    // Never try to load Icu data from files.
    udata_setFileAccess(UDATA_ONLY_PACKAGES, &err);
    DCHECK(err == U_ZERO_ERROR);
  }
}

}  // namespace

MachineLearningServiceImpl::MachineLearningServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::Closure disconnect_handler,
    const std::string& model_dir)
    : builtin_model_metadata_(GetBuiltinModelMetadata()),
      model_dir_(model_dir),
      receiver_(this,
                mojo::InterfaceRequest<
                    chromeos::machine_learning::mojom::MachineLearningService>(
                    std::move(pipe))) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

MachineLearningServiceImpl::MachineLearningServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::Closure disconnect_handler,
    dbus::Bus* bus)
    : MachineLearningServiceImpl(
          std::move(pipe), std::move(disconnect_handler), kSystemModelDir) {
  if (bus) {
    dlcservice_client_ = std::make_unique<DlcserviceClient>(bus);
  }
}

void MachineLearningServiceImpl::Clone(
    mojo::PendingReceiver<MachineLearningService> receiver) {
  clone_receivers_.Add(this, std::move(receiver));
}

void MachineLearningServiceImpl::LoadBuiltinModel(
    BuiltinModelSpecPtr spec,
    mojo::PendingReceiver<Model> receiver,
    LoadBuiltinModelCallback callback) {
  // Unsupported models do not have metadata entries.
  const auto metadata_lookup = builtin_model_metadata_.find(spec->id);
  if (metadata_lookup == builtin_model_metadata_.end()) {
    LOG(WARNING) << "LoadBuiltinModel requested for unsupported model ID "
                 << spec->id << ".";
    std::move(callback).Run(LoadModelResult::MODEL_SPEC_ERROR);
    RecordModelSpecificationErrorEvent();
    return;
  }

  const BuiltinModelMetadata& metadata = metadata_lookup->second;

  DCHECK(!metadata.metrics_model_name.empty());

  RequestMetrics request_metrics(metadata.metrics_model_name,
                                 kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Attempt to load model.
  const std::string model_path = model_dir_ + metadata.model_file;
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model file '" << model_path << "'.";
    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  ModelImpl::Create(metadata.required_inputs, metadata.required_outputs,
                    std::move(model), std::move(receiver),
                    metadata.metrics_model_name);

  std::move(callback).Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void MachineLearningServiceImpl::LoadFlatBufferModel(
    FlatBufferModelSpecPtr spec,
    mojo::PendingReceiver<Model> receiver,
    LoadFlatBufferModelCallback callback) {
  DCHECK(!spec->metrics_model_name.empty());

  RequestMetrics request_metrics(spec->metrics_model_name, kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Take the ownership of the content of `model_string` because `ModelImpl` has
  // to hold the memory.
  auto model_data =
      std::make_unique<AlignedModelData>(std::move(spec->model_string));

  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::VerifyAndBuildFromBuffer(model_data->data(),
                                                        model_data->size());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model string of metric name: "
               << spec->metrics_model_name << "'.";
    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  ModelImpl::Create(
      std::map<std::string, int>(spec->inputs.begin(), spec->inputs.end()),
      std::map<std::string, int>(spec->outputs.begin(), spec->outputs.end()),
      std::move(model), std::move(model_data), std::move(receiver),
      spec->metrics_model_name);

  std::move(callback).Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void MachineLearningServiceImpl::LoadTextClassifier(
    mojo::PendingReceiver<TextClassifier> receiver,
    LoadTextClassifierCallback callback) {
  RequestMetrics request_metrics("TextClassifier", kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Create the TextClassifier.
  if (!TextClassifierImpl::Create(std::move(receiver))) {
    LOG(ERROR) << "Failed to create TextClassifierImpl object.";
    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // initialize the icu library.
  InitIcuIfNeeded();

  std::move(callback).Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void LoadHandwritingModelFromDir(
    HandwritingRecognizerSpecPtr spec,
    mojo::PendingReceiver<HandwritingRecognizer> receiver,
    MachineLearningServiceImpl::LoadHandwritingModelCallback callback,
    const std::string& root_path) {
  RequestMetrics request_metrics("HandwritingModel", kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Returns error if root_path is empty.
  if (root_path.empty()) {
    std::move(callback).Run(LoadHandwritingModelResult::DLC_GET_PATH_ERROR);
    request_metrics.RecordRequestEvent(
        LoadHandwritingModelResult::DLC_GET_PATH_ERROR);
    return;
  }

  // Load HandwritingLibrary.
  auto* const hwr_library = ml::HandwritingLibrary::GetInstance(root_path);

  if (hwr_library->GetStatus() != ml::HandwritingLibrary::Status::kOk) {
    LOG(ERROR) << "Initialize ml::HandwritingLibrary with error "
               << static_cast<int>(hwr_library->GetStatus());

    switch (hwr_library->GetStatus()) {
      case ml::HandwritingLibrary::Status::kLoadLibraryFailed: {
        std::move(callback).Run(
            LoadHandwritingModelResult::LOAD_NATIVE_LIB_ERROR);
        request_metrics.RecordRequestEvent(
            LoadHandwritingModelResult::LOAD_NATIVE_LIB_ERROR);
        return;
      }
      case ml::HandwritingLibrary::Status::kFunctionLookupFailed: {
        std::move(callback).Run(
            LoadHandwritingModelResult::LOAD_FUNC_PTR_ERROR);
        request_metrics.RecordRequestEvent(
            LoadHandwritingModelResult::LOAD_FUNC_PTR_ERROR);
        return;
      }
      default: {
        std::move(callback).Run(LoadHandwritingModelResult::LOAD_MODEL_ERROR);
        request_metrics.RecordRequestEvent(
            LoadHandwritingModelResult::LOAD_MODEL_ERROR);
        return;
      }
    }
  }

  // Create HandwritingRecognizer.
  if (!HandwritingRecognizerImpl::Create(std::move(spec),
                                         std::move(receiver))) {
    LOG(ERROR) << "LoadHandwritingRecognizer returned false.";
    std::move(callback).Run(LoadHandwritingModelResult::LOAD_MODEL_FILES_ERROR);
    request_metrics.RecordRequestEvent(
        LoadHandwritingModelResult::LOAD_MODEL_FILES_ERROR);
    return;
  }

  std::move(callback).Run(LoadHandwritingModelResult::OK);
  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadHandwritingModelResult::OK);
}

void MachineLearningServiceImpl::LoadHandwritingModel(
    chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec,
    mojo::PendingReceiver<
        chromeos::machine_learning::mojom::HandwritingRecognizer> receiver,
    LoadHandwritingModelCallback callback) {
  // If handwriting is installed on rootfs, load it from there.
  if (ml::HandwritingLibrary::IsUseLibHandwritingEnabled()) {
    LoadHandwritingModelFromDir(
        std::move(spec), std::move(receiver), std::move(callback),
        ml::HandwritingLibrary::kHandwritingDefaultModelDir);
    return;
  }

  // If handwriting is installed as DLC, get the dir and subsequently load it
  // from there.
  if (ml::HandwritingLibrary::IsUseLibHandwritingDlcEnabled()) {
    dlcservice_client_->GetDlcRootPath(
        "libhandwriting",
        base::BindOnce(&LoadHandwritingModelFromDir, std::move(spec),
                       std::move(receiver), std::move(callback)));
    return;
  }

  // If handwriting is not on rootfs and not in DLC, this function should not
  // be called.
  LOG(ERROR) << "Calling LoadHandwritingModel without Handwriting enabled "
                "should never happen.";
  std::move(callback).Run(LoadHandwritingModelResult::LOAD_MODEL_ERROR);
}

void MachineLearningServiceImpl::LoadHandwritingModelWithSpec(
    HandwritingRecognizerSpecPtr spec,
    mojo::PendingReceiver<HandwritingRecognizer> receiver,
    LoadHandwritingModelWithSpecCallback callback) {
  RequestMetrics request_metrics("HandwritingModel", kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Load HandwritingLibrary.
  auto* const hwr_library = ml::HandwritingLibrary::GetInstance();

  if (hwr_library->GetStatus() ==
      ml::HandwritingLibrary::Status::kNotSupported) {
    LOG(ERROR) << "Initialize ml::HandwritingLibrary with error "
               << static_cast<int>(hwr_library->GetStatus());

    std::move(callback).Run(LoadModelResult::FEATURE_NOT_SUPPORTED_ERROR);
    request_metrics.RecordRequestEvent(
        LoadModelResult::FEATURE_NOT_SUPPORTED_ERROR);
    return;
  }

  if (hwr_library->GetStatus() != ml::HandwritingLibrary::Status::kOk) {
    LOG(ERROR) << "Initialize ml::HandwritingLibrary with error "
               << static_cast<int>(hwr_library->GetStatus());

    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // Create HandwritingRecognizer.
  if (!HandwritingRecognizerImpl::Create(std::move(spec),
                                         std::move(receiver))) {
    LOG(ERROR) << "LoadHandwritingRecognizer returned false.";
    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  std::move(callback).Run(LoadModelResult::OK);
  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void MachineLearningServiceImpl::LoadSpeechRecognizer(
    SodaConfigPtr config,
    mojo::PendingRemote<SodaClient> soda_client,
    mojo::PendingReceiver<SodaRecognizer> soda_recognizer,
    LoadSpeechRecognizerCallback callback) {
  RequestMetrics request_metrics("Soda", kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Create the SodaRecognizer.
  if (!SodaRecognizerImpl::Create(std::move(config), std::move(soda_client),
                                  std::move(soda_recognizer))) {
    LOG(ERROR) << "Failed to create SodaRecognizerImpl object.";
    // TODO(robsc): it may be better that SODA has its specific enum values to
    // return, similar to handwriting. So before we finalize the impl of SODA
    // Mojo API, we may revisit this return value.
    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  std::move(callback).Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void MachineLearningServiceImpl::LoadGrammarChecker(
    mojo::PendingReceiver<chromeos::machine_learning::mojom::GrammarChecker>
        receiver,
    LoadGrammarCheckerCallback callback) {
  RequestMetrics request_metrics("GrammarChecker", kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Load GrammarLibrary.
  auto* const grammar_library = ml::GrammarLibrary::GetInstance();

  if (grammar_library->GetStatus() ==
      ml::GrammarLibrary::Status::kNotSupported) {
    LOG(ERROR) << "Initialize ml::GrammarLibrary with error "
               << static_cast<int>(grammar_library->GetStatus());

    std::move(callback).Run(LoadModelResult::FEATURE_NOT_SUPPORTED_ERROR);
    request_metrics.RecordRequestEvent(
        LoadModelResult::FEATURE_NOT_SUPPORTED_ERROR);
    return;
  }

  if (grammar_library->GetStatus() != ml::GrammarLibrary::Status::kOk) {
    LOG(ERROR) << "Initialize ml::GrammarLibrary with error "
               << static_cast<int>(grammar_library->GetStatus());

    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // Create GrammarChecker.
  if (!GrammarCheckerImpl::Create(std::move(receiver))) {
    std::move(callback).Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  std::move(callback).Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

}  // namespace ml
