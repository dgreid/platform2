// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::convert::{TryFrom, TryInto};
use std::error::Error;
use std::fmt;
use std::fs::{remove_file, OpenOptions};
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, IntoRawFd};
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::thread::sleep;
use std::time::Duration;

use dbus::{
    arg::OwnedFd,
    ffidisp::{BusType, Connection, ConnectionItem},
    Message,
};
use protobuf::Message as ProtoMessage;

use crate::disk::{DiskInfo, DiskOpType, VmDiskImageType};
use crate::lsb_release::{LsbRelease, ReleaseChannel};
use crate::proto::system_api::cicerone_service::*;
use crate::proto::system_api::concierge_service::*;
use crate::proto::system_api::dlcservice::*;
use crate::proto::system_api::seneschal_service::*;
use crate::proto::system_api::vm_plugin_dispatcher;
use crate::proto::system_api::vm_plugin_dispatcher::VmErrorCode;
use crate::proto::system_api::*;

const REMOVABLE_MEDIA_ROOT: &str = "/media/removable";
const CRYPTOHOME_USER: &str = "/home/user";
const DOWNLOADS_DIR: &str = "Downloads";
const MNT_SHARED_ROOT: &str = "/mnt/shared";

/// Round to disk block size.
const DEFAULT_TIMEOUT_MS: i32 = 80 * 1000;
const EXPORT_DISK_TIMEOUT_MS: i32 = 15 * 60 * 1000;

enum ChromeOSError {
    BadChromeFeatureStatus,
    BadDiskImageStatus(DiskImageStatus, String),
    BadPluginVmStatus(VmErrorCode),
    BadVmStatus(VmStatus, String),
    BadVmPluginDispatcherStatus,
    CrostiniVmDisabled,
    ExportPathExists,
    ImportPathDoesNotExist,
    FailedAdjustVm(String),
    FailedAttachUsb(String),
    FailedAllocateExtraDisk { path: String, errno: i32 },
    FailedCreateContainer(CreateLxdContainerResponse_Status, String),
    FailedCreateContainerSignal(LxdContainerCreatedSignal_Status, String),
    FailedDetachUsb(String),
    FailedDlcInstall(String, String),
    FailedGetOpenPath(PathBuf),
    FailedGetVmInfo,
    FailedListDiskImages(String),
    FailedListUsb,
    FailedMetricsSend { exit_code: Option<i32> },
    FailedOpenPath(dbus::Error),
    FailedSendProblemReport(String, i32),
    FailedSetupContainerUser(SetUpLxdContainerUserResponse_Status, String),
    FailedSharePath(String),
    FailedStartContainerStatus(StartLxdContainerResponse_Status, String),
    FailedStartLxdProgressSignal(StartLxdProgressSignal_Status, String),
    FailedStartLxdStatus(StartLxdResponse_Status, String),
    FailedStopVm { vm_name: String, reason: String },
    InvalidExportPath,
    InvalidImportPath,
    InvalidSourcePath,
    NoVmTechnologyEnabled,
    NotAvailableForPluginVm,
    NotPluginVm,
    PluginVmDisabled,
    RetrieveActiveSessions,
    SourcePathDoesNotExist,
    TpmOnStable,
}

use self::ChromeOSError::*;

impl fmt::Display for ChromeOSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            BadChromeFeatureStatus => write!(f, "invalid response to chrome feature request"),
            BadDiskImageStatus(s, reason) => {
                write!(f, "bad disk image status: `{:?}`: {}", s, reason)
            }
            BadPluginVmStatus(s) => write!(f, "bad VM status: `{:?}`", s),
            BadVmStatus(s, reason) => write!(f, "bad VM status: `{:?}`: {}", s, reason),
            BadVmPluginDispatcherStatus => write!(f, "failed to start Parallels dispatcher"),
            CrostiniVmDisabled => write!(f, "Crostini VMs are currently disabled"),
            ExportPathExists => write!(f, "disk export path already exists"),
            ImportPathDoesNotExist => write!(f, "disk import path does not exist"),
            FailedAdjustVm(reason) => write!(f, "failed to adjust vm: {}", reason),
            FailedAttachUsb(reason) => write!(f, "failed to attach usb device to vm: {}", reason),
            FailedAllocateExtraDisk { path, errno } => {
                write!(f, "failed to allocate an extra disk at {}: {}", path, errno)
            }
            FailedDetachUsb(reason) => write!(f, "failed to detach usb device from vm: {}", reason),
            FailedDlcInstall(name, reason) => write!(
                f,
                "DLC service failed to install module `{}`: {}",
                name, reason
            ),
            FailedCreateContainer(s, reason) => {
                write!(f, "failed to create container: `{:?}`: {}", s, reason)
            }
            FailedCreateContainerSignal(s, reason) => {
                write!(f, "failed to create container: `{:?}`: {}", s, reason)
            }
            FailedGetOpenPath(path) => write!(f, "failed to request OpenPath {}", path.display()),
            FailedGetVmInfo => write!(f, "failed to get vm info"),
            FailedSendProblemReport(msg, error_code) => {
                write!(f, "failed to send problem report: {} ({})", msg, error_code)
            }
            FailedSetupContainerUser(s, reason) => {
                write!(f, "failed to setup container user: `{:?}`: {}", s, reason)
            }
            FailedSharePath(reason) => write!(f, "failed to share path with vm: {}", reason),
            FailedStartContainerStatus(s, reason) => {
                write!(f, "failed to start container: `{:?}`: {}", s, reason)
            }
            FailedStartLxdProgressSignal(s, reason) => {
                write!(f, "failed to start lxd: `{:?}`: {}", s, reason)
            }
            FailedStartLxdStatus(s, reason) => {
                write!(f, "failed to start lxd: `{:?}`: {}", s, reason)
            }
            FailedListDiskImages(reason) => write!(f, "failed to list disk images: {}", reason),
            FailedListUsb => write!(f, "failed to get list of usb devices attached to vm"),
            FailedMetricsSend { exit_code } => {
                write!(f, "failed to send metrics")?;
                if let Some(code) = exit_code {
                    write!(f, ": exited with non-zero code {}", code)?;
                }
                Ok(())
            }
            FailedOpenPath(e) => write!(
                f,
                "failed permission_broker OpenPath: {}",
                e.message().unwrap_or("")
            ),
            FailedStopVm { vm_name, reason } => {
                write!(f, "failed to stop vm `{}`: {}", vm_name, reason)
            }
            InvalidExportPath => write!(f, "disk export path is invalid"),
            InvalidImportPath => write!(f, "disk import path is invalid"),
            InvalidSourcePath => write!(f, "source media path is invalid"),
            NoVmTechnologyEnabled => write!(f, "neither Crostini nor Parallels VMs are enabled"),
            NotAvailableForPluginVm => write!(f, "this command is not available for Parallels VM"),
            NotPluginVm => write!(f, "this VM is not a Parallels VM"),
            PluginVmDisabled => write!(f, "Parallels VMs are currently disabled"),
            RetrieveActiveSessions => write!(f, "failed to retrieve active sessions"),
            SourcePathDoesNotExist => write!(f, "source media path does not exist"),
            TpmOnStable => write!(f, "TPM device is not available on stable channel"),
        }
    }
}

