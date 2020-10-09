// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/test_util.h"

namespace lorgnette {

void PrintTo(const lorgnette::DocumentSource& ds, std::ostream* os) {
  *os << "DocumentSource(" << std::endl;
  *os << "  name = " << ds.name() << "," << std::endl;
  *os << "  type = " << SourceType_Name(ds.type()) << "," << std::endl;

  if (ds.has_area()) {
    *os << "  area.width = " << ds.area().width() << "," << std::endl;
    *os << "  area.height = " << ds.area().height() << "," << std::endl;
  }

  *os << ")";
}

DocumentSource CreateDocumentSource(const std::string& name,
                                    SourceType type,
                                    double width,
                                    double height) {
  DocumentSource source;
  source.set_name(name);
  source.set_type(type);
  source.mutable_area()->set_width(width);
  source.mutable_area()->set_height(height);
  return source;
}

}  // namespace lorgnette
