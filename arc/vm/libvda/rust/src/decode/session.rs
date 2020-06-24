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
use super::vda_instance::VdaInstance;
use crate::error::*;
use crate::format::{BufferFd, FramePlane, PixelFormat, Profile};

/// Represents a decode session.
pub struct Session<'a> {
    // Pipe file to be notified decode session events.
    pipe: File,
    vda_ptr: *mut c_void,
    raw_ptr: *mut bindings::vda_session_info_t,
    // `phantom` gurantees that `Session` won't outlive the lifetime of `VdaInstance` that owns
    // `vda_ptr`.
    phantom: PhantomData<&'a VdaInstance>,
}

impl<'a> Session<'a> {
    /// Creates a new `Session`.
    ///
    /// This function is safe if `vda_ptr` is a non-NULL pointer obtained from
    /// `bindings::initialize`.
    pub(crate) unsafe fn new(vda_ptr: *mut c_void, profile: Profile) -> Option<Self> {
        // `init_decode_session` is safe if `vda_ptr` is a non-NULL pointer from
        // `bindings::initialize`.
        let raw_ptr: *mut bindings::vda_session_info_t =
            bindings::init_decode_session(vda_ptr, profile.to_raw_profile());

        if raw_ptr.is_null() {
            return None;
        }

        // Dereferencing `raw_ptr` is safe because it is a valid pointer to a FD provided by libvda.
        // We need to dup() the `event_pipe_fd` because File object close() the FD while libvda also
        // close() it when `close_decode_session` is called.
        let pipe = File::from_raw_fd(libc::dup((*raw_ptr).event_pipe_fd));

        Some(Session {
            pipe,
            vda_ptr,
            raw_ptr,
            phantom: PhantomData,
        })
    }

    /// Gets a reference of pipe that notifies events from VDA session.
    pub fn pipe(&self) -> &File {
        &self.pipe
    }

    /// Reads an `Event` object from a pipe provided a decode session.
    pub fn read_event(&mut self) -> Result<Event> {
        const BUF_SIZE: usize = mem::size_of::<bindings::vda_event_t>();
        let mut buf = [0u8; BUF_SIZE];

        self.pipe
            .read_exact(&mut buf)
            .map_err(Error::ReadEventFailure)?;

        // Safe because libvda must have written vda_event_t to the pipe.
        let vda_event = unsafe { mem::transmute::<[u8; BUF_SIZE], bindings::vda_event_t>(buf) };

        // Safe because `vda_event` is a value read from `self.pipe`.
        unsafe { Event::new(vda_event) }
    }

    /// Sends a decode request for a bitstream buffer given as `fd`.
    ///
    /// `fd` will be closed by Chrome after decoding has occurred.
    pub fn decode(
        &self,
        bitstream_id: i32,
        fd: BufferFd,
        offset: u32,
        bytes_used: u32,
    ) -> Result<()> {
        // Safe because `raw_ptr` is valid and a libvda's API is called properly.
        let r = unsafe {
            bindings::vda_decode((*self.raw_ptr).ctx, bitstream_id, fd, offset, bytes_used)
        };
        Response::new(r).into()
    }

    /// Sets the number of expected output buffers.
    ///
    /// This function must be called after `Event::ProvidePictureBuffers` are notified.
    /// After calling this function, `user_output_buffer` must be called `num_output_buffers` times.
    pub fn set_output_buffer_count(&self, num_output_buffers: usize) -> Result<()> {
        // Safe because `raw_ptr` is valid and a libvda's API is called properly.
        let r = unsafe {
            bindings::vda_set_output_buffer_count((*self.raw_ptr).ctx, num_output_buffers)
        };
        Response::new(r).into()
    }

    /// Provides an output buffer that will be filled with decoded frames.
    ///
    /// Users calls this function after `set_output_buffer_count`. Then, libvda
    /// will fill next frames in the buffer and noitify `Event::PictureReady`.
    ///
    /// This function is also used to notify that they consumed decoded frames
    /// in the output buffer.
    pub fn use_output_buffer(
        &self,
        picture_buffer_id: i32,
        format: PixelFormat,
        output_buffer: BufferFd,
        planes: &[FramePlane],
    ) -> Result<()> {
        let mut planes: Vec<_> = planes.iter().map(FramePlane::to_raw_frame_plane).collect();

        // Safe because `raw_ptr` is valid and a libvda's API is called properly.
        let r = unsafe {
            bindings::vda_use_output_buffer(
                (*self.raw_ptr).ctx,
                picture_buffer_id,
                format.to_raw_pixel_format(),
                output_buffer,
                planes.len(),
                planes.as_mut_ptr(),
            )
        };
        Response::new(r).into()
    }

    /// Returns an output buffer for reuse.
    ///
    /// `picture_buffer_id` must be a value for which `use_output_buffer` has been called already.
    pub fn reuse_output_buffer(&self, picture_buffer_id: i32) -> Result<()> {
        // Safe because `raw_ptr` is valid and a libvda's API is called properly.
        let r =
            unsafe { bindings::vda_reuse_output_buffer((*self.raw_ptr).ctx, picture_buffer_id) };
        Response::new(r).into()
    }

    /// Flushes the decode session.
    ///
    /// When this operation has completed, `Event::FlushResponse` will be notified.
    pub fn flush(&self) -> Result<()> {
        // Safe because `raw_ptr` is valid and a libvda's API is called properly.
        let r = unsafe { bindings::vda_flush((*self.raw_ptr).ctx) };
        Response::new(r).into()
    }

    /// Resets the decode session.
    ///
    /// When this operation has completed, Event::ResetResponse will be notified.
    pub fn reset(&self) -> Result<()> {
        // Safe because `raw_ptr` is valid and a libvda's API is called properly.
        let r = unsafe { bindings::vda_reset((*self.raw_ptr).ctx) };
        Response::new(r).into()
    }
}

impl<'a> Drop for Session<'a> {
    fn drop(&mut self) {
        // Safe because `vda_ptr` and `raw_ptr` are unchanged from the time `new` was called.
        // Also, `vda_ptr` is valid because `phantom` guranteed that `VdaInstance` owning `vda_ptr`
        // is not dropped yet.
        unsafe {
            bindings::close_decode_session(self.vda_ptr, self.raw_ptr);
        }
    }
}