impl fmt::Debug for ChromeOSError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}

impl Error for ChromeOSError {}

fn dbus_message_to_proto<T: ProtoMessage>(message: &Message) -> Result<T, Box<dyn Error>> {
    let raw_buffer: Vec<u8> = message.read1()?;
    let mut proto = T::new();
    proto.merge_from_bytes(&raw_buffer)?;
    Ok(proto)
}

#[derive(Default)]
pub struct VmFeatures {
    pub gpu: bool,
    pub software_tpm: bool,
    pub audio_capture: bool,
    pub run_as_untrusted: bool,
}

pub enum ContainerSource {
    ImageServer {
        image_alias: String,
        image_server: String,
    },
    Tarballs {
        rootfs_path: String,
        metadata_path: String,
    },
}

impl Default for ContainerSource {
    fn default() -> Self {
        ContainerSource::ImageServer {
            image_alias: "".to_string(),
            image_server: "".to_string(),
        }
    }
}

struct ProtobusSignalWatcher<'a> {
    connection: Option<&'a Connection>,
    interface: String,
    signal: String,
}

impl<'a> ProtobusSignalWatcher<'a> {
    fn new(
        connection: Option<&'a Connection>,
        interface: &str,
        signal: &str,
    ) -> Result<ProtobusSignalWatcher<'a>, Box<dyn Error>> {
        let out = ProtobusSignalWatcher {
            connection,
            interface: interface.to_owned(),
            signal: signal.to_owned(),
        };
        if let Some(connection) = out.connection {
            connection.add_match(&out.format_rule())?;
        }
        Ok(out)
    }

    fn format_rule(&self) -> String {
        format!("interface='{}',member='{}'", self.interface, self.signal)
    }

    fn wait<O: ProtoMessage>(&self, timeout_millis: i32) -> Result<O, Box<dyn Error>> {
        self.wait_with_filter(timeout_millis, |_| true)
    }

    fn wait_with_filter<O, F>(&self, timeout_millis: i32, predicate: F) -> Result<O, Box<dyn Error>>
    where
        O: ProtoMessage,
        F: Fn(&O) -> bool,
    {
        let connection = match self.connection.as_ref() {
            Some(c) => c,
            None => return Err("waiting for items on mocked connections is not implemented".into()),
        };
        for item in connection.iter(timeout_millis) {
            match item {
                ConnectionItem::Signal(message) => {
                    if let (Some(msg_interface), Some(msg_signal)) =
                        (message.interface(), message.member())
                    {
                        if msg_interface.as_cstr().to_string_lossy() == self.interface
                            && msg_signal.as_cstr().to_string_lossy() == self.signal
                        {
                            let proto_message: O = dbus_message_to_proto(&message)?;
                            if predicate(&proto_message) {
                                return Ok(proto_message);
                            }
                        }
                    }
                }
                ConnectionItem::Nothing => break,
                _ => {}
            };
        }
        Err("timeout while waiting for signal".into())
    }
}

impl Drop for ProtobusSignalWatcher<'_> {
    fn drop(&mut self) {
        if let Some(connection) = self.connection {
            connection
                .remove_match(&self.format_rule())
                .expect("unable to remove match rule on protobus");
        }
    }
}

pub trait FilterFn: 'static + Fn(Message) -> Result<Message, Result<Message, dbus::Error>> {}

impl<T> FilterFn for T where
    T: 'static + Fn(Message) -> Result<Message, Result<Message, dbus::Error>>
{
}

#[derive(Default)]
pub struct ConnectionProxy {
    connection: Option<Connection>,
    filter: Option<Box<dyn FilterFn>>,
}

impl From<Connection> for ConnectionProxy {
    fn from(connection: Connection) -> ConnectionProxy {
        ConnectionProxy {
            connection: Some(connection),
            ..Default::default()
        }
    }
}

impl ConnectionProxy {
    pub fn dummy() -> ConnectionProxy {
        Default::default()
    }

    pub fn set_filter<F: FilterFn>(&mut self, filter: F) {
        self.filter = Some(Box::new(filter));
    }

    fn send_with_reply_and_block(
        &self,
        msg: Message,
        timeout_millis: i32,
    ) -> Result<Message, dbus::Error> {
        let mut filtered_msg = match &self.filter {
            Some(filter) => match filter(msg) {
                Ok(new_msg) => new_msg,
                Err(res) => return res,
            },
            None => msg,
        };
        match &self.connection {
            Some(connection) => connection.send_with_reply_and_block(filtered_msg, timeout_millis),
            None => {
                // A serial number is required to assign to the method return message.
                filtered_msg.set_serial(1);
                Ok(Message::new_method_return(&filtered_msg).unwrap())
            }
        }
    }
}

#[derive(Default)]
pub struct UserDisks {
    pub kernel: Option<String>,
    pub rootfs: Option<String>,
    pub extra_disk: Option<String>,
}

/// Uses the standard ChromeOS interfaces to implement the methods with the least possible
/// privilege. Uses a combination of D-Bus, protobufs, and shell protocols.
pub struct Methods {
    connection: ConnectionProxy,
    crostini_enabled: Option<bool>,
    crostini_dlc: Option<bool>,
    plugin_vm_enabled: Option<bool>,
}

impl Methods {
    /// Initiates a D-Bus connection and returns an initialized `Methods`.
    pub fn new() -> Result<Methods, Box<dyn Error>> {
        let connection = Connection::get_private(BusType::System)?;
        Ok(Methods {
            connection: connection.into(),
            crostini_enabled: None,
            crostini_dlc: None,
            plugin_vm_enabled: None,
        })
    }

    #[cfg(test)]
    pub fn dummy() -> Methods {
        Methods {
            connection: ConnectionProxy::dummy(),
            crostini_enabled: Some(true),
            crostini_dlc: Some(true),
            plugin_vm_enabled: Some(true),
        }
    }

    pub fn connection_proxy_mut(&mut self) -> &mut ConnectionProxy {
        &mut self.connection
    }

    /// Helper for doing protobuf over dbus requests and responses.
    fn sync_protobus<I: ProtoMessage, O: ProtoMessage>(
        &self,
        message: Message,
        request: &I,
    ) -> Result<O, Box<dyn Error>> {
        self.sync_protobus_timeout(message, request, &[], DEFAULT_TIMEOUT_MS)
    }

    /// Helper for doing protobuf over dbus requests and responses.
    fn sync_protobus_timeout<I: ProtoMessage, O: ProtoMessage>(
        &self,
        message: Message,
        request: &I,
        fds: &[OwnedFd],
        timeout_millis: i32,
    ) -> Result<O, Box<dyn Error>> {
        let method = message.append1(request.write_to_bytes()?).append_ref(fds);
        let message = self
            .connection
            .send_with_reply_and_block(method, timeout_millis)?;
        dbus_message_to_proto(&message)
    }

