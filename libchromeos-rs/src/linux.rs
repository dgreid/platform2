// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Provides safe implementations of common low level functions that assume a Linux environment.
use std::io::{Error, ErrorKind, Result};
use std::os::raw::c_int;
use std::process::Child;
use std::ptr::null_mut;
use std::thread::sleep;
use std::time::{Duration, SystemTime};

use libc::{syscall, waitpid, SYS_gettid, ECHILD, WNOHANG};

pub type Pid = libc::pid_t;

const POLL_RATE: Duration = Duration::from_millis(50);

pub enum Signal {
    Abort = libc::SIGABRT as isize,
    Child = libc::SIGCHLD as isize,
    Continue = libc::SIGCONT as isize,
    FloatingPointException = libc::SIGFPE as isize,
    HangUp = libc::SIGHUP as isize,
    Interrupt = libc::SIGINT as isize,
    Kill = libc::SIGKILL as isize,
    Quit = libc::SIGQUIT as isize,
    Stop = libc::SIGSTOP as isize,
    Sys = libc::SIGSYS as isize,
    Terminate = libc::SIGTERM as isize,
}

pub fn getpid() -> Pid {
    // Calling getpid() is always safe.
    unsafe { libc::getpid() }
}

pub fn gettid() -> Pid {
    // Calling the gettid() sycall is always safe.
    unsafe { syscall(SYS_gettid) as Pid }
}

pub fn getsid(pid: Option<Pid>) -> Result<Pid> {
    // Calling the getsid() sycall is always safe.
    let ret = unsafe { libc::getsid(pid.unwrap_or(0)) } as Pid;
    if ret < 0 {
        Err(Error::last_os_error())
    } else {
        Ok(ret)
    }
}

/// # Safety
/// Safe as long as the parent process doesn't assume this process is in its process group.
pub unsafe fn setsid() -> Result<Pid> {
    let ret = libc::setsid() as Pid;
    if ret < 0 {
        Err(Error::last_os_error())
    } else {
        Ok(ret)
    }
}

/// # Safety
/// This is marked unsafe because it allows signals to be sent to arbitrary PIDs. Sending some
/// signals may lead to undefined behavior. Also, the return codes of the child processes need to
/// reaped to avoid leaking zombie processes.
pub unsafe fn kill(pid: Pid, signal: Signal) -> Result<()> {
    if libc::kill(pid, signal as c_int) != 0 {
        Err(Error::last_os_error())
    } else {
        Ok(())
    }
}

fn allow_process_doesnt_exist(err: Error) -> Result<()> {
    if let Some(libc::ESRCH) = err.raw_os_error() {
        Ok(())
    } else {
        Err(err)
    }
}

/// Terminates a child process (and its children if it is a group leader) if it exists, and try to
/// reap any zombie processes, but if the timeout is reached send SIGKILL.
pub fn kill_tree(child: &mut Child, timeout: Duration) -> Result<()> {
    let target = {
        let pid = child.id() as Pid;
        if getsid(Some(pid))? == pid {
            -pid
        } else {
            pid
        }
    };

    // Safe because target is a child process (or group) and behavior of SIGTERM is defined.
    if let Err(err) = unsafe { kill(target, Signal::Terminate) } {
        return allow_process_doesnt_exist(err);
    };

    let start = SystemTime::now();
    const ZERO_DURATION: Duration = Duration::from_secs(0);
    let check_timeout = || -> Result<()> {
        let remaining = SystemTime::now()
            .duration_since(start)
            .unwrap_or(ZERO_DURATION);
        if remaining > timeout {
            // Safe because target is a child process (or group) and behavior of SIGKILL is defined.
            if let Err(err) = unsafe { kill(target, Signal::Kill) } {
                return allow_process_doesnt_exist(err);
            };
            Err(Error::from(ErrorKind::TimedOut))
        } else {
            sleep(POLL_RATE.min(remaining));
            Ok(())
        }
    };

    while child.try_wait()?.is_none() {
        check_timeout()?;
    }

    loop {
        // Safe because target is a child process (or group) and WNOHANG is used.
        let ret = unsafe { waitpid(target, null_mut(), WNOHANG) };
        match ret {
            -1 => {
                let err = Error::last_os_error();
                if let Some(ECHILD) = err.raw_os_error() {
                    break;
                }
                return Err(err);
            }
            0 => {
                check_timeout()?;
            }
            _ => {}
        };
    }

    Ok(())
}
