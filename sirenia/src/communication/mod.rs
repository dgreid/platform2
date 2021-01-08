// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module that handles the communication api for sending messages between
//! Dugong and Trichechus

use std::fmt::Debug;
use std::result::Result as StdResult;

use serde::{Deserialize, Serialize};
use sirenia_rpc_macros::sirenia_rpc;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AppInfo {
    pub app_id: String,
    pub port_number: u32,
}

/// Definition for the rpc between TEEs and Trichechus
#[sirenia_rpc]
pub trait TEEStorage {
    type Error;

    fn read_data(&self, id: String) -> StdResult<Vec<u8>, Self::Error>;
    fn write_data(&self, id: String, data: Vec<u8>) -> StdResult<(), Self::Error>;
}

#[sirenia_rpc]
pub trait Trichechus {
    type Error;

    fn start_session(&self, app_info: AppInfo) -> StdResult<(), Self::Error>;
    fn get_logs(&self) -> StdResult<Vec<String>, Self::Error>;
}
