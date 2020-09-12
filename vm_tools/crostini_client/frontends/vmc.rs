// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error::Error;
use std::path::Path;
use std::{fmt, fs};

use std::io::{stdout, Write};

use getopts::Options;

use super::Frontend;
use crate::disk::DiskOpType;
use crate::methods::{ContainerSource, Methods, VmFeatures};
use crate::proto::system_api::cicerone_service::StartLxdContainerRequest_PrivilegeLevel;
use crate::EnvMap;

enum VmcError {
    Command(&'static str, u32, Box<dyn Error>),
    BadProblemReportArguments(getopts::Fail),
    ExpectedCrosUserIdHash,
    ExpectedUIntSize,
    ExpectedName,
    ExpectedNoArgs,
    ExpectedPath,
    ExpectedSize,
    ExpectedU8Bus,
    ExpectedU8Device,
    ExpectedU8Port,
    ExpectedUUID,
    ExpectedVmAndContainer,
    ExpectedVmAndFileName,
    ExpectedVmAndMaybeFileName,
    ExpectedVmAndPath,
    ExpectedVmAndSize,
    ExpectedVmBusDevice,
    ExpectedVmPort,
    InvalidEmail,
    InvalidPath(std::ffi::OsString),
    MissingActiveSession,
    ExpectedPrivilegedFlagValue,
    UnknownSubcommand(String),
}

use self::VmcError::*;

// Optional flag used with "vmc container" command. Use this with the getopts crate API.
static PRIVILEGED_FLAG: &str = "privileged";

// Option names for pvm.send-porblem-report command. Use this with the getopts crate API.
static EMAIL_OPTION: &str = "email";
static VM_NAME_OPTION: &str = "vm-name";

// Remove useless expression items that the `try_command!()` macro captures and stringifies when
// generating a `VmcError::Command`.
fn trim_routine(s: &str) -> String {
    s.trim_start_matches("self.methods.")
        .replace(char::is_whitespace, "")
}

fn get_user_hash(environ: &EnvMap) -> Result<String, VmcError> {
    if let Some(hash) = environ.get("CROS_USER_ID_HASH").map(|s| String::from(*s)) {
        return Ok(hash);
    }
    // The current user is the one with a non-empty cryptohome
    let entries = fs::read_dir(Path::new("/home/user")).map_err(|_| ExpectedCrosUserIdHash)?;
    let mut dirs = entries.filter_map(|f| {
        let dir = f.ok()?;
        if dir.path().read_dir().ok()?.count() > 0 {
            dir.file_name().into_string().ok()
        } else {
            None
        }
    });
    let first = dirs.next().ok_or(ExpectedCrosUserIdHash)?;
    // If there's another user cryptohome, it's ambiguous which to use so require it to be
    // specified manually.
    match dirs.next() {
        Some(_) => Err(ExpectedCrosUserIdHash),
        None => Ok(first),
    }
}

impl fmt::Display for VmcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BadProblemReportArguments(e) => write!(f, "failed to parse arguments: {:?}", e),
            Command(routine, line_num, e) => write!(
                f,
                "routine at {}:{} `{}` failed: {}",
                file!(),
                line_num,
                trim_routine(&routine),
                e
            ),
            ExpectedCrosUserIdHash => write!(f, "expected CROS_USER_ID_HASH environment variable"),
            ExpectedUIntSize => write!(
                f,
                "expected unsigned integer for the disk size. (e.g. 1000000000, 256M, 1G)"
            ),
            ExpectedName => write!(f, "expected <name>"),
            ExpectedPath => write!(f, "expected <path>"),
            ExpectedSize => write!(f, "expected <size>"),
            ExpectedVmAndContainer => write!(
                f,
                "expected <vm name> <container name> [ <image server> <image alias> ]"
            ),
            ExpectedVmAndFileName => {
                write!(f, "expected <vm name> <file name> [removable storage name]")
            }
            ExpectedVmAndMaybeFileName => write!(
                f,
                "expected <vm name> [<file name> [removable storage name]]"
            ),
            ExpectedVmAndPath => write!(f, "expected <vm name> <path>"),
            ExpectedVmAndSize => write!(f, "expected <vm name> <size>"),
            ExpectedVmBusDevice => write!(f, "expected <vm name> <bus>:<device>"),
            ExpectedNoArgs => write!(f, "expected no arguments"),
            ExpectedU8Bus => write!(f, "expected <bus> to fit into an 8-bit integer"),
            ExpectedU8Device => write!(f, "expected <device> to fit into an 8-bit integer"),
            ExpectedU8Port => write!(f, "expected <port> to fit into an 8-bit integer"),
            ExpectedUUID => write!(f, "expected <command UUID>"),
            ExpectedVmPort => write!(f, "expected <vm name> <port>"),
            InvalidEmail => write!(f, "the active session has an invalid email address"),
            InvalidPath(path) => write!(f, "invalid path: {:?}", path),
            MissingActiveSession => write!(
                f,
                "missing active session corresponding to $CROS_USER_ID_HASH"
            ),
            ExpectedPrivilegedFlagValue => {
                write!(f, "Expected <true/false> after the privileged flag")
            }
            UnknownSubcommand(s) => write!(f, "no such subcommand: `{}`", s),
        }
    }
}

