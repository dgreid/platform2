// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/grammar_checker_impl.h"

#include <utility>
#include <vector>

#include "ml/grammar_proto_mojom_conversion.h"
#include "ml/request_metrics.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::GrammarChecker;
using ::chromeos::machine_learning::mojom::GrammarCheckerCandidatePtr;
using ::chromeos::machine_learning::mojom::GrammarCheckerQueryPtr;
using ::chromeos::machine_learning::mojom::GrammarCheckerResult;

}  // namespace

bool GrammarCheckerImpl::Create(
    mojo::PendingReceiver<GrammarChecker> receiver) {
  auto checker_impl = new GrammarCheckerImpl(std::move(receiver));

  // Set the disconnection handler to strongly bind `checker_impl` to delete
  // `checker_impl` when the connection is gone.
  checker_impl->receiver_.set_disconnect_handler(base::Bind(
      [](const GrammarCheckerImpl* const checker_impl) { delete checker_impl; },
      base::Unretained(checker_impl)));

  return checker_impl->successfully_loaded_;
}

GrammarCheckerImpl::GrammarCheckerImpl(
    mojo::PendingReceiver<GrammarChecker> receiver)
    : library_(ml::GrammarLibrary::GetInstance()),
      receiver_(this, std::move(receiver)) {
  DCHECK(library_->GetStatus() == ml::GrammarLibrary::Status::kOk)
      << "GrammarCheckerImpl should be created only if GrammarLibrary is "
         "initialized successfully.";

  checker_ = library_->CreateGrammarChecker();

  successfully_loaded_ = library_->LoadGrammarChecker(checker_);
}

GrammarCheckerImpl::~GrammarCheckerImpl() {
  library_->DestroyGrammarChecker(checker_);
}

void GrammarCheckerImpl::Check(GrammarCheckerQueryPtr query,
                               CheckCallback callback) {
  RequestMetrics request_metrics("GrammarChecker", "Check");
  request_metrics.StartRecordingPerformanceMetrics();

  chrome_knowledge::GrammarCheckerResult result_proto;

  if (library_->CheckGrammar(checker_,
                             GrammarCheckerQueryToProto(std::move(query)),
                             &result_proto)) {
    // Check succeeded, run callback on the result.
    std::move(callback).Run(GrammarCheckerResultFromProto(result_proto));
    request_metrics.FinishRecordingPerformanceMetrics();
    request_metrics.RecordRequestEvent(GrammarCheckerResult::Status::OK);
  } else {
    // Check failed, run callback on empty result and status = ERROR.
    std::move(callback).Run(
        GrammarCheckerResult::New(GrammarCheckerResult::Status::ERROR,
                                  std::vector<GrammarCheckerCandidatePtr>()));
    request_metrics.RecordRequestEvent(GrammarCheckerResult::Status::ERROR);
  }
}

}  // namespace ml
