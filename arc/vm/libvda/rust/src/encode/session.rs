// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::File;
use std::io::Read;
use std::marker::PhantomData;
use std::mem;
use std::os::raw::c_void;
use std::os::unix::io::FromRawFd;

use super::bindings;
use super::event::*;
use super::vea_instance::{Config, VeaInstance};
use crate::error::*;
use crate::format::{BufferFd, FramePlane};

pub type VeaInputBufferId = bindings::vea_input_buffer_id_t;
pub type VeaOutputBufferId = bindings::vea_output_buffer_id_t;

/// Represents an encode session.
pub struct Session<'a> {
    // Pipe file to be notified encode session events.
    pipe: File,
    vea_ptr: *mut c_void,
    raw_ptr: *mut bindings::vea_session_info_t,
    // `phantom` guarantees that `Session` will not outlive the lifetime of
    // `VeaInstance` that owns `vea_ptr`.
    phantom: PhantomData<&'a VeaInstance>,
}

fn convert_error_code(code: i32) -> Result<()> {
    if code == 0 {
        Ok(())
    } else {
        Err(Error::EncodeSessionFailure(code))
    }
}

impl<'a> Session<'a> {
    /// Creates a new `Session`.
    ///
    /// This function is safe if `vea_ptr` is a non-NULL pointer obtained from
    /// `bindings::initialize_encode`.
    pub(crate) unsafe fn new(vea_ptr: *mut c_void, config: Config) -> Option<Self> {
        // `init_encode_session` is safe if `vea_ptr` is a non-NULL pointer from
        // `bindings::initialize`.
        let raw_ptr: *mut bindings::vea_session_info_t =
            bindings::init_encode_session(vea_ptr, &mut config.to_raw_config());

        if raw_ptr.is_null() {
            return None;
        }

        // Dereferencing `raw_ptr` is safe because it is a valid pointer to a FD provided by libvda.
        // We need to dup() the `event_pipe_fd` because File object close() the FD while libvda also
        // close() it when `close_encode_session` is called.
        let pipe = File::from_raw_fd(libc::dup((*raw_ptr).event_pipe_fd));

        Some(Session {
            pipe,
            vea_ptr,
            raw_ptr,
            phantom: PhantomData,
        })
    }

    /// Returns a reference for the pipe that notifies of encode events.
    pub fn pipe(&self) -> &File {
        &self.pipe
    }

    /// Reads an `Event` object from a pipe provided by an encode session.
    pub fn read_event(&mut self) -> Result<Event> {
        const BUF_SIZE: usize = mem::size_of::<bindings::vea_event_t>();
        let mut buf = [0u8; BUF_SIZE];

        self.pipe
            .read_exact(&mut buf)
            .map_err(Error::ReadEventFailure)?;

        // Safe because libvda must have written vea_event_t to the pipe.
        let vea_event = unsafe { mem::transmute::<[u8; BUF_SIZE], bindings::vea_event_t>(buf) };

        // Safe because `vea_event` is a value read from `self.pipe`.
        unsafe { Event::new(vea_event) }
    }

    /// Sends an encode request for an input buffer given as `fd` with planes described
    /// by `planes. The timestamp of the frame to encode is typically provided in
    /// milliseconds by `timestamp`. `force_keyframe` indicates to the encoder that
    /// the frame should be encoded as a keyframe.
    ///
    /// When the input buffer has been filled, an `EncoderEvent::ProcessedInputBuffer`
    /// event can be read from the event pipe.
    ///
    /// The caller is responsible for passing in a unique value for `input_buffer_id`
    /// which can be referenced when the event is received.
    ///
    /// `fd` will be closed after encoding has occurred.
    pub fn encode(
        &self,
        input_buffer_id: VeaInputBufferId,
        fd: BufferFd,
        planes: &[FramePlane],
        timestamp: i64,
        force_keyframe: bool,
    ) -> Result<()> {
        let mut planes: Vec<_> = planes.iter().map(FramePlane::to_raw_frame_plane).collect();

        // Safe because `raw_ptr` is valid and libvda's encode API is called properly.
        let r = unsafe {
            bindings::vea_encode(
                (*self.raw_ptr).ctx,
                input_buffer_id,
                fd,
                planes.len(),
                planes.as_mut_ptr(),
                timestamp,
                if force_keyframe { 1 } else { 0 },
            )
        };
        convert_error_code(r)
    }

    /// Provides a buffer for storing encoded output.
    ///
    /// When the output buffer has been filled, an `EncoderEvent::ProcessedOutputBuffer`
    /// event can be read from the event pipe.
    ///
    /// The caller is responsible for passing in a unique value for `output_buffer_id`
    /// which can be referenced when the event is received.
    ///
    /// This function takes ownership of `fd`.
    pub fn use_output_buffer(
        &self,
        output_buffer_id: VeaOutputBufferId,
        fd: BufferFd,
        offset: u32,
        size: u32,
    ) -> Result<()> {
        // Safe because `raw_ptr` is valid and libvda's encode API is called properly.
        let r = unsafe {
            bindings::vea_use_output_buffer((*self.raw_ptr).ctx, output_buffer_id, fd, offset, size)
        };
        convert_error_code(r)
    }

    /// Requests encoding parameter changes.
    ///
    /// The request is not guaranteed to be honored by libvda and could be ignored
    /// by the backing encoder implementation.
    pub fn request_encoding_params_change(&self, bitrate: u32, framerate: u32) -> Result<()> {
        // Safe because `raw_ptr` is valid and libvda's encode API is called properly.
        let r = unsafe {
            bindings::vea_request_encoding_params_change((*self.raw_ptr).ctx, bitrate, framerate)
        };
        convert_error_code(r)
    }

    /// Flushes the encode session.
    ///
    /// When this operation has completed, Event::FlushResponse can be read from
    /// the event pipe.
    pub fn flush(&self) -> Result<()> {
        // Safe because `raw_ptr` is valid and libvda's encode API is called properly.
        let r = unsafe { bindings::vea_flush((*self.raw_ptr).ctx) };
        convert_error_code(r)
    }
}

impl<'a> Drop for Session<'a> {
    fn drop(&mut self) {
        // Safe because `vea_ptr` and `raw_ptr` are unchanged from the time `new` was called.
        // Also, `vea_ptr` is valid because `phantom` guarantees that `VeaInstance` owning `vea_ptr`
        // has not dropped yet.
        unsafe {
            bindings::close_encode_session(self.vea_ptr, self.raw_ptr);
        }
    }
}