impl fmt::Debug for VmcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}

impl Error for VmcError {}

type VmcResult = Result<(), Box<dyn Error>>;

macro_rules! try_command {
    ($x:expr) => {
        if cfg!(test) {
            // Ignore the command's result for testing.
            $x.map_err(|e| Command(stringify!($x), line!(), e))
                .unwrap_or_default()
        } else {
            $x.map_err(|e| Command(stringify!($x), line!(), e))?
        }
    };
}

struct Command<'a, 'b, 'c> {
    methods: &'a mut Methods,
    args: &'b [&'b str],
    environ: &'c EnvMap<'c>,
}

impl<'a, 'b, 'c> Command<'a, 'b, 'c> {
    // Metrics are on a best-effort basis. We print errors related to sending metrics, but stop
    // propagation of the error, which is why this function never returns an error.
    fn metrics_send_sample(&mut self, name: &str) {
        if let Err(e) = self.methods.metrics_send_sample(name) {
            eprintln!(
                "warning: failed attempt to send metrics sample `{}`: {}",
                name, e
            );
        }
    }

    fn start(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optflag("", "enable-gpu", "when starting the vm, enable gpu support");
        opts.optflag(
            "",
            "software-tpm",
            "provide software-based virtual Trusted Platform Module",
        );
        opts.optflag(
            "",
            "enable-audio-capture",
            "when starting the vm, enable audio capture support",
        );
        opts.optflag(
            "",
            "untrusted",
            "treat this VM as untrusted, only valid in developer mode",
        );
        let matches = opts.parse(self.args)?;

        if matches.free.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = &matches.free[0];
        let user_id_hash = get_user_hash(self.environ)?;
        let features = VmFeatures {
            gpu: matches.opt_present("enable-gpu"),
            software_tpm: matches.opt_present("software-tpm"),
            audio_capture: matches.opt_present("enable-audio-capture"),
            run_as_untrusted: matches.opt_present("untrusted"),
        };

        self.metrics_send_sample("Vm.VmcStart");
        try_command!(self.methods.vm_start(vm_name, &user_id_hash, features));
        self.metrics_send_sample("Vm.VmcStartSuccess");
        try_command!(self.methods.vsh_exec(vm_name, &user_id_hash));

        Ok(())
    }

    fn stop(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];
        let user_id_hash = get_user_hash(self.environ)?;

        try_command!(self.methods.vm_stop(vm_name, &user_id_hash));

