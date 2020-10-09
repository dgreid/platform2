// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_TEST_UTIL_H_
#define LORGNETTE_TEST_UTIL_H_

#include <ostream>
#include <string>

#include <gmock/gmock.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

namespace lorgnette {

void PrintTo(const lorgnette::DocumentSource& ds, std::ostream* os);

DocumentSource CreateDocumentSource(const std::string& name,
                                    SourceType type,
                                    double width,
                                    double height);

MATCHER_P(EqualsDocumentSource, expected, "") {
  if (arg.type() != expected.type()) {
    *result_listener << "type " << SourceType_Name(arg.type())
                     << " does not match expected type "
                     << SourceType_Name(expected.type());
    return false;
  }

  if (arg.name() != expected.name()) {
    *result_listener << "name " << arg.name()
                     << " does not match expected name " << expected.name();
    return false;
  }

  if (arg.has_area() != expected.has_area()) {
    *result_listener << (arg.has_area() ? "has area" : "does not have area")
                     << " but expected to "
                     << (expected.has_area() ? "have area" : "not have area");
    return false;
  }

  if (arg.area().width() != expected.area().width()) {
    *result_listener << "width " << arg.area().width()
                     << " does not match expected width "
                     << expected.area().width();
    return false;
  }

  if (arg.area().height() != expected.area().height()) {
    *result_listener << "height " << arg.area().height()
                     << " does not match expected height "
                     << expected.area().height();
    return false;
  }

  return true;
}

}  // namespace lorgnette

#endif  // LORGNETTE_TEST_UTIL_H_
