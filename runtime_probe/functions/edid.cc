// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/edid.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <pcrecpp.h>

#include <numeric>
#include <utility>

#include "runtime_probe/utils/edid.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

namespace {
constexpr char kSysfsDrmPath[] = "/sys/class/drm/*";
}  // namespace

std::unique_ptr<EdidFunction> EdidFunction::FromKwargsValue(
    const base::Value& dict_value) {
  if (dict_value.DictSize() != 0)
    return nullptr;

  auto instance = std::make_unique<EdidFunction>();
  bool result = true;

  result &= PARSE_ARGUMENT(dir_path, {kSysfsDrmPath});

  if (!result)
    return nullptr;
  return instance;
}

EdidFunction::DataType EdidFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve edid results.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  // TODO(b/161770131): replace with TakeList() after libchrome uprev.
  return DataType(std::move(json_output->GetList()));
}

int EdidFunction::EvalInHelper(std::string* output) const {
  base::Value result(base::Value::Type::LIST);

  // Store paths which have been evaluated.
  base::Value evaluated_path(base::Value::Type::DICTIONARY);
  for (const auto& dir_path : dir_path_) {
    for (const auto& edid_path : GetEdidPaths(base::FilePath(dir_path))) {
      if (evaluated_path.FindKey(edid_path.value()))
        continue;

      auto node_res = EvalInHelperByPath(edid_path);
      if (node_res.DictEmpty()) {
        evaluated_path.SetBoolKey(edid_path.value(), false);
        continue;
      }
      evaluated_path.SetBoolKey(edid_path.value(), true);
      result.GetList().push_back(std::move(node_res));
    }
  }

  if (!base::JSONWriter::Write(result, output)) {
    LOG(ERROR) << "Failed to serialize edid probed result to json string";
    return -1;
  }
  return 0;
}

std::vector<base::FilePath> EdidFunction::GetEdidPaths(
    const base::FilePath& glob_path) const {
  std::vector<base::FilePath> edid_list;

  const auto glob_root = glob_path.DirName();
  const auto glob_pattern = glob_path.BaseName();

  base::FileEnumerator drm_it(glob_root, false,
                              base::FileEnumerator::FileType::DIRECTORIES,
                              glob_pattern.value());

  while (true) {
    const auto drm_path = drm_it.Next();
    if (drm_path.empty())
      break;

    const auto edid_path = drm_path.Append("edid");
    if (base::PathExists(edid_path)) {
      edid_list.push_back(edid_path);
    }
  }

  return edid_list;
}

base::Value EdidFunction::EvalInHelperByPath(
    const base::FilePath& edid_path) const {
  VLOG(2) << "Processing the node \"" << edid_path.value() << "\"";

  std::string raw_bytes;
  if (!base::ReadFileToString(edid_path, &raw_bytes))
    return base::Value(base::Value::Type::DICTIONARY);
  if (raw_bytes.length() == 0) {
    return base::Value(base::Value::Type::DICTIONARY);
  }

  base::Value res(base::Value::Type::DICTIONARY);
  auto edid =
      Edid::From(std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end()));
  if (!edid) {
    return res;
  }
  res.SetStringKey("vendor", edid->vendor);
  res.SetStringKey("product_id", base::StringPrintf("%04x", edid->product_id));
  res.SetIntKey("width", edid->width);
  res.SetIntKey("height", edid->height);
  res.SetStringKey("path", edid_path.value());
  return res;
}

}  // namespace runtime_probe
