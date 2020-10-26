// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libwebserv/response.h>

#include <base/json/json_writer.h>
#include <base/values.h>
#include <brillo/http/http_request.h>
#include <brillo/mime_utils.h>
#include <brillo/streams/memory_stream.h>

namespace libwebserv {

void Response::AddHeader(const std::string& header_name,
                         const std::string& value) {
  AddHeaders({std::pair<std::string, std::string>{header_name, value}});
}

void Response::ReplyWithText(int status_code,
                             const std::string& text,
                             const std::string& mime_type) {
  Reply(status_code, brillo::MemoryStream::OpenCopyOf(text, nullptr),
        mime_type);
}

void Response::ReplyWithJson(int status_code, const base::Value& json) {
  std::string text;
  base::JSONWriter::WriteWithOptions(
      json, base::JSONWriter::OPTIONS_PRETTY_PRINT, &text);
  std::string mime_type = brillo::mime::AppendParameter(
      brillo::mime::application::kJson, brillo::mime::parameters::kCharset,
      "utf-8");
  ReplyWithText(status_code, text, mime_type);
}

void Response::ReplyWithJson(int status_code, const base::Value* json) {
  ReplyWithJson(status_code, *json);
}

void Response::ReplyWithJson(int status_code,
                             const std::map<std::string, std::string>& json) {
  base::Value json_value(base::Value::Type::DICTIONARY);
  for (const auto& pair : json) {
    json_value.SetStringPath(pair.first, pair.second);
  }
  ReplyWithJson(status_code, json_value);
}

void Response::Redirect(int status_code, const std::string& redirect_url) {
  AddHeader(brillo::http::response_header::kLocation, redirect_url);
  ReplyWithError(status_code, "");
}

void Response::ReplyWithError(int status_code, const std::string& error_text) {
  Reply(status_code, brillo::MemoryStream::OpenCopyOf(error_text, nullptr),
        "text/plain");
}

void Response::ReplyWithErrorNotFound() {
  ReplyWithError(brillo::http::status_code::NotFound, "Not Found");
}

}  // namespace libwebserv