        Ok(())
    }

    fn create(&mut self) -> VmcResult {
        let plugin_vm = self.args.len() > 0 && self.args[0] == "-p";
        if plugin_vm {
            // Discard the first argument (-p).
            self.args = &self.args[1..];
        }

        let mut s = self.args.splitn(2, |arg| *arg == "--");
        let args = s.next().expect("failed to split argument list");
        let params = s.next().unwrap_or(&[]);

        let (vm_name, file_name, removable_media) = match args.len() {
            1 => (args[0], None, None),
            2 => (args[0], Some(args[1]), None),
            3 => (args[0], Some(args[1]), Some(args[2])),
            _ => return Err(ExpectedVmAndMaybeFileName.into()),
        };

        let user_id_hash = get_user_hash(self.environ)?;

        if let Some(uuid) = try_command!(self.methods.vm_create(
            vm_name,
            &user_id_hash,
            plugin_vm,
            file_name,
            removable_media,
            params,
        )) {
            println!("VM creation in progress: {}", uuid);
            self.wait_disk_op_completion(&uuid, &user_id_hash, DiskOpType::Create)?;
        }
        Ok(())
    }

    fn adjust(&mut self) -> VmcResult {
        if self.args.len() < 2 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];
        let user_id_hash = get_user_hash(self.environ)?;
        let operation = &self.args[1];

        try_command!(self
            .methods
            .vm_adjust(vm_name, &user_id_hash, operation, &self.args[2..]));

        Ok(())
    }

    fn destroy(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];
        let user_id_hash = get_user_hash(self.environ)?;

        match self.methods.disk_destroy(vm_name, &user_id_hash) {
            Ok(()) => Ok(()),
            Err(e) => {
                self.metrics_send_sample("Vm.DiskEraseFailed");
                Err(Command("disk_destroy", line!(), e).into())
            }
        }
    }

    fn wait_disk_op_completion(
        &mut self,
        uuid: &str,
        user_id_hash: &str,
        op_type: DiskOpType,
    ) -> VmcResult {
        loop {
            let (done, progress) =
                try_command!(self.methods.wait_disk_op(&uuid, &user_id_hash, op_type));
            if done {
                println!("\rOperation completed successfully");
                return Ok(());
            }

            print!("\rOperation in progress: {}% done", progress);
            stdout().flush()?;
        }
    }

    fn resize(&mut self) -> VmcResult {
        if self.args.len() != 2 {
            return Err(ExpectedVmAndSize.into());
        }

        let vm_name = self.args[0];
        let size: u64 = self.args[1].parse().or(Err(ExpectedSize))?;

        let user_id_hash = get_user_hash(self.environ)?;

        match try_command!(self.methods.disk_resize(vm_name, &user_id_hash, size)) {
            Some(uuid) => {
                println!("Resize in progress: {}", uuid);
                self.wait_disk_op_completion(&uuid, &user_id_hash, DiskOpType::Resize)?;
            }
            None => {
                println!("Operation completed successfully");
            }
        }
        Ok(())
    }

    fn export(&mut self) -> VmcResult {
        let generate_digest = self.args.len() > 0 && self.args[0] == "-d";
        if generate_digest {
            // Discard the first argument (-d).
            self.args = &self.args[1..];
        }

        let (vm_name, file_name, removable_media) = match self.args.len() {
            2 => (self.args[0], self.args[1], None),
            3 => (self.args[0], self.args[1], Some(self.args[2])),
            _ => return Err(ExpectedVmAndFileName.into()),
        };

        let digest_name = file_name.to_owned() + ".sha256.txt";
        let digest_option = if generate_digest {
            Some(digest_name.as_str())
        } else {
            None
        };

        let user_id_hash = get_user_hash(self.environ)?;

        if let Some(uuid) = try_command!(self.methods.vm_export(
            vm_name,
            &user_id_hash,
            file_name,
            digest_option,
            removable_media
        )) {
            println!("Export in progress: {}", uuid);
            self.wait_disk_op_completion(&uuid, &user_id_hash, DiskOpType::Create)?;
        }
        Ok(())
    }

    fn import(&mut self) -> VmcResult {
        let plugin_vm = self.args.len() > 0 && self.args[0] == "-p";
        if plugin_vm {
            // Discard the first argument (-p).
            self.args = &self.args[1..];
        }
        let (vm_name, file_name, removable_media) = match self.args.len() {
            2 => (self.args[0], self.args[1], None),
            3 => (self.args[0], self.args[1], Some(self.args[2])),
            _ => return Err(ExpectedVmAndFileName.into()),
        };

        let user_id_hash = get_user_hash(self.environ)?;

        if let Some(uuid) = try_command!(self.methods.vm_import(
            vm_name,
            &user_id_hash,
            plugin_vm,
            file_name,
            removable_media
        )) {
            println!("Import in progress: {}", uuid);
            self.wait_disk_op_completion(&uuid, &user_id_hash, DiskOpType::Create)?;
        }
        Ok(())
    }

    fn disk_op_status(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedUUID.into());
        }

        let uuid = self.args[0];
        let user_id_hash = get_user_hash(self.environ)?;

        let (done, progress) =
            try_command!(self
                .methods
                .disk_op_status(uuid, &user_id_hash, DiskOpType::Create));
        if done {
            println!("Operation completed successfully");
        } else {
            println!("Operation in progress: {}% done", progress);
        }
        Ok(())
    }

    fn create_extra_disk(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.reqopt("", "size", "size of extra disk", "SIZE");
        opts.optflag(
            "",
            "removable-media",
            "store the extra disk on a removable media",
        );

        let matches = opts.parse(self.args)?;

        let s = matches.opt_str("size").ok_or_else(|| ExpectedSize)?;
        let size: u64 = match s.chars().last() {
            Some('M') => s[..s.len() - 1].parse::<u64>().map(|x| x * 1024 * 1024),
            Some('G') => s[..s.len() - 1]
                .parse::<u64>()
                .map(|x| x * 1024 * 1024 * 1024),
            _ => s.parse(),
        }
        .map_err(|_| ExpectedUIntSize)?;

        if matches.free.len() != 1 {
            return Err(ExpectedPath.into());
        }

        let mut path = matches.free[0].clone();
        if matches.opt_present("removable-media") {
            path = Path::new("/media/removable")
                .join(path)
                .into_os_string()
                .into_string()
                .map_err(|s| InvalidPath(s))?;
        }

        try_command!(self.methods.extra_disk_create(&path, size));
        println!("A raw disk is created at {}.", path);
        Ok(())
    }

    fn list(&mut self) -> VmcResult {
        if !self.args.is_empty() {
            return Err(ExpectedNoArgs.into());
        }

        let user_id_hash = get_user_hash(self.environ)?;

        let (disk_image_list, total_size) = try_command!(self.methods.disk_list(&user_id_hash));
        for disk in disk_image_list {
            let mut extra_info = String::new();
            if let Some(min_size) = disk.min_size {
                extra_info.push_str(&format!(", min shrinkable size {} bytes", min_size));
            }
            extra_info.push_str(&format!(", {}", disk.image_type));
            if !disk.user_chosen_size {
                extra_info.push_str(", sparse");
            }
            println!("{} ({} bytes{})", disk.name, disk.size, extra_info);
        }
        println!("Total Size (bytes): {}", total_size);
        Ok(())
    }

    fn share(&mut self) -> VmcResult {
        if self.args.len() != 2 {
            return Err(ExpectedVmAndPath.into());
        }

        let user_id_hash = get_user_hash(self.environ)?;

        let vm_name = self.args[0];
        let path = self.args[1];
        let vm_path = try_command!(self.methods.vm_share_path(vm_name, &user_id_hash, path));
        println!("{} is available at path {}", path, vm_path);
        Ok(())
    }

    fn unshare(&mut self) -> VmcResult {
        if self.args.len() != 2 {
            return Err(ExpectedVmAndPath.into());
        }

        let user_id_hash = get_user_hash(self.environ)?;

        let vm_name = self.args[0];
        let path = self.args[1];
        try_command!(self.methods.vm_unshare_path(vm_name, &user_id_hash, path));
        Ok(())
    }

    fn container(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optopt(
            "p",
            PRIVILEGED_FLAG,
            "is the container privileged. only takes effect on boards that support privileged containers",
            "true / false",
        );
        let matches = opts
            .parse(self.args)
            .map_err(|_| ExpectedPrivilegedFlagValue)?;

        // The privileged flag is optional but when its given it must be followed by a valid value.
        let privilege_level = match matches.opt_str(PRIVILEGED_FLAG) {
            Some(s) => match s.as_str() {
                "True" | "true" => StartLxdContainerRequest_PrivilegeLevel::PRIVILEGED,
                "False" | "false" => StartLxdContainerRequest_PrivilegeLevel::UNPRIVILEGED,
                _ => return Err(ExpectedPrivilegedFlagValue.into()),
            },
            None => StartLxdContainerRequest_PrivilegeLevel::UNCHANGED,
        };

        let required_args = &matches.free;
        let (vm_name, container_name, source) = match required_args.len() {
            2 => (
                required_args[0].as_str(),
                required_args[1].as_str(),
                ContainerSource::ImageServer {
                    image_server: "https://storage.googleapis.com/cros-containers/%d".to_string(),
                    image_alias: "debian/buster".to_string(),
                },
            ),
            4 => (
                required_args[0].as_str(),
                required_args[1].as_str(),
                // If this argument looks like an absolute path, treat it and the following
                // parameter as local paths to tarballs.  Otherwise, assume they are an
                // image server URL and image alias.
                if required_args[2].starts_with("/") {
                    ContainerSource::Tarballs {
                        rootfs_path: required_args[2].clone(),
                        metadata_path: required_args[3].clone(),
                    }
                } else {
                    ContainerSource::ImageServer {
                        image_server: required_args[2].clone(),
                        image_alias: required_args[3].clone(),
                    }
                },
            ),
            _ => return Err(ExpectedVmAndContainer.into()),
        };

        let user_id_hash = get_user_hash(self.environ)?;

        let sessions = try_command!(self.methods.sessions_list());
        let email = sessions
            .iter()
            .find(|(_, hash)| hash == &user_id_hash)
            .map(|(email, _)| email)
            .ok_or(MissingActiveSession)?;

        let username = match email.find('@') {
            Some(0) | None => return Err(InvalidEmail.into()),
            Some(end) => &email[..end],
        };

        try_command!(self
            .methods
            .container_create(vm_name, &user_id_hash, container_name, source));
        try_command!(self.methods.container_setup_user(
            vm_name,
            &user_id_hash,
            container_name,
            username
        ));

        // If the container was already running then this will update the privilege level of the
        // container if |privilege_level| is not |UNCHANGED|. This will take into effect on next
        // container boot.
        try_command!(self.methods.container_start(
            vm_name,
            &user_id_hash,
            container_name,
            privilege_level
        ));

        try_command!(self
            .methods
            .vsh_exec_container(vm_name, &user_id_hash, container_name));

        Ok(())
    }

    fn usb_attach(&mut self) -> VmcResult {
        let (vm_name, bus_device) = match self.args.len() {
            2 => (self.args[0], self.args[1]),
            _ => return Err(ExpectedVmBusDevice.into()),
        };

        let mut bus_device_parts = bus_device.splitn(2, ':');
        let (bus, device) = match (bus_device_parts.next(), bus_device_parts.next()) {
            (Some(bus_str), Some(device_str)) => (
                bus_str.parse().or(Err(ExpectedU8Bus))?,
                device_str.parse().or(Err(ExpectedU8Device))?,
            ),
            _ => return Err(ExpectedVmBusDevice.into()),
        };

        let user_id_hash = get_user_hash(self.environ)?;

        let guest_port = try_command!(self.methods.usb_attach(vm_name, &user_id_hash, bus, device));

        println!(
            "usb device at bus={} device={} attached to vm {} at port={}",
            bus, device, vm_name, guest_port
        );

        Ok(())
    }

    fn usb_detach(&mut self) -> VmcResult {
        let (vm_name, port) = match self.args.len() {
            2 => (self.args[0], self.args[1].parse().or(Err(ExpectedU8Port))?),
            _ => return Err(ExpectedVmPort.into()),
        };

        let user_id_hash = get_user_hash(self.environ)?;

        try_command!(self.methods.usb_detach(vm_name, &user_id_hash, port));

        println!("usb device detached from port {}", port);

        Ok(())
    }

    fn usb_list(&mut self) -> VmcResult {
        if self.args.len() != 1 {
            return Err(ExpectedName.into());
        }

        let vm_name = self.args[0];
        let user_id_hash = get_user_hash(self.environ)?;

        let devices = try_command!(self.methods.usb_list(vm_name, &user_id_hash));
        if devices.is_empty() {
            println!("No attached usb devices");
        }
        for (port, vendor_id, product_id, name) in devices {
            println!(
                "Port {:03} ID {:04x}:{:04x} {}",
                port, vendor_id, product_id, name
            );
        }

        Ok(())
    }

    fn pvm_send_problem_report(&mut self) -> VmcResult {
        let mut opts = Options::new();
        opts.optopt(
            "e",
            EMAIL_OPTION,
            "email to associate with the problem report",
            "EMAIL",
        );
        opts.optopt(
            "n",
            VM_NAME_OPTION,
            "name of the VM for which problem report is generated",
            "NAME",
        );
        let matches = opts
            .parse(self.args)
            .map_err(|e| BadProblemReportArguments(e))?;

        let vm_name = matches.opt_str(VM_NAME_OPTION);
        let user_id_hash = get_user_hash(self.environ)?;
        let email = matches.opt_str(EMAIL_OPTION);
        let text = if matches.free.is_empty() {
            None
        } else {
            Some(matches.free.join(" "))
        };

        let report_id =
            try_command!(self
                .methods
                .pvm_send_problem_report(vm_name, &user_id_hash, email, text));

        println!("Problem report has been sent. Report ID: {}", report_id);
        Ok(())
    }
}

