// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "print_tools/ipp_in_json.h"

#include <memory>
#include <utility>

#include <base/json/json_writer.h>
#include <base/macros.h>
#include <base/optional.h>
#include <base/values.h>

namespace {

base::Value SaveAsJson(const ipp::Collection* coll);

// It saves a single value (at given index) from the attribute as JSON
// structure. The parameter "attr" cannot be nullptr, "index" must be correct.
base::Value SaveAsJson(const ipp::Attribute* attr, unsigned index) {
  CHECK(attr != nullptr);
  CHECK(index < attr->GetSize());
  using AttrType = ipp::AttrType;
  switch (attr->GetType()) {
    case AttrType::integer: {
      int vi;
      attr->GetValue(&vi, index);
      return base::Value(vi);
    }
    case AttrType::boolean: {
      int vb;
      attr->GetValue(&vb, index);
      return base::Value(static_cast<bool>(vb));
    }
    case AttrType::enum_: {
      std::string vs;
      attr->GetValue(&vs, index);
      if (vs.empty()) {
        int vi;
        attr->GetValue(&vi, index);
        return base::Value(vi);
      }
      return base::Value(vs);
    }
    case AttrType::collection:
      return SaveAsJson(attr->GetCollection(index));
    case AttrType::text:
    case AttrType::name: {
      ipp::StringWithLanguage vs;
      attr->GetValue(&vs, index);
      if (vs.language.empty())
        return base::Value(vs.value);
      base::Value obj(base::Value::Type::DICTIONARY);
      obj.SetStringKey("value", vs.value);
      obj.SetStringKey("language", vs.language);
      return obj;
    }
    case AttrType::dateTime:
    case AttrType::resolution:
    case AttrType::rangeOfInteger:
    case AttrType::octetString:
    case AttrType::keyword:
    case AttrType::uri:
    case AttrType::uriScheme:
    case AttrType::charset:
    case AttrType::naturalLanguage:
    case AttrType::mimeMediaType: {
      std::string vs;
      attr->GetValue(&vs, index);
      return base::Value(vs);
    }
  }
  return base::Value();  // not reachable
}

// It saves all attribute's values as JSON structure.
// The parameter "attr" cannot be nullptr.
base::Value SaveAsJson(const ipp::Attribute* attr) {
  CHECK(attr != nullptr);
  if (attr->IsASet()) {
    base::Value arr(base::Value::Type::LIST);
    const unsigned size = attr->GetSize();
    for (unsigned i = 0; i < size; ++i)
      arr.Append(SaveAsJson(attr, i));
    return arr;
  } else {
    return SaveAsJson(attr, 0);
  }
}

// It saves a given Collection as JSON object.
// The parameter "coll" cannot be nullptr.
base::Value SaveAsJson(const ipp::Collection* coll) {
  CHECK(coll != nullptr);
  base::Value obj(base::Value::Type::DICTIONARY);
  std::vector<const ipp::Attribute*> attrs = coll->GetAllAttributes();

  for (auto a : attrs) {
    auto state = a->GetState();
    if (state == ipp::AttrState::unset)
      continue;

    if (state == ipp::AttrState::set) {
      base::Value obj2(base::Value::Type::DICTIONARY);
      obj2.SetStringKey("type", ToString(a->GetType()));
      obj2.SetKey("value", SaveAsJson(a));
      obj.SetKey(a->GetName(), std::move(obj2));
    } else {
      obj.SetStringKey(a->GetName(), ToString(state));
    }
  }

  return obj;
}

// It saves all groups from given Package as JSON object.
// The parameter "pkg" cannot be nullptr.
base::Value SaveAsJson(const ipp::Package* pkg) {
  CHECK(pkg != nullptr);
  base::Value obj(base::Value::Type::DICTIONARY);
  std::vector<const ipp::Group*> groups = pkg->GetAllGroups();

  for (auto g : groups) {
    const size_t size = g->GetSize();
    if (size == 0)
      continue;
    if (g->IsASet()) {
      base::Value arr(base::Value::Type::LIST);
      for (size_t i = 0; i < size; ++i)
        arr.Append(SaveAsJson(g->GetCollection(i)));
      obj.SetKey(ToString(g->GetName()), std::move(arr));
    } else {
      obj.SetKey(ToString(g->GetName()), SaveAsJson(g->GetCollection()));
    }
  }

  return obj;
}

// Saves given logs as JSON array.
base::Value SaveAsJson(const std::vector<ipp::Log>& log) {
  base::Value arr(base::Value::Type::LIST);
  for (const auto& l : log) {
    base::Value obj(base::Value::Type::DICTIONARY);
    obj.SetStringKey("message", l.message);
    if (!l.frame_context.empty())
      obj.SetStringKey("frame_context", l.frame_context);
    if (!l.parser_context.empty())
      obj.SetStringKey("parser_context", l.parser_context);
    arr.Append(std::move(obj));
  }
  return arr;
}

}  // namespace

bool ConvertToJson(const ipp::Response& response,
                   const std::vector<ipp::Log>& log,
                   bool compressed_json,
                   std::string* json) {
  // Build structure.
  base::Value doc(base::Value::Type::DICTIONARY);
  doc.SetStringKey("status", ipp::ToString(response.StatusCode()));
  if (!log.empty()) {
    doc.SetKey("parsing_logs", SaveAsJson(log));
  }
  doc.SetKey("response", SaveAsJson(&response));
  // Convert to JSON.
  bool result;
  if (compressed_json) {
    result = base::JSONWriter::Write(doc, json);
  } else {
    const int options = base::JSONWriter::OPTIONS_PRETTY_PRINT;
    result = base::JSONWriter::WriteWithOptions(doc, options, json);
  }
  return result;
}
