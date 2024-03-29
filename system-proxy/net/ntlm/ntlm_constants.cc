// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/net/ntlm/ntlm_constants.h"

#include <utility>

namespace net {

namespace ntlm {

AvPair::AvPair() = default;

AvPair::AvPair(TargetInfoAvId avid, uint16_t avlen)
    : avid(avid), avlen(avlen) {}

AvPair::AvPair(TargetInfoAvId avid, std::vector<uint8_t> buffer)
    : buffer(std::move(buffer)), avid(avid) {
  avlen = this->buffer.size();
}

AvPair::AvPair(const AvPair& other) = default;

AvPair::AvPair(AvPair&& other) = default;

AvPair::~AvPair() = default;

AvPair& AvPair::operator=(const AvPair& other) = default;

AvPair& AvPair::operator=(AvPair&& other) = default;

}  // namespace ntlm
}  // namespace net
