// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides type safe interfaces for each operation exposed by Chrome's
//! VideoDecodeAccelerator.

use std::os::raw::c_void;

use super::bindings;
use super::format::*;
use super::session::*;
use crate::error::*;
use crate::format::*;

/// Represents a backend implementation of libvda.
pub enum VdaImplType {
    Fake,
    Gavda, // GpuArcVideoDecodeAccelerator
}

/// Represents decoding capabilities of libvda instances.
pub struct Capabilities {
    pub input_formats: Vec<InputFormat>,
    pub output_formats: Vec<PixelFormat>,
}

/// Represents a libvda instance.
pub struct VdaInstance {
    // `raw_ptr` must be a valid pointer obtained from `decode_bindings::initialize`.
    raw_ptr: *mut c_void,
    caps: Capabilities,
}

impl VdaInstance {
    /// Creates VdaInstance. `typ` specifies which backend will be used.
    pub fn new(typ: VdaImplType) -> Result<Self> {
        let impl_type = match typ {
            VdaImplType::Fake => bindings::vda_impl_type_FAKE,
            VdaImplType::Gavda => bindings::vda_impl_type_GAVDA,
        };

        // Safe because libvda's API is called properly.
        let raw_ptr = unsafe { bindings::initialize(impl_type) };
        if raw_ptr.is_null() {
            return Err(Error::InstanceInitFailure);
        }

        // Get available input/output formats.
        // Safe because libvda's API is called properly.
        let vda_cap_ptr = unsafe { bindings::get_vda_capabilities(raw_ptr) };
        if vda_cap_ptr.is_null() {
            return Err(Error::GetCapabilitiesFailure);
        }
        // Safe because `vda_cap_ptr` is not NULL.
        let vda_cap = unsafe { *vda_cap_ptr };

        // Safe because `input_formats` is valid for |`num_input_formats`| elements if both are valid.
        let input_formats = unsafe {
            InputFormat::from_raw_parts(vda_cap.input_formats, vda_cap.num_input_formats)?
        };

        // Output formats
        // Safe because `output_formats` is valid for |`num_output_formats`| elements if both are valid.
        let output_formats = unsafe {
            PixelFormat::from_raw_parts(vda_cap.output_formats, vda_cap.num_output_formats)?
        };

        Ok(VdaInstance {
            raw_ptr,
            caps: Capabilities {
                input_formats,
                output_formats,
            },
        })
    }

    /// Get media capabilities.
    pub fn get_capabilities(&self) -> &Capabilities {
        &self.caps
    }

    /// Opens a new `Session` for a given `Profile`.
    pub fn open_session<'a>(&'a self, profile: Profile) -> Result<Session<'a>> {
        // Safe because `self.raw_ptr` is a non-NULL pointer obtained from `bindings::initialize`
        // in `VdaInstance::new`.
        unsafe { Session::new(self.raw_ptr, profile).ok_or(Error::SessionInitFailure(profile)) }
    }
}

impl Drop for VdaInstance {
    fn drop(&mut self) {
        // Safe because libvda's API is called properly.
        unsafe { bindings::deinitialize(self.raw_ptr) }
    }
}