    fn protobus_wait_for_signal_timeout<O: ProtoMessage>(
        &mut self,
        interface: &str,
        signal: &str,
        timeout_millis: i32,
    ) -> Result<O, Box<dyn Error>> {
        ProtobusSignalWatcher::new(self.connection.connection.as_ref(), interface, signal)?
            .wait(timeout_millis)
    }

    fn get_dlc_state(&mut self, name: &str) -> Result<DlcState_State, Box<dyn Error>> {
        let method = Message::new_method_call(
            DLC_SERVICE_SERVICE_NAME,
            DLC_SERVICE_SERVICE_PATH,
            DLC_SERVICE_INTERFACE,
            GET_DLC_STATE_METHOD,
        )?
        .append1(name);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)
            .map_err(|e| FailedDlcInstall(name.to_owned(), e.to_string()))?;

        let response: DlcState = dbus_message_to_proto(&message)
            .map_err(|e| FailedDlcInstall(name.to_owned(), e.to_string()))?;

        Ok(response.get_state())
    }

    fn init_dlc_install(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        let method = Message::new_method_call(
            DLC_SERVICE_SERVICE_NAME,
            DLC_SERVICE_SERVICE_PATH,
            DLC_SERVICE_INTERFACE,
            INSTALL_DLC_METHOD,
        )?
        .append1(name);

        self.connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)
            .map_err(|e| FailedDlcInstall(name.to_owned(), e.to_string()))?;

        Ok(())
    }

    fn poll_dlc_install(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        // Unfortunately DLC service does not provide a synchronous method to install package,
        // and, if package is already installed, OnInstallStatus signal might be issued before
        // replying to "Install" method call, which does not carry any indication whether the
        // operation in progress or not. So polling it is...
        while self.get_dlc_state(name)? == DlcState_State::INSTALLING {
            sleep(Duration::from_secs(5));
        }
        if self.get_dlc_state(name)? != DlcState_State::INSTALLED {
            return Err(FailedDlcInstall(
                name.to_owned(),
                "Failed to install Parallels DLC".to_string(),
            )
            .into());
        }
        Ok(())
    }

    fn install_dlc(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        if self.get_dlc_state(name)? != DlcState_State::INSTALLED {
            self.init_dlc_install(name)?;
            self.poll_dlc_install(name)?;
        }
        Ok(())
    }

    fn is_vm_type_enabled(
        &mut self,
        user_id_hash: &str,
        method_name: &str,
    ) -> Result<bool, Box<dyn Error>> {
        let method = Message::new_method_call(
            CHROME_FEATURES_SERVICE_NAME,
            CHROME_FEATURES_SERVICE_PATH,
            CHROME_FEATURES_SERVICE_INTERFACE,
            method_name,
        )?
        .append1(user_id_hash);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1() {
            Some(true) => Ok(true),
            Some(false) => Ok(false),
            _ => Err(BadChromeFeatureStatus.into()),
        }
    }

    fn is_chrome_feature_enabled(&mut self, feature_name: &str) -> Result<bool, Box<dyn Error>> {
        let method = Message::new_method_call(
            CHROME_FEATURES_SERVICE_NAME,
            CHROME_FEATURES_SERVICE_PATH,
            CHROME_FEATURES_SERVICE_INTERFACE,
            CHROME_FEATURES_SERVICE_IS_FEATURE_ENABLED_METHOD,
        )?
        .append1(feature_name);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1() {
            Some(true) => Ok(true),
            Some(false) => Ok(false),
            _ => Err(BadChromeFeatureStatus.into()),
        }
    }

    fn notify_vm_starting(&mut self) -> Result<(), Box<dyn Error>> {
        let method = Message::new_method_call(
            LOCK_TO_SINGLE_USER_SERVICE_NAME,
            LOCK_TO_SINGLE_USER_SERVICE_PATH,
            LOCK_TO_SINGLE_USER_INTERFACE,
            NOTIFY_VM_STARTING_METHOD,
        )?;

        self.connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;

        Ok(())
    }

    fn does_crostini_use_dlc(&mut self) -> Result<bool, Box<dyn Error>> {
        let enabled = match self.crostini_dlc {
            Some(value) => value,
            None => {
                let value = self.is_chrome_feature_enabled("CrostiniUseDlc")?;
                self.crostini_dlc = Some(value);
                value
            }
        };
        Ok(enabled)
    }

    fn is_crostini_enabled(&mut self, user_id_hash: &str) -> Result<bool, Box<dyn Error>> {
        let enabled = match self.crostini_enabled {
            Some(value) => value,
            None => {
                let value = self.is_vm_type_enabled(
                    user_id_hash,
                    CHROME_FEATURES_SERVICE_IS_CROSTINI_ENABLED_METHOD,
                )?;
                self.crostini_enabled = Some(value);
                value
            }
        };
        Ok(enabled)
    }

    fn is_plugin_vm_enabled(&mut self, user_id_hash: &str) -> Result<bool, Box<dyn Error>> {
        let enabled = match self.plugin_vm_enabled {
            Some(value) => value,
            None => {
                let value = self.is_vm_type_enabled(
                    user_id_hash,
                    CHROME_FEATURES_SERVICE_IS_PLUGIN_VM_ENABLED_METHOD,
                )?;
                self.plugin_vm_enabled = Some(value);
                value
            }
        };
        Ok(enabled)
    }

    /// Request debugd to start vmplugin_dispatcher.
    fn start_vm_plugin_dispatcher(&mut self, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        // Download and install pita component. If this fails we won't be able to start
        // the dispatcher service below.
        self.install_dlc("pita")?;

        let method = Message::new_method_call(
            DEBUGD_SERVICE_NAME,
            DEBUGD_SERVICE_PATH,
            DEBUGD_INTERFACE,
            START_VM_PLUGIN_DISPATCHER,
        )?
        .append1(user_id_hash)
        .append1("en-US");

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1() {
            Some(true) => Ok(()),
            _ => Err(BadVmPluginDispatcherStatus.into()),
        }
    }

    /// Starts all necessary VM services (currently just the Parallels dispatcher).
    fn start_vm_infrastructure(&mut self, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        if self.is_plugin_vm_enabled(user_id_hash)? {
            // Starting the dispatcher will also start concierge.
            self.start_vm_plugin_dispatcher(user_id_hash)
        } else if self.is_crostini_enabled(user_id_hash)? {
            Ok(())
        } else {
            Err(NoVmTechnologyEnabled.into())
        }
    }

    /// Request that concierge adjust an existing VM.
    fn adjust_vm(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        operation: &str,
        params: &[&str],
    ) -> Result<(), Box<dyn Error>> {
        let mut request = AdjustVmRequest::new();
        request.name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.operation = operation.to_owned();

        for param in params {
            request.mut_params().push(param.to_string());
        }

        let response: AdjustVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                ADJUST_VM_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedAdjustVm(response.failure_reason).into())
        }
    }

    /// Request that concierge create a disk image.
    fn create_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = CreateDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.image_type = DiskImageType::DISK_IMAGE_AUTO;
        request.storage_location = StorageLocation::STORAGE_CRYPTOHOME_ROOT;

        let response: CreateDiskImageResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                CREATE_DISK_IMAGE_METHOD,
            )?,
            &request,
        )?;

        match response.status {
            DiskImageStatus::DISK_STATUS_EXISTS | DiskImageStatus::DISK_STATUS_CREATED => {
                Ok(response.disk_path)
            }
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge create a new VM image.
    fn create_vm_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        source_name: Option<&str>,
        removable_media: Option<&str>,
        params: &[&str],
    ) -> Result<Option<String>, Box<dyn Error>> {
        let mut request = CreateDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.image_type = DiskImageType::DISK_IMAGE_AUTO;
        request.storage_location = if plugin_vm {
            if !self.is_plugin_vm_enabled(user_id_hash)? {
                return Err(PluginVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM
        } else {
            if !self.is_crostini_enabled(user_id_hash)? {
                return Err(CrostiniVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_ROOT
        };

        let source_fd = match source_name {
            Some(source) => {
                let source_path = match removable_media {
                    Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT)
                        .join(media_path)
                        .join(source),
                    None => Path::new(CRYPTOHOME_USER)
                        .join(user_id_hash)
                        .join(DOWNLOADS_DIR)
                        .join(source),
                };

                if source_path.components().any(|c| c == Component::ParentDir) {
                    return Err(InvalidSourcePath.into());
                }

                if !source_path.exists() {
                    return Err(SourcePathDoesNotExist.into());
                }

                let source_file = OpenOptions::new().read(true).open(source_path)?;
                request.source_size = source_file.metadata()?.len();
                // Safe because OwnedFd is given a valid owned fd.
                Some(unsafe { OwnedFd::new(source_file.into_raw_fd()) })
            }
            None => None,
        };

        for param in params {
            request.mut_params().push(param.to_string());
        }

        // We can't use sync_protobus because we need to append the file descriptor out of band from
        // the protobuf message.
        let mut method = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            CREATE_DISK_IMAGE_METHOD,
        )?
        .append1(request.write_to_bytes()?);
        if let Some(fd) = source_fd {
            method = method.append1(fd);
        }

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;

        let response: CreateDiskImageResponse = dbus_message_to_proto(&message)?;
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge create a disk image.
    fn destroy_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = DestroyDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();

        let response: DestroyDiskImageResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                DESTROY_DISK_IMAGE_METHOD,
            )?,
            &request,
        )?;

        match response.status {
            DiskImageStatus::DISK_STATUS_DESTROYED
            | DiskImageStatus::DISK_STATUS_DOES_NOT_EXIST => Ok(()),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    fn create_output_file(
        &mut self,
        user_id_hash: &str,
        name: &str,
        removable_media: Option<&str>,
    ) -> Result<std::fs::File, Box<dyn Error>> {
        let path = match removable_media {
            Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT).join(media_path).join(name),
            None => Path::new(CRYPTOHOME_USER)
                .join(user_id_hash)
                .join(DOWNLOADS_DIR)
                .join(name),
        };

        if path.components().any(|c| c == Component::ParentDir) {
            return Err(InvalidExportPath.into());
        }

        if path.exists() {
            return Err(ExportPathExists.into());
        }

        // Exporting the disk should always create a new file, and only be accessible to the user
        // that creates it. The old version of this used `O_NOFOLLOW` in its open flags, but this
        // has no effect as `O_NOFOLLOW` only preempts symlinks for the final part of the path,
        // which is guaranteed to not exist by `create_new(true)`.
        let file = OpenOptions::new()
            .write(true)
            .read(true)
            .create_new(true)
            .mode(0o600)
            .open(path)?;

        Ok(file)
    }

    /// Request that concierge export a VM's disk image.
    fn export_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        export_name: &str,
        digest_name: Option<&str>,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<dyn Error>> {
        let export_file = self.create_output_file(user_id_hash, export_name, removable_media)?;
        // Safe because OwnedFd is given a valid owned fd.
        let export_fd = unsafe { OwnedFd::new(export_file.into_raw_fd()) };

        let mut request = ExportDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.generate_sha256_digest = digest_name.is_some();

        // We can't use sync_protobus because we need to append the file descriptor out of band from
        // the protobuf message.
        let mut method = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            EXPORT_DISK_IMAGE_METHOD,
        )?
        .append1(request.write_to_bytes()?)
        .append1(export_fd);

        if let Some(name) = digest_name {
            let digest_file = self.create_output_file(user_id_hash, name, removable_media)?;
            // Safe because OwnedFd is given a valid owned fd.
            let digest_fd = unsafe { OwnedFd::new(digest_file.into_raw_fd()) };
            method = method.append1(digest_fd);
        }

        let message = self
            .connection
            .send_with_reply_and_block(method, EXPORT_DISK_TIMEOUT_MS)?;

        let response: ExportDiskImageResponse = dbus_message_to_proto(&message)?;
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge import a VM's disk image.
    fn import_disk_image(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        import_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<dyn Error>> {
        let import_path = match removable_media {
            Some(media_path) => Path::new(REMOVABLE_MEDIA_ROOT)
                .join(media_path)
                .join(import_name),
            None => Path::new(CRYPTOHOME_USER)
                .join(user_id_hash)
                .join(DOWNLOADS_DIR)
                .join(import_name),
        };

        if import_path.components().any(|c| c == Component::ParentDir) {
            return Err(InvalidImportPath.into());
        }

        if !import_path.exists() {
            return Err(ImportPathDoesNotExist.into());
        }

        let import_file = OpenOptions::new().read(true).open(import_path)?;
        let file_size = import_file.metadata()?.len();
        // Safe because OwnedFd is given a valid owned fd.
        let import_fd = unsafe { OwnedFd::new(import_file.into_raw_fd()) };

        let mut request = ImportDiskImageRequest::new();
        request.disk_path = vm_name.to_owned();
        request.cryptohome_id = user_id_hash.to_owned();
        request.storage_location = if plugin_vm {
            if !self.is_plugin_vm_enabled(user_id_hash)? {
                return Err(PluginVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM
        } else {
            if !self.is_crostini_enabled(user_id_hash)? {
                return Err(CrostiniVmDisabled.into());
            }
            StorageLocation::STORAGE_CRYPTOHOME_ROOT
        };
        request.source_size = file_size;

        // We can't use sync_protobus because we need to append the file descriptor out of band from
        // the protobuf message.
        let method = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            IMPORT_DISK_IMAGE_METHOD,
        )?
        .append1(request.write_to_bytes()?)
        .append1(import_fd);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;

        let response: ImportDiskImageResponse = dbus_message_to_proto(&message)?;
        match response.status {
            DiskImageStatus::DISK_STATUS_CREATED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    /// Request that concierge resize the VM's disk.
    fn resize_disk(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        size: u64,
    ) -> Result<Option<String>, Box<dyn Error>> {
        let mut request = ResizeDiskImageRequest::new();
        request.cryptohome_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.disk_size = size;

        let response: ResizeDiskImageResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                RESIZE_DISK_IMAGE_METHOD,
            )?,
            &request,
        )?;

        match response.status {
            DiskImageStatus::DISK_STATUS_RESIZED => Ok(None),
            DiskImageStatus::DISK_STATUS_IN_PROGRESS => Ok(Some(response.command_uuid)),
            _ => Err(BadDiskImageStatus(response.status, response.failure_reason).into()),
        }
    }

    fn parse_disk_op_status(
        &mut self,
        response: DiskImageStatusResponse,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        let expected_status = match op_type {
            DiskOpType::Create => DiskImageStatus::DISK_STATUS_CREATED,
            DiskOpType::Resize => DiskImageStatus::DISK_STATUS_RESIZED,
        };
        if response.status == expected_status {
            Ok((true, response.progress))
        } else if response.status == DiskImageStatus::DISK_STATUS_IN_PROGRESS {
            Ok((false, response.progress))
        } else {
            Err(BadDiskImageStatus(response.status, response.failure_reason).into())
        }
    }

    /// Request concierge to provide status of a disk operation (import or export) with given UUID.
    fn check_disk_operation(
        &mut self,
        uuid: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        let mut request = DiskImageStatusRequest::new();
        request.command_uuid = uuid.to_owned();

        let response: DiskImageStatusResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                DISK_IMAGE_STATUS_METHOD,
            )?,
            &request,
        )?;

        self.parse_disk_op_status(response, op_type)
    }

    /// Wait for updated status of a disk operation (import or export) with given UUID.
    fn wait_disk_operation(
        &mut self,
        uuid: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        loop {
            let response: DiskImageStatusResponse = self.protobus_wait_for_signal_timeout(
                VM_CONCIERGE_INTERFACE,
                DISK_IMAGE_PROGRESS_SIGNAL,
                DEFAULT_TIMEOUT_MS,
            )?;

            if response.command_uuid == uuid {
                return self.parse_disk_op_status(response, op_type);
            }
        }
    }

    /// Request a list of disk images from concierge.
    fn list_disk_images(
        &mut self,
        user_id_hash: &str,
        target_location: Option<StorageLocation>,
        target_name: Option<&str>,
    ) -> Result<(Vec<VmDiskInfo>, u64), Box<dyn Error>> {
        let mut request = ListVmDisksRequest::new();
        request.cryptohome_id = user_id_hash.to_owned();
        match target_location {
            Some(location) => request.storage_location = location,
            None => request.all_locations = true,
        };
        if let Some(vm_name) = target_name {
            request.vm_name = vm_name.to_string();
        }

        let response: ListVmDisksResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                LIST_VM_DISKS_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok((response.images.into(), response.total_size))
        } else {
            Err(FailedListDiskImages(response.failure_reason).into())
        }
    }

    /// Checks if VM with given name/disk is running in Parallels.
    fn is_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<bool, Box<dyn Error>> {
        let (images, _) = self.list_disk_images(
            user_id_hash,
            Some(StorageLocation::STORAGE_CRYPTOHOME_PLUGINVM),
            Some(vm_name),
        )?;
        Ok(images.len() != 0)
    }

    /// Request that concierge start a vm with the given disk image.
    fn start_vm_with_disk(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        features: VmFeatures,
        stateful_disk_path: String,
        user_disks: UserDisks,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = StartVmRequest::new();
        if self.does_crostini_use_dlc()? {
            let mut vm = VirtualMachineSpec::new();
            vm.dlc_id = "termina-dlc".to_owned();
            request.vm = protobuf::SingularPtrField::some(vm);
        }
        request.start_termina = true;
        request.owner_id = user_id_hash.to_owned();
        request.enable_gpu = features.gpu;
        request.software_tpm = features.software_tpm;
        request.enable_audio_capture = features.audio_capture;
        request.run_as_untrusted = features.run_as_untrusted;
        request.name = vm_name.to_owned();
        {
            let disk_image = request.mut_disks().push_default();
            disk_image.path = stateful_disk_path;
            disk_image.writable = true;
            disk_image.do_mount = false;
        }
        let tremplin_started = ProtobusSignalWatcher::new(
            self.connection.connection.as_ref(),
            VM_CICERONE_INTERFACE,
            TREMPLIN_STARTED_SIGNAL,
        )?;

        let message = Message::new_method_call(
            VM_CONCIERGE_SERVICE_NAME,
            VM_CONCIERGE_SERVICE_PATH,
            VM_CONCIERGE_INTERFACE,
            START_VM_METHOD,
        )?;

        let mut disk_files = vec![];
        // User-specified kernel
        if let Some(path) = user_disks.kernel {
            request.use_fd_for_kernel = true;
            disk_files.push(
                OpenOptions::new()
                    .read(true)
                    .custom_flags(libc::O_NOFOLLOW)
                    .open(&path)?,
            );
        }

        // User-specified rootfs
        if let Some(path) = user_disks.rootfs {
            request.use_fd_for_rootfs = true;
            disk_files.push(
                OpenOptions::new()
                    .read(true)
                    .custom_flags(libc::O_NOFOLLOW)
                    .open(&path)?,
            );
        }

        // User-specified extra disk
        if let Some(path) = user_disks.extra_disk {
            request.use_fd_for_storage = true;
            disk_files.push(
                OpenOptions::new()
                    .read(true)
                    .write(true) // extra disk is writable
                    .custom_flags(libc::O_NOFOLLOW)
                    .open(&path)?,
            );
        }

        let owned_fds: Vec<OwnedFd> = disk_files
            .into_iter()
            .map(|f| {
                let raw_fd = f.into_raw_fd();
                // Safe because `raw_fd` is a valid and we are the unique owner of this descriptor.
                unsafe { OwnedFd::new(raw_fd) }
            })
            .collect();

        // Send a protobuf request with the FDs.
        let response: StartVmResponse =
            self.sync_protobus_timeout(message, &request, &owned_fds, DEFAULT_TIMEOUT_MS)?;

        match response.status {
            VmStatus::VM_STATUS_STARTING => {
                assert!(response.success);
                tremplin_started
                    .wait_with_filter(DEFAULT_TIMEOUT_MS, |s: &TremplinStartedSignal| {
                        s.vm_name == vm_name && s.owner_id == user_id_hash
                    })?;
                Ok(())
            }
            VmStatus::VM_STATUS_RUNNING => {
                assert!(response.success);
                Ok(())
            }
            _ => Err(BadVmStatus(response.status, response.failure_reason).into()),
        }
    }

    fn start_lxd(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = StartLxdRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();

        let lxd_started = ProtobusSignalWatcher::new(
            self.connection.connection.as_ref(),
            VM_CICERONE_INTERFACE,
            START_LXD_PROGRESS_SIGNAL,
        )?;

        let response: StartLxdResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                START_LXD_METHOD,
            )?,
            &request,
        )?;

        use self::StartLxdResponse_Status::*;
        match response.status {
            STARTING => {
                use self::StartLxdProgressSignal_Status::*;
                let signal = lxd_started.wait_with_filter(
                    DEFAULT_TIMEOUT_MS,
                    |s: &StartLxdProgressSignal| {
                        s.vm_name == vm_name
                            && s.owner_id == user_id_hash
                            && s.status != STARTING
                            && s.status != RECOVERING
                    },
                )?;
                match signal.status {
                    STARTED => Ok(()),
                    STARTING | RECOVERING => unreachable!(),
                    UNKNOWN | FAILED => Err(FailedStartLxdProgressSignal(
                        signal.status,
                        signal.failure_reason,
                    )
                    .into()),
                }
            }
            ALREADY_RUNNING => Ok(()),
            UNKNOWN | FAILED => {
                Err(FailedStartLxdStatus(response.status, response.failure_reason).into())
            }
        }
    }

    /// Request that dispatcher start given Parallels VM.
    fn start_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = vm_plugin_dispatcher::StartVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name_uuid = vm_name.to_owned();

        let response: vm_plugin_dispatcher::StartVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                START_VM_METHOD,
            )?,
            &request,
        )?;

        match response.error {
            VmErrorCode::VM_SUCCESS => Ok(()),
            _ => Err(BadPluginVmStatus(response.error).into()),
        }
    }

    /// Request that dispatcher starts application responsible for rendering Parallels VM window.
    fn show_plugin_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = vm_plugin_dispatcher::ShowVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name_uuid = vm_name.to_owned();

        let response: vm_plugin_dispatcher::ShowVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                SHOW_VM_METHOD,
            )?,
            &request,
        )?;

        match response.error {
            VmErrorCode::VM_SUCCESS => Ok(()),
            _ => Err(BadPluginVmStatus(response.error).into()),
        }
    }

    /// Request that `concierge` stop a vm with the given disk image.
    fn stop_vm(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        let mut request = StopVmRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: StopVmResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                STOP_VM_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedStopVm {
                vm_name: vm_name.to_owned(),
                reason: response.failure_reason,
            }
            .into())
        }
    }

    // Request `VmInfo` from concierge about given `vm_name`.
    fn get_vm_info(&mut self, vm_name: &str, user_id_hash: &str) -> Result<VmInfo, Box<dyn Error>> {
        let mut request = GetVmInfoRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.name = vm_name.to_owned();

        let response: GetVmInfoResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                GET_VM_INFO_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.vm_info.unwrap_or_default())
        } else {
            Err(FailedGetVmInfo.into())
        }
    }

    // Request the given `path` be shared with the seneschal instance associated with the desired
    // vm, owned by `user_id_hash`.
    fn share_path_with_vm(
        &mut self,
        seneschal_handle: u32,
        user_id_hash: &str,
        path: &str,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = SharePathRequest::new();
        request.handle = seneschal_handle;
        request.shared_path.set_default().path = path.to_owned();
        request.storage_location = SharePathRequest_StorageLocation::MY_FILES;
        request.owner_id = user_id_hash.to_owned();

        let response: SharePathResponse = self.sync_protobus(
            Message::new_method_call(
                SENESCHAL_SERVICE_NAME,
                SENESCHAL_SERVICE_PATH,
                SENESCHAL_INTERFACE,
                SHARE_PATH_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.path)
        } else {
            Err(FailedSharePath(response.failure_reason).into())
        }
    }

    // Request the given `path` be no longer shared with the vm associated with given seneshal
    // instance.
    fn unshare_path_with_vm(
        &mut self,
        seneschal_handle: u32,
        path: &str,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = UnsharePathRequest::new();
        request.handle = seneschal_handle;
        request.path = format!("MyFiles/{}", path);

        let response: SharePathResponse = self.sync_protobus(
            Message::new_method_call(
                SENESCHAL_SERVICE_NAME,
                SENESCHAL_SERVICE_PATH,
                SENESCHAL_INTERFACE,
                UNSHARE_PATH_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedSharePath(response.failure_reason).into())
        }
    }

    fn create_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        source: ContainerSource,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = CreateLxdContainerRequest::new();
        request.vm_name = vm_name.to_owned();
        request.container_name = container_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        match source {
            ContainerSource::ImageServer {
                image_alias,
                image_server,
            } => {
                request.image_server = image_server.to_owned();
                request.image_alias = image_alias.to_owned();
            }
            ContainerSource::Tarballs {
                rootfs_path,
                metadata_path,
            } => {
                request.rootfs_path = rootfs_path.to_owned();
                request.metadata_path = metadata_path.to_owned();
            }
        }

        let response: CreateLxdContainerResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                CREATE_LXD_CONTAINER_METHOD,
            )?,
            &request,
        )?;

        use self::CreateLxdContainerResponse_Status::*;
        use self::LxdContainerCreatedSignal_Status::*;
        match response.status {
            CREATING => {
                let signal: LxdContainerCreatedSignal = self.protobus_wait_for_signal_timeout(
                    VM_CICERONE_INTERFACE,
                    LXD_CONTAINER_CREATED_SIGNAL,
                    DEFAULT_TIMEOUT_MS,
                )?;
                match signal.status {
                    CREATED => Ok(()),
                    _ => Err(
                        FailedCreateContainerSignal(signal.status, signal.failure_reason).into(),
                    ),
                }
            }
            EXISTS => Ok(()),
            _ => Err(FailedCreateContainer(response.status, response.failure_reason).into()),
        }
    }

    fn start_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        privilege_level: StartLxdContainerRequest_PrivilegeLevel,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = StartLxdContainerRequest::new();
        request.vm_name = vm_name.to_owned();
        request.container_name = container_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.privilege_level = privilege_level;

        let response: StartLxdContainerResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                START_LXD_CONTAINER_METHOD,
            )?,
            &request,
        )?;

        use self::StartLxdContainerResponse_Status::*;
        match response.status {
            // |REMAPPING| happens when the privilege level of a container was changed before this
            // boot. It's a long running operation and when it happens it's returned in lieu of
            // |STARTING|. It makes sense to treat them the same way.
            STARTING | REMAPPING => {
                // TODO: listen for signal before calling the D-Bus method.
                let _signal: cicerone_service::ContainerStartedSignal = self
                    .protobus_wait_for_signal_timeout(
                        VM_CICERONE_INTERFACE,
                        CONTAINER_STARTED_SIGNAL,
                        DEFAULT_TIMEOUT_MS,
                    )?;
                Ok(())
            }
            RUNNING => Ok(()),
            _ => Err(FailedStartContainerStatus(response.status, response.failure_reason).into()),
        }
    }

    fn setup_container_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        username: &str,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = SetUpLxdContainerUserRequest::new();
        request.vm_name = vm_name.to_owned();
        request.owner_id = user_id_hash.to_owned();
        request.container_name = container_name.to_owned();
        request.container_username = username.to_owned();

        let response: SetUpLxdContainerUserResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CICERONE_SERVICE_NAME,
                VM_CICERONE_SERVICE_PATH,
                VM_CICERONE_INTERFACE,
                SET_UP_LXD_CONTAINER_USER_METHOD,
            )?,
            &request,
        )?;

        use self::SetUpLxdContainerUserResponse_Status::*;
        match response.status {
            SUCCESS | EXISTS => Ok(()),
            _ => Err(FailedSetupContainerUser(response.status, response.failure_reason).into()),
        }
    }

    fn attach_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        bus: u8,
        device: u8,
        usb_fd: OwnedFd,
    ) -> Result<u8, Box<dyn Error>> {
        let mut request = AttachUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.bus_number = bus as u32;
        request.port_number = device as u32;

        let response: AttachUsbDeviceResponse = self.sync_protobus_timeout(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                ATTACH_USB_DEVICE_METHOD,
            )?,
            &request,
            &[usb_fd],
            DEFAULT_TIMEOUT_MS,
        )?;

        if response.success {
            Ok(response.guest_port as u8)
        } else {
            Err(FailedAttachUsb(response.reason).into())
        }
    }

    fn detach_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        port: u8,
    ) -> Result<(), Box<dyn Error>> {
        let mut request = DetachUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();
        request.guest_port = port as u32;

        let response: DetachUsbDeviceResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                DETACH_USB_DEVICE_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(())
        } else {
            Err(FailedDetachUsb(response.reason).into())
        }
    }

    fn list_usb(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<Vec<UsbDeviceMessage>, Box<dyn Error>> {
        let mut request = ListUsbDeviceRequest::new();
        request.owner_id = user_id_hash.to_owned();
        request.vm_name = vm_name.to_owned();

        let response: ListUsbDeviceResponse = self.sync_protobus(
            Message::new_method_call(
                VM_CONCIERGE_SERVICE_NAME,
                VM_CONCIERGE_SERVICE_PATH,
                VM_CONCIERGE_INTERFACE,
                LIST_USB_DEVICE_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.usb_devices.into())
        } else {
            Err(FailedListUsb.into())
        }
    }

    fn permission_broker_open_path(&mut self, path: &Path) -> Result<OwnedFd, Box<dyn Error>> {
        let path_str = path
            .to_str()
            .ok_or_else(|| FailedGetOpenPath(path.into()))?;
        let method = Message::new_method_call(
            PERMISSION_BROKER_SERVICE_NAME,
            PERMISSION_BROKER_SERVICE_PATH,
            PERMISSION_BROKER_INTERFACE,
            OPEN_PATH,
        )?
        .append1(path_str);

        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)
            .map_err(FailedOpenPath)?;

        message
            .get1()
            .ok_or_else(|| FailedGetOpenPath(path.into()).into())
    }

    fn send_problem_report_for_plugin_vm(
        &mut self,
        vm_name: Option<String>,
        user_id_hash: &str,
        email: Option<String>,
        text: Option<String>,
    ) -> Result<String, Box<dyn Error>> {
        let mut request = vm_plugin_dispatcher::SendProblemReportRequest::new();
        request.owner_id = user_id_hash.to_owned();

        if let Some(name) = vm_name {
            if !self.is_plugin_vm(name.as_str(), user_id_hash)? {
                return Err(NotPluginVm.into());
            }
            request.vm_name_uuid = name;
        }

        if !self.is_plugin_vm_enabled(user_id_hash)? {
            return Err(PluginVmDisabled.into());
        }

        request.detailed = true;

        if let Some(email_str) = email {
            request.email = email_str;
        }

        if let Some(text_str) = text {
            request.description = text_str;
        }

        let response: vm_plugin_dispatcher::SendProblemReportResponse = self.sync_protobus(
            Message::new_method_call(
                VM_PLUGIN_DISPATCHER_SERVICE_NAME,
                VM_PLUGIN_DISPATCHER_SERVICE_PATH,
                VM_PLUGIN_DISPATCHER_INTERFACE,
                SEND_PROBLEM_REPORT_METHOD,
            )?,
            &request,
        )?;

        if response.success {
            Ok(response.report_id)
        } else {
            Err(FailedSendProblemReport(response.error_message, response.result_code).into())
        }
    }

    pub fn metrics_send_sample(&mut self, name: &str) -> Result<(), Box<dyn Error>> {
        #![allow(unreachable_code)]
        let _ = name;
        // Metrics are not appropriate for test builds
        #[cfg(test)]
        return Ok(());

        let status = Command::new("metrics_client")
            .arg("-v")
            .arg(name)
            .status()?;
        if !status.success() {
            return Err(FailedMetricsSend {
                exit_code: status.code(),
            }
            .into());
        }
        Ok(())
    }

    pub fn sessions_list(&mut self) -> Result<Vec<(String, String)>, Box<dyn Error>> {
        let method = Message::new_method_call(
            "org.chromium.SessionManager",
            "/org/chromium/SessionManager",
            "org.chromium.SessionManagerInterface",
            "RetrieveActiveSessions",
        )?;
        let message = self
            .connection
            .send_with_reply_and_block(method, DEFAULT_TIMEOUT_MS)?;
        match message.get1::<HashMap<String, String>>() {
            Some(sessions) => Ok(sessions.into_iter().collect()),
            _ => Err(RetrieveActiveSessions.into()),
        }
    }

    pub fn vm_create(
        &mut self,
        name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        source_name: Option<&str>,
        removable_media: Option<&str>,
        params: &[&str],
    ) -> Result<Option<String>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.create_vm_image(
            name,
            user_id_hash,
            plugin_vm,
            source_name,
            removable_media,
            params,
        )
    }

    pub fn vm_adjust(
        &mut self,
        name: &str,
        user_id_hash: &str,
        operation: &str,
        params: &[&str],
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.adjust_vm(name, user_id_hash, operation, params)
    }

    pub fn vm_start(
        &mut self,
        name: &str,
        user_id_hash: &str,
        features: VmFeatures,
        user_disks: UserDisks,
        start_lxd: bool,
    ) -> Result<(), Box<dyn Error>> {
        self.notify_vm_starting()?;
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(name, user_id_hash)? {
            if !self.is_plugin_vm_enabled(user_id_hash)? {
                return Err(PluginVmDisabled.into());
            }
            self.start_plugin_vm(name, user_id_hash)
        } else {
            if !self.is_crostini_enabled(user_id_hash)? {
                return Err(CrostiniVmDisabled.into());
            }

            let is_stable_channel = is_stable_channel();
            if features.software_tpm && is_stable_channel {
                return Err(TpmOnStable.into());
            }

            let disk_image_path = self.create_disk_image(name, user_id_hash)?;
            self.start_vm_with_disk(name, user_id_hash, features, disk_image_path, user_disks)?;
            if start_lxd {
                self.start_lxd(name, user_id_hash)?;
            }
            Ok(())
        }
    }

    pub fn vm_stop(&mut self, name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.stop_vm(name, user_id_hash)
    }

    pub fn vm_export(
        &mut self,
        name: &str,
        user_id_hash: &str,
        file_name: &str,
        digest_name: Option<&str>,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.export_disk_image(name, user_id_hash, file_name, digest_name, removable_media)
    }

    pub fn vm_import(
        &mut self,
        name: &str,
        user_id_hash: &str,
        plugin_vm: bool,
        file_name: &str,
        removable_media: Option<&str>,
    ) -> Result<Option<String>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.import_disk_image(name, user_id_hash, plugin_vm, file_name, removable_media)
    }

    pub fn vm_share_path(
        &mut self,
        name: &str,
        user_id_hash: &str,
        path: &str,
    ) -> Result<String, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let vm_info = self.get_vm_info(name, user_id_hash)?;
        let vm_path = self.share_path_with_vm(
            vm_info.seneschal_server_handle.try_into()?,
            user_id_hash,
            path,
        )?;
        Ok(format!("{}/{}", MNT_SHARED_ROOT, vm_path))
    }

    pub fn vm_unshare_path(
        &mut self,
        name: &str,
        user_id_hash: &str,
        path: &str,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let vm_info = self.get_vm_info(name, user_id_hash)?;
        self.unshare_path_with_vm(vm_info.seneschal_server_handle.try_into()?, path)
    }

    pub fn vsh_exec(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            self.show_plugin_vm(vm_name, user_id_hash)
        } else {
            Command::new("vsh")
                .arg(format!("--vm_name={}", vm_name))
                .arg(format!("--owner_id={}", user_id_hash))
                .args(&[
                    "--",
                    "LXD_DIR=/mnt/stateful/lxd",
                    "LXD_CONF=/mnt/stateful/lxd_conf",
                ])
                .status()?;
            Ok(())
        }
    }

    pub fn vsh_exec_container(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
    ) -> Result<(), Box<dyn Error>> {
        Command::new("vsh")
            .arg(format!("--vm_name={}", vm_name))
            .arg(format!("--owner_id={}", user_id_hash))
            .arg(format!("--target_container={}", container_name))
            .args(&[
                "--",
                "LXD_DIR=/mnt/stateful/lxd",
                "LXD_CONF=/mnt/stateful/lxd_conf",
            ])
            .status()?;

        Ok(())
    }

    pub fn disk_destroy(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.destroy_disk_image(vm_name, user_id_hash)
    }

    pub fn disk_list(
        &mut self,
        user_id_hash: &str,
    ) -> Result<(Vec<DiskInfo>, u64), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let (images, total_size) = self.list_disk_images(user_id_hash, None, None)?;
        let out_images: Vec<DiskInfo> = images
            .into_iter()
            .map(|e| DiskInfo {
                name: e.name,
                size: e.size,
                min_size: if e.min_size != 0 {
                    Some(e.min_size)
                } else {
                    None
                },
                image_type: match e.image_type {
                    DiskImageType::DISK_IMAGE_RAW => VmDiskImageType::Raw,
                    DiskImageType::DISK_IMAGE_QCOW2 => VmDiskImageType::Qcow2,
                    DiskImageType::DISK_IMAGE_AUTO => VmDiskImageType::Auto,
                    DiskImageType::DISK_IMAGE_PLUGINVM => VmDiskImageType::PluginVm,
                },
                user_chosen_size: e.user_chosen_size,
            })
            .collect();
        Ok((out_images, total_size))
    }

    pub fn disk_resize(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        size: u64,
    ) -> Result<Option<String>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.resize_disk(vm_name, user_id_hash, size)
    }

    pub fn disk_op_status(
        &mut self,
        uuid: &str,
        user_id_hash: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.check_disk_operation(uuid, op_type)
    }

    pub fn wait_disk_op(
        &mut self,
        uuid: &str,
        user_id_hash: &str,
        op_type: DiskOpType,
    ) -> Result<(bool, u32), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.wait_disk_operation(uuid, op_type)
    }

    pub fn extra_disk_create(&mut self, path: &str, disk_size: u64) -> Result<(), Box<dyn Error>> {
        // When testing, don't create a file.
        if cfg!(test) {
            return Ok(());
        }

        // Validate `disk_size`.
        let disk_size =
            libc::off64_t::try_from(disk_size).map_err(|_| FailedAllocateExtraDisk {
                path: path.to_owned(),
                errno: libc::EINVAL,
            })?;

        let file = OpenOptions::new()
            .create_new(true)
            .read(true)
            .write(true)
            .open(path)?;

        // Truncate a disk file.
        // Safe since we pass in a valid fd and disk_size.
        let ret = unsafe { libc::fallocate64(file.as_raw_fd(), 0, 0, disk_size) };
        if ret < 0 {
            let errno = unsafe { *libc::__errno_location() };
            let _ = remove_file(path);
            return Err(FailedAllocateExtraDisk {
                path: path.to_owned(),
                errno,
            }
            .into());
        }
        Ok(())
    }

    pub fn container_create(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        source: ContainerSource,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.create_container(vm_name, user_id_hash, container_name, source)
    }

    pub fn container_start(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        privilege_level: StartLxdContainerRequest_PrivilegeLevel,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.start_container(vm_name, user_id_hash, container_name, privilege_level)
    }

    pub fn container_setup_user(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        container_name: &str,
        username: &str,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        if self.is_plugin_vm(vm_name, user_id_hash)? {
            return Err(NotAvailableForPluginVm.into());
        }

        self.setup_container_user(vm_name, user_id_hash, container_name, username)
    }

    pub fn usb_attach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        bus: u8,
        device: u8,
    ) -> Result<u8, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let usb_file_path = format!("/dev/bus/usb/{:03}/{:03}", bus, device);
        let usb_fd = self.permission_broker_open_path(Path::new(&usb_file_path))?;
        self.attach_usb(vm_name, user_id_hash, bus, device, usb_fd)
    }

    pub fn usb_detach(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
        port: u8,
    ) -> Result<(), Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.detach_usb(vm_name, user_id_hash, port)
    }

    pub fn usb_list(
        &mut self,
        vm_name: &str,
        user_id_hash: &str,
    ) -> Result<Vec<(u8, u16, u16, String)>, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        let device_list = self
            .list_usb(vm_name, user_id_hash)?
            .into_iter()
            .map(|mut d| {
                (
                    d.get_guest_port() as u8,
                    d.get_vendor_id() as u16,
                    d.get_product_id() as u16,
                    d.take_device_name(),
                )
            })
            .collect();
        Ok(device_list)
    }

    pub fn pvm_send_problem_report(
        &mut self,
        vm_name: Option<String>,
        user_id_hash: &str,
        email: Option<String>,
        text: Option<String>,
    ) -> Result<String, Box<dyn Error>> {
        self.start_vm_infrastructure(user_id_hash)?;
        self.send_problem_report_for_plugin_vm(vm_name, user_id_hash, email, text)
    }
}

fn is_stable_channel() -> bool {
    match LsbRelease::gather() {
        Ok(lsb) => lsb.release_channel() == Some(ReleaseChannel::Stable),
        Err(_) => {
            // Weird /etc/lsb-release, do not enforce stable restrictions.
            false
        }
    }
}