const USAGE: &str = r#"
   [ start [--enable-gpu] [--enable-audio-capture] [--untrusted] <name> |
     stop <name> |
     create [-p] <name> [<source media> [<removable storage name>]] [-- additional parameters]
     create-extra-disk --size SIZE [--removable-media] <host disk path> |
     adjust <name> <operation> [additional parameters] |
     destroy <name> |
     disk-op-status <command UUID> |
     export [-d] <vm name> <file name> [<removable storage name>] |
     import [-p] <vm name> <file name> [<removable storage name>] |
     resize <vm name> <size> |
     list |
     share <vm name> <path> |
     unshare <vm name> <path> |
     container <vm name> <container name> [ (<image server> <image alias>) | (<rootfs path> <metadata path>)] [ --privileged <true/false> ]
     usb-attach <vm name> <bus>:<device> |
     usb-detach <vm name> <port> |
     usb-list <vm name> |
     --help | -h ]
"#;

/// A frontend that implements a `vmc` (Virtual Machine Controller) style command line interface.
/// This is the interface accessible from crosh (Ctrl-Alt-T in the browser to access).
pub struct Vmc;

impl Frontend for Vmc {
    fn name(&self) -> &str {
        "vmc"
    }

    fn print_usage(&self, program_name: &str) {
        eprintln!("USAGE: {}{}", program_name, USAGE);
    }

