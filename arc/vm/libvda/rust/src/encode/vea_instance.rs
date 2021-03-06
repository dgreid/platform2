// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::os::raw::c_void;

use super::bindings;
use super::format::OutputProfile;
use super::session::*;
use crate::error::*;
use crate::format::*;

/// Represents a backend implementation of libvda.
pub enum VeaImplType {
    Fake,
    Gavea, // GpuArcVideoEncoderAccelerator
}

/// Represents encoding capabilities of libvda encode instances.
pub struct EncodeCapabilities {
    pub input_formats: Vec<PixelFormat>,
    pub output_formats: Vec<OutputProfile>,
}

/// Represents a libvda encode instance.
pub struct VeaInstance {
    // `raw_ptr` must be a valid pointer obtained from `bindings::initialize`.
    raw_ptr: *mut c_void,
    caps: EncodeCapabilities,
}

/// Represents an encoding configuration for a libvda encode session.
#[derive(Debug, Clone, Copy)]
pub struct Config {
    pub input_format: PixelFormat,
    pub input_visible_width: u32,
    pub input_visible_height: u32,
    pub output_profile: Profile,
    pub initial_bitrate: u32,
    pub initial_framerate: Option<u32>,
    pub h264_output_level: Option<u8>,
}

impl Config {
    pub(crate) fn to_raw_config(self) -> bindings::vea_config_t {
        bindings::vea_config_t {
            input_format: self.input_format.to_raw_pixel_format(),
            input_visible_width: self.input_visible_width,
            input_visible_height: self.input_visible_height,
            output_profile: self.output_profile.to_raw_profile(),
            initial_bitrate: self.initial_bitrate,
            initial_framerate: self.initial_framerate.unwrap_or(0),
            has_initial_framerate: if self.initial_framerate.is_some() {
                1
            } else {
                0
            },
            h264_output_level: self.h264_output_level.unwrap_or(0),
            has_h264_output_level: if self.h264_output_level.is_some() {
                1
            } else {
                0
            },
        }
    }
}

impl VeaInstance {
    /// Creates VeaInstance. `impl_type` specifies which backend will be used.
    pub fn new(impl_type: VeaImplType) -> Result<Self> {
        let raw_impl_type = match impl_type {
            VeaImplType::Fake => bindings::vea_impl_type_VEA_FAKE,
            VeaImplType::Gavea => bindings::vea_impl_type_GAVEA,
        };

        // Safe because libvda's API is called properly.
        let raw_ptr = unsafe { bindings::initialize_encode(raw_impl_type) };
        if raw_ptr.is_null() {
            return Err(Error::InstanceInitFailure);
        }

        // Get available input/output formats.
        // Safe because libvda's API is called properly.
        let vea_caps_ptr = unsafe { bindings::get_vea_capabilities(raw_ptr) };
        if vea_caps_ptr.is_null() {
            return Err(Error::GetCapabilitiesFailure);
        }
        // Safe because `vea_cap_ptr` is not NULL and provided by get_vea_capabilities().
        let bindings::vea_capabilities_t {
            num_input_formats,
            input_formats,
            num_output_formats,
            output_formats,
        } = unsafe { *vea_caps_ptr };

        if num_input_formats == 0 || input_formats.is_null() {
            return Err(Error::InvalidCapabilities(
                "invalid input formats".to_string(),
            ));
        }
        if num_output_formats == 0 || output_formats.is_null() {
            return Err(Error::InvalidCapabilities(
                "invalid output formats".to_string(),
            ));
        }

        // Safe because `input_formats` is valid for |`num_input_formats`| elements.
        let input_formats =
            unsafe { PixelFormat::from_raw_parts(input_formats, num_input_formats)? };

        // Safe because `output_formats` is valid for |`num_output_formats`| elements.
        let output_formats =
            unsafe { OutputProfile::from_raw_parts(output_formats, num_output_formats)? };

        Ok(Self {
            raw_ptr,
            caps: EncodeCapabilities {
                input_formats,
                output_formats,
            },
        })
    }

    /// Gets encoder capabilities.
    pub fn get_capabilities(&self) -> &EncodeCapabilities {
        &self.caps
    }

    /// Opens a new `Session` for a given `Config`.
    pub fn open_session<'a>(&'a self, config: Config) -> Result<Session<'a>> {
        // Safe because `self.raw_ptr` is a non-NULL pointer obtained from
        // `decode_bindings::initialize` in `VdaInstance::new`.
        unsafe { Session::new(self.raw_ptr, config).ok_or(Error::EncodeSessionInitFailure(config)) }
    }
}

impl Drop for VeaInstance {
    fn drop(&mut self) {
        // Safe because libvda's API is called properly.
        unsafe { bindings::deinitialize_encode(self.raw_ptr) }
    }
}
