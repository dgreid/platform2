// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines messages used for communication between Trichechus and Cronista for storing and
//! retrieving persistent data.

use serde::{Deserialize, Serialize};
use sirenia_rpc_macros::sirenia_rpc;

/// Represents the possible status codes from an RPC.
/// Values are assigned to make it easier to interface with D-Bus.
#[derive(Debug, Deserialize, Serialize)]
pub enum Status {
    Success = 0,
    Failure = 1,
}

/// Should the data be globally available or only available within the users' sessions.
/// Values are assigned to make it easier to interface with D-Bus.
#[derive(Debug, Deserialize, Serialize)]
pub enum Scope {
    System = 0,
    Session = 1,
    Test = -1,
}

#[sirenia_rpc]
pub trait Cronista {
    type Error;

    //TODO These need to carry enough information to prove the entry was recorded in the log.
    fn persist(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
        data: Vec<u8>,
    ) -> std::result::Result<Status, Self::Error>;
    fn retrieve(
        &self,
        scope: Scope,
        domain: String,
        identifier: String,
    ) -> std::result::Result<(Status, Vec<u8>), Self::Error>;
}
