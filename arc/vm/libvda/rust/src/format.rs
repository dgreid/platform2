// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Data structures representing coded/raw formats.

use enumn::N;
use std::os::unix::io::RawFd;

use super::bindings;
use super::error::*;

/// Represents a FD for bitstream/frame buffer.
/// Files described by BufferFd must be accessed from outside of this crate.
pub type BufferFd = RawFd;

/// Represents a video frame plane.
pub struct FramePlane {
    pub offset: i32,
    pub stride: i32,
}

/// Represents a video codec.
#[derive(Debug, Clone, Copy, N)]
#[repr(i32)]
pub enum Profile {
    VP8 = bindings::video_codec_profile_VP8PROFILE_MIN,
    VP9Profile0 = bindings::video_codec_profile_VP9PROFILE_PROFILE0,
    H264 = bindings::video_codec_profile_H264PROFILE_MAIN,
}

impl Profile {
    pub(crate) fn to_raw_profile(self) -> bindings::video_codec_profile_t {
        match self {
            Profile::VP8 => bindings::video_codec_profile_VP8PROFILE_MIN,
            Profile::VP9Profile0 => bindings::video_codec_profile_VP9PROFILE_PROFILE0,
            Profile::H264 => bindings::video_codec_profile_H264PROFILE_MAIN,
        }
    }
}

/// Represents a raw pixel format.
#[derive(Debug, N)]
#[repr(u32)]
pub enum PixelFormat {
    YV12 = bindings::video_pixel_format_YV12,
    NV12 = bindings::video_pixel_format_NV12,
}

impl PixelFormat {
    pub(crate) fn new(f: bindings::video_pixel_format_t) -> Result<PixelFormat> {
        PixelFormat::n(f).ok_or(Error::UnknownPixelFormat(f))
    }

    pub(crate) fn to_raw_pixel_format(&self) -> bindings::video_pixel_format_t {
        match *self {
            PixelFormat::YV12 => bindings::video_pixel_format_YV12,
            PixelFormat::NV12 => bindings::video_pixel_format_NV12,
        }
    }
}
