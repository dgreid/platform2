// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

mod arguments;
use arguments::Args;

#[derive(Debug)]
pub enum Error {
    ParseArgs(arguments::Error),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            ParseArgs(err) => write!(f, "Failed to parse arguments: {}", err),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

fn run() -> Result<()> {
    let argv: Vec<String> = std::env::args().collect();
    let _args = match Args::parse(&argv).map_err(Error::ParseArgs)? {
        None => return Ok(()),
        Some(args) => args,
    };

    Ok(())
}

fn main() {
    // Use run() instead of returning a Result from main() so that we can print
    // errors using Display instead of Debug.
    if let Err(e) = run() {
        eprintln!("{}", e);
    }
}
