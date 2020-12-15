// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines messages used for communication between Trichechus and Cronista for storing and
//! retrieving persistent data.

use serde::{Deserialize, Serialize};

use crate::storage::StorableMember;

//TODO These messages also need to carry enough information to prove the entry was recorded in the
//log.
#[derive(Deserialize, Serialize)]
pub enum Request {
    Persist {
        domain: String,
        identifier: String,
        data: StorableMember,
    },
    Retrieve {
        domain: String,
        identifier: String,
    },
}

#[derive(Deserialize, Serialize)]
pub enum Response {
    Persist { status: i32 },
    Retrieve { status: i32, data: StorableMember },
}