    fn run(&self, methods: &mut Methods, args: &[&str], environ: &EnvMap) -> VmcResult {
        if args.len() < 2 {
            self.print_usage("vmc");
            return Ok(());
        }

        for &arg in args {
            match arg {
                "--" => break,
                "--help" | "-h" => {
                    self.print_usage("vmc");
                    return Ok(());
                }
                _ => {}
            }
        }

        let mut command = Command {
            methods,
            args: &args[2..],
            environ,
        };

        let command_name = args[1];
        match command_name {
            "start" => command.start(),
            "stop" => command.stop(),
            "create" => command.create(),
            "create-extra-disk" => command.create_extra_disk(),
            "adjust" => command.adjust(),
            "destroy" => command.destroy(),
            "export" => command.export(),
            "import" => command.import(),
            "disk-op-status" => command.disk_op_status(),
            "resize" => command.resize(),
            "list" => command.list(),
            "share" => command.share(),
            "unshare" => command.unshare(),
            "container" => command.container(),
            "usb-attach" => command.usb_attach(),
            "usb-detach" => command.usb_detach(),
            "usb-list" => command.usb_list(),
            "pvm.send-problem-report" => command.pvm_send_problem_report(),
            _ => Err(UnknownSubcommand(command_name.to_owned()).into()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::proto::system_api::concierge_service::*;
    use crate::proto::system_api::dlcservice::*;
    use dbus::Message;
    use protobuf::Message as ProtoMessage;
    use std::collections::HashMap;

    fn mocked_connection_filter(mut msg: Message) -> Result<Message, Result<Message, dbus::Error>> {
        eprintln!("{:?}", msg);
        msg.set_serial(1);
        if let Some(member) = msg.member() {
            match member.as_cstr().to_bytes() {
                b"GetDlcState" => {
                    let mut dlc_state = DlcState::new();
                    dlc_state.state = DlcState_State::INSTALLED;
                    let msg_return = msg
                        .method_return()
                        .append1(dlc_state.write_to_bytes().unwrap());
                    return Err(Ok(msg_return));
                }
                b"StartVmPluginDispatcher" => {
                    let msg_return = msg.method_return().append1(true);
                    return Err(Ok(msg_return));
                }
                b"DestroyDiskImage" => {
                    let mut resp = DestroyDiskImageResponse::new();
                    resp.status = DiskImageStatus::DISK_STATUS_DOES_NOT_EXIST;
                    let msg_return = msg.method_return().append1(resp.write_to_bytes().unwrap());
                    return Err(Ok(msg_return));
                }
                b"RetrieveActiveSessions" => {
                    let sessions: HashMap<String, String> =
                        [("testuser@example.com".to_owned(), "fake_hash".to_owned())]
                            .iter()
                            .cloned()
                            .collect();
                    let msg_return = msg.method_return().append1(sessions);
                    return Err(Ok(msg_return));
                }
                _ => {}
            }
        }
        Ok(msg)
    }

    fn mocked_methods() -> Methods {
        let mut methods = Methods::dummy();
        methods
            .connection_proxy_mut()
            .set_filter(mocked_connection_filter);
        methods
    }

    #[test]
    fn arg_parsing() {
        const DUMMY_SUCCESS_ARGS: &[&[&str]] = &[
            &["vmc", "start", "termina"],
            &["vmc", "start", "--enable-gpu", "termina"],
            &["vmc", "start", "termina", "--enable-gpu"],
            &["vmc", "start", "termina", "--software-tpm"],
            &["vmc", "start", "termina", "--enable-gpu", "--software-tpm"],
            &["vmc", "start", "termina", "--enable-audio-capture"],
            &["vmc", "start", "termina", "--untrusted"],
            &[
                "vmc",
                "start",
                "termina",
                "--enable-audio-capture",
                "--enable-gpu",
            ],
            &[
                "vmc",
                "start",
                "termina",
                "--enable-gpu",
                "--enable-audio-capture",
            ],
            &["vmc", "stop", "termina"],
            &["vmc", "create", "termina"],
            &["vmc", "create", "-p", "termina"],
            &[
                "vmc",
                "create",
                "-p",
                "termina",
                "file name",
                "removable media",
            ],
            &["vmc", "create", "-p", "termina", "--"],
            &["vmc", "create", "-p", "termina", "--", "param"],
            &["vmc", "create-extra-disk", "--size=1000000", "foo.img"],
            &["vmc", "create-extra-disk", "--size=256M", "foo.img"],
            &["vmc", "create-extra-disk", "--size=1G", "foo.img"],
            &["vmc", "create-extra-disk", "--size", "1G", "foo.img"],
            &[
                "vmc",
                "create-extra-disk",
                "--size=1G",
                "--removable-media",
                "'USB Drive/foo.img'",
            ],
            &["vmc", "adjust", "termina", "op"],
            &["vmc", "adjust", "termina", "op", "param"],
            &["vmc", "destroy", "termina"],
            &["vmc", "disk-op-status", "12345"],
            &["vmc", "export", "termina", "file name"],
            &["vmc", "export", "-d", "termina", "file name"],
            &["vmc", "export", "termina", "file name", "removable media"],
            &[
                "vmc",
                "export",
                "-d",
                "termina",
                "file name",
                "removable media",
            ],
            &["vmc", "import", "termina", "file name"],
            &["vmc", "import", "termina", "file name", "removable media"],
            &["vmc", "import", "-p", "termina", "file name"],
            &[
                "vmc",
                "import",
                "-p",
                "termina",
                "file name",
                "removable media",
            ],
            &["vmc", "list"],
            &["vmc", "share", "termina", "my-folder"],
            &["vmc", "unshare", "termina", "my-folder"],
            &["vmc", "usb-attach", "termina", "1:2"],
            &["vmc", "usb-detach", "termina", "5"],
            &["vmc", "usb-detach", "termina", "5"],
            &["vmc", "usb-list", "termina"],
            &["vmc", "pvm.send-problem-report"],
            &["vmc", "pvm.send-problem-report", "text"],
            &["vmc", "pvm.send-problem-report", "text", "text2"],
            &[
                "vmc",
                "pvm.send-problem-report",
                "-n",
                "termina",
                "text",
                "text2",
            ],
            &[
                "vmc",
                "pvm.send-problem-report",
                "-e",
                "someone@somewhere.com",
                "text",
                "text2",
            ],
            &[
                "vmc",
                "pvm.send-problem-report",
                "-n",
                "termina",
                "-e",
                "someone@somewhere.com",
                "text",
                "text2",
            ],
            &["vmc", "--help"],
            &["vmc", "-h"],
        ];

        const DUMMY_FAILURE_ARGS: &[&[&str]] = &[
            &["vmc", "start"],
            &["vmc", "start", "--i-made-this-up", "termina"],
            &["vmc", "start", "termina", "extra args"],
            &["vmc", "stop"],
            &["vmc", "stop", "termina", "extra args"],
            &["vmc", "create"],
            &["vmc", "create", "-p"],
            &[
                "vmc",
                "create",
                "-p",
                "termina",
                "file name",
                "removable media",
                "extra args",
            ],
            &["vmc", "create-extra-disk"],
            &["vmc", "create-extra-disk", "foo.img"],
            &["vmc", "create-extra-disk", "--size", "1G"],
            &["vmc", "create-extra-disk", "--size", "foo.img"],
            &["vmc", "create-extra-disk", "--size=1G", "--removable-media"],
            &["vmc", "adjust"],
            &["vmc", "adjust", "termina"],
            &["vmc", "destroy"],
            &["vmc", "destroy", "termina", "extra args"],
            &["vmc", "disk-op-status"],
            &["vmc", "destroy", "12345", "extra args"],
            &["vmc", "export", "termina"],
            &["vmc", "export", "-d", "termina"],
            &["vmc", "export", "termina", "too", "many", "args"],
            &["vmc", "export", "-d", "termina", "too", "many", "args"],
            &["vmc", "import", "termina"],
            &["vmc", "import", "termina", "too", "many", "args"],
            &["vmc", "import", "-p", "termina"],
            &["vmc", "import", "-p", "termina", "too", "many", "args"],
            &["vmc", "list", "extra args"],
            &["vmc", "share"],
            &["vmc", "share", "too", "many", "args"],
            &["vmc", "unshare"],
            &["vmc", "unshare", "too", "many", "args"],
            &["vmc", "usb-attach"],
            &["vmc", "usb-attach", "termina"],
            &["vmc", "usb-attach", "termina", "whatever"],
            &["vmc", "usb-attach", "termina", "1:2:1dee:93d2"],
            &["vmc", "usb-detach"],
            &["vmc", "usb-detach", "not-a-number"],
            &["vmc", "usb-list"],
            &["vmc", "usb-list", "termina", "args"],
            &["vmc", "pvm.send-problem-report", "-e"],
            &["vmc", "pvm.send-problem-report", "-n"],
        ];

        let mut methods = mocked_methods();

        let environ = vec![("CROS_USER_ID_HASH", "fake_hash")]
            .into_iter()
            .collect();
        for args in DUMMY_SUCCESS_ARGS {
            if let Err(e) = Vmc.run(&mut methods, args, &environ) {
                panic!("test args failed: {:?}: {}", args, e)
            }
        }
        for args in DUMMY_FAILURE_ARGS {
            if let Ok(()) = Vmc.run(&mut methods, args, &environ) {
                panic!("test args should have failed: {:?}", args)
            }
        }
    }

    #[test]
    fn container() {
        const CONTAINER_ARGS: &[&[&str]] = &[
            &["vmc", "container", "termina", "penguin"],
            &[
                "vmc",
                "container",
                "termina",
                "penguin",
                "https://my-image-server.com/",
                "custom/os",
            ],
        ];

        // How |PRIVILEGED_FLAG| appears on the command line.
        const PRIVILEGED_FLAG_CMDLINE: &str = "--privileged";

        let mut methods = mocked_methods();

        let environ = vec![("CROS_USER_ID_HASH", "fake_hash")]
            .into_iter()
            .collect();
        for args in CONTAINER_ARGS {
            if let Err(e) = Vmc.run(&mut methods, args, &environ) {
                panic!("test args failed: {:?}: {}", args, e)
            }
        }

        // Test "--privileged" flag.
        const DUMMY_PRIVILEGED_SUCCESS_ARGS: &[&[&str]] = &[
            &[
                "vmc",
                "container",
                "termina",
                "penguin",
                PRIVILEGED_FLAG_CMDLINE,
                "true",
            ],
            &[
                "vmc",
                "container",
                "termina",
                PRIVILEGED_FLAG_CMDLINE,
                "false",
                "penguin",
            ],
            &[
                "vmc",
                "container",
                PRIVILEGED_FLAG_CMDLINE,
                "true",
                "termina",
                "penguin",
            ],
        ];

        for args in DUMMY_PRIVILEGED_SUCCESS_ARGS {
            if let Err(e) = Vmc.run(&mut methods, args, &environ) {
                panic!("test args failed: {:?}: {}", args, e)
            }
        }

        const DUMMY_PRIVILEGED_FAILURE_ARGS: &[&[&str]] = &[
            &["vmc", "container", PRIVILEGED_FLAG_CMDLINE],
            &["vmc", "container", PRIVILEGED_FLAG_CMDLINE, "termina"],
            &["vmc", "container", PRIVILEGED_FLAG_CMDLINE, "termina"],
            &[
                "vmc",
                "container",
                PRIVILEGED_FLAG_CMDLINE,
                "termina",
                "penguin",
            ],
        ];

        for args in DUMMY_PRIVILEGED_FAILURE_ARGS {
            if let Ok(()) = Vmc.run(&mut methods, args, &environ) {
                panic!("test args should have failed: {:?}", args)
            }
        }
    }
}
