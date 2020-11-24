// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the logic used to setup sandboxes for TEE applications.

use std::fmt::{self, Display};
use std::os::unix::io::RawFd;
use std::path::Path;

use libc::pid_t;
use minijail::{self, Minijail};

#[derive(Debug)]
pub enum Error {
    /// Failed to initialize the jail.
    Jail(minijail::Error),
    /// An error occurred when spawning the child process.
    ForkingJail(minijail::Error),
    /// Failed to set the pivot root path.
    PivotRoot(minijail::Error),
    /// Failed to parse the seccomp policy.
    SeccompPolicy(minijail::Error),
    /// Failed to set the maximum number of open files.
    SettingMaxOpenFiles(minijail::Error),
    /// An error returned while waiting for a child process.
    Wait(minijail::Error),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            Jail(e) => write!(f, "failed to setup jail: {}", e),
            ForkingJail(e) => write!(f, "failed to fork jail process: {}", e),
            PivotRoot(e) => write!(f, "failed to pivot root: {}", e),
            SeccompPolicy(e) => write!(f, "failed to parse seccomp policy: {}", e),
            SettingMaxOpenFiles(e) => write!(f, "error setting max open files: {}", e),
            Wait(e) => write!(f, "failed waiting on jailed process to complete: {}", e),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

const PIVOT_ROOT: &str = "/mnt/empty";

/// An abstraction for the TEE application sandbox.
pub struct Sandbox(minijail::Minijail);

impl Sandbox {
    /// Setup default sandbox / namespaces
    pub fn new(seccomp_bpf_file: Option<&Path>) -> Result<Self> {
        // All child jails run in a new user namespace without any users mapped,
        // they run as nobody unless otherwise configured.
        let mut j = Minijail::new().map_err(Error::Jail)?;

        j.namespace_pids();

        // TODO() determine why uid sandboxing doesn't work.
        //j.namespace_user();
        //j.namespace_user_disable_setgroups();

        j.change_user("nobody").unwrap();
        j.change_group("nobody").unwrap();

        j.use_caps(0);
        j.namespace_vfs();
        j.namespace_net();
        j.no_new_privs();

        if let Some(path) = seccomp_bpf_file {
            j.parse_seccomp_program(&path)
                .map_err(Error::SeccompPolicy)?;
            j.use_seccomp_filter();
        }

        j.enter_pivot_root(Path::new(PIVOT_ROOT))
            .map_err(Error::PivotRoot)?;

        let limit = 1024u64;
        j.set_rlimit(libc::RLIMIT_NOFILE as i32, limit, limit)
            .map_err(Error::SettingMaxOpenFiles)?;

        Ok(Sandbox(j))
    }

    /// A version of the sandbox for use with tests because it doesn't require
    /// elevated privilege.
    pub fn new_test() -> Result<Self> {
        let mut j = Minijail::new().map_err(Error::Jail)?;

        j.namespace_user();
        j.namespace_user_disable_setgroups();
        j.no_new_privs();

        Ok(Sandbox(j))
    }

    /// Execute `cmd` with the specified arguments `args`. The specified file
    /// descriptors are connected to stdio for the child process.
    pub fn run(
        &mut self,
        cmd: &Path,
        args: &[&str],
        stdin: RawFd,
        stdout: RawFd,
        stderr: RawFd,
    ) -> Result<pid_t> {
        // Execute child process with stdin and stdout hooked up to communication and stderr to logging.
        let keep_fds: [(RawFd, RawFd); 3] = [(stdin, 0), (stdout, 1), (stderr, 2)];

        let pid = match self
            .0
            .run_remap(cmd, &keep_fds, args)
            .map_err(Error::ForkingJail)?
        {
            0 => {
                unsafe { libc::exit(0) };
            }
            p => p,
        };

        Ok(pid)
    }

    /// Wait until the child process completes. Non-zero return codes are
    /// returned as an error.
    pub fn wait_for_completion(&mut self) -> Result<()> {
        self.0.wait().map_err(Error::Wait)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::io::{Read, Write};
    use std::os::unix::io::AsRawFd;

    use sys_util::pipe;

    fn do_test(mut s: Sandbox) {
        const STDOUT_TEST: &str = "stdout test";
        const STDERR_TEST: &str = "stderr test";

        let (r_stdin, mut w_stdin) = pipe(true).unwrap();
        let (mut r_stdout, w_stdout) = pipe(true).unwrap();
        let (mut r_stderr, w_stderr) = pipe(true).unwrap();

        s.run(
            Path::new("/bin/sh"),
            &["/bin/sh"],
            r_stdin.as_raw_fd(),
            w_stdout.as_raw_fd(),
            w_stderr.as_raw_fd(),
        )
        .unwrap();
        std::mem::drop(r_stdin);
        std::mem::drop(w_stdout);
        std::mem::drop(w_stderr);

        write!(
            &mut w_stdin,
            "echo -n '{}'; echo -n '{}' 1>&2; exit;",
            STDOUT_TEST, STDERR_TEST
        )
        .unwrap();
        w_stdin.flush().unwrap();
        std::mem::drop(w_stdin);

        let mut stdout_result = String::new();
        r_stdout.read_to_string(&mut stdout_result).unwrap();

        let mut stderr_result = String::new();
        r_stderr.read_to_string(&mut stderr_result).unwrap();

        let result = s.wait_for_completion();

        if result.is_err() {
            eprintln!("Got error code: {:?}", result)
        }

        assert_eq!(
            (stdout_result, stderr_result),
            (STDOUT_TEST.to_string(), STDERR_TEST.to_string())
        );

        result.unwrap();
    }

    #[test]
    #[ignore] // privileged operation.
    fn sandbox() {
        let s = Sandbox::new(None).unwrap();
        do_test(s);
    }

    #[test]
    fn sandbox_unpriviledged() {
        let s = Sandbox::new_test().unwrap();
        do_test(s);
    }
}
