// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// debugd dbus-constants.h
pub const DEBUGD_INTERFACE: &str = "org.chromium.debugd";
pub const DEBUGD_SERVICE_PATH: &str = "/org/chromium/debugd";
pub const DEBUGD_SERVICE_NAME: &str = "org.chromium.debugd";
pub const START_VM_CONCIERGE: &str = "StartVmConcierge";
pub const START_VM_PLUGIN_DISPATCHER: &str = "StartVmPluginDispatcher";

// Chrome dbus service_constants.h
pub const CHROME_FEATURES_INTERFACE: &str = "org.chromium.ChromeFeaturesServiceInterface";
pub const CHROME_FEATURES_SERVICE_PATH: &str = "/org/chromium/ChromeFeaturesService";
pub const CHROME_FEATURES_SERVICE_NAME: &str = "org.chromium.ChromeFeaturesService";
pub const IS_CROSTINI_ENABLED: &str = "IsCrostiniEnabled";
pub const IS_PLUGIN_VM_ENABLED: &str = "IsPluginVmEnabled";

// concierge dbus-constants.h
pub const VM_CONCIERGE_INTERFACE: &str = "org.chromium.VmConcierge";
pub const VM_CONCIERGE_SERVICE_PATH: &str = "/org/chromium/VmConcierge";
pub const VM_CONCIERGE_SERVICE_NAME: &str = "org.chromium.VmConcierge";
pub const START_VM_METHOD: &str = "StartVm";
pub const STOP_VM_METHOD: &str = "StopVm";
pub const STOP_ALL_VMS_METHOD: &str = "StopAllVms";
pub const GET_VM_INFO_METHOD: &str = "GetVmInfo";
pub const CREATE_DISK_IMAGE_METHOD: &str = "CreateDiskImage";
pub const DESTROY_DISK_IMAGE_METHOD: &str = "DestroyDiskImage";
pub const EXPORT_DISK_IMAGE_METHOD: &str = "ExportDiskImage";
pub const IMPORT_DISK_IMAGE_METHOD: &str = "ImportDiskImage";
pub const RESIZE_DISK_IMAGE_METHOD: &str = "ResizeDiskImage";
pub const DISK_IMAGE_STATUS_METHOD: &str = "DiskImageStatus";
pub const LIST_VM_DISKS_METHOD: &str = "ListVmDisks";
pub const START_CONTAINER_METHOD: &str = "StartContainer";
pub const GET_CONTAINER_SSH_KEYS_METHOD: &str = "GetContainerSshKeys";
pub const SYNC_VM_TIMES_METHOD: &str = "SyncVmTimes";
pub const ATTACH_USB_DEVICE_METHOD: &str = "AttachUsbDevice";
pub const DETACH_USB_DEVICE_METHOD: &str = "DetachUsbDevice";
pub const LIST_USB_DEVICE_METHOD: &str = "ListUsbDevices";
pub const ADJUST_VM_METHOD: &str = "AdjustVm";
pub const CONTAINER_STARTUP_FAILED_SIGNAL: &str = "ContainerStartupFailed";
pub const DISK_IMAGE_PROGRESS_SIGNAL: &str = "DiskImageProgress";

// vm_plugin_dispatcher dbus-constants.h
pub const VM_PLUGIN_DISPATCHER_INTERFACE: &str = "org.chromium.VmPluginDispatcher";
pub const VM_PLUGIN_DISPATCHER_SERVICE_PATH: &str = "/org/chromium/VmPluginDispatcher";
pub const VM_PLUGIN_DISPATCHER_SERVICE_NAME: &str = "org.chromium.VmPluginDispatcher";
pub const START_PLUGIN_VM_METHOD: &str = "StartVm";
pub const SHOW_PLUGIN_VM_METHOD: &str = "ShowVm";
pub const SEND_PVM_PROBLEM_REPORT_METHOD: &str = "SendProblemReport";

// cicerone dbus-constants.h
pub const VM_CICERONE_INTERFACE: &str = "org.chromium.VmCicerone";
pub const VM_CICERONE_SERVICE_PATH: &str = "/org/chromium/VmCicerone";
pub const VM_CICERONE_SERVICE_NAME: &str = "org.chromium.VmCicerone";
pub const NOTIFY_VM_STARTED_METHOD: &str = "NotifyVmStarted";
pub const NOTIFY_VM_STOPPED_METHOD: &str = "NotifyVmStopped";
pub const GET_CONTAINER_TOKEN_METHOD: &str = "GetContainerToken";
pub const LAUNCH_CONTAINER_APPLICATION_METHOD: &str = "LaunchContainerApplication";
pub const GET_CONTAINER_APP_ICON_METHOD: &str = "GetContainerAppIcon";
pub const LAUNCH_VSHD_METHOD: &str = "LaunchVshd";
pub const GET_LINUX_PACKAGE_INFO_METHOD: &str = "GetLinuxPackageInfo";
pub const INSTALL_LINUX_PACKAGE_METHOD: &str = "InstallLinuxPackage";
pub const UNINSTALL_PACKAGE_OWNING_FILE_METHOD: &str = "UninstallPackageOwningFile";
pub const CREATE_LXD_CONTAINER_METHOD: &str = "CreateLxdContainer";
pub const START_LXD_CONTAINER_METHOD: &str = "StartLxdContainer";
pub const GET_LXD_CONTAINER_USERNAME_METHOD: &str = "GetLxdContainerUsername";
pub const SET_UP_LXD_CONTAINER_USER_METHOD: &str = "SetUpLxdContainerUser";
pub const START_LXD_METHOD: &str = "StartLxd";
pub const GET_DEBUG_INFORMATION: &str = "GetDebugInformation";
pub const CONTAINER_STARTED_SIGNAL: &str = "ContainerStarted";
pub const CONTAINER_SHUTDOWN_SIGNAL: &str = "ContainerShutdown";
pub const INSTALL_LINUX_PACKAGE_PROGRESS_SIGNAL: &str = "InstallLinuxPackageProgress";
pub const UNINSTALL_PACKAGE_PROGRESS_SIGNAL: &str = "UninstallPackageProgress";
pub const LXD_CONTAINER_CREATED_SIGNAL: &str = "LxdContainerCreated";
pub const LXD_CONTAINER_DOWNLOADING_SIGNAL: &str = "LxdContainerDownloading";
pub const LXD_CONTAINER_STARTING_SIGNAL: &str = "LxdContainerStarting";
pub const TREMPLIN_STARTED_SIGNAL: &str = "TremplinStarted";
pub const START_LXD_PROGRESS_SIGNAL: &str = "StartLxdProgress";

// seneschal dbus-constants.h
pub const SENESCHAL_INTERFACE: &str = "org.chromium.Seneschal";
pub const SENESCHAL_SERVICE_PATH: &str = "/org/chromium/Seneschal";
pub const SENESCHAL_SERVICE_NAME: &str = "org.chromium.Seneschal";
pub const START_SERVER_METHOD: &str = "StartServer";
pub const STOP_SERVER_METHOD: &str = "StopServer";
pub const SHARE_PATH_METHOD: &str = "SharePath";
pub const UNSHARE_PATH_METHOD: &str = "UnsharePath";

// permission_broker dbus-constants.h
pub const PERMISSION_BROKER_INTERFACE: &str = "org.chromium.PermissionBroker";
pub const PERMISSION_BROKER_SERVICE_PATH: &str = "/org/chromium/PermissionBroker";
pub const PERMISSION_BROKER_SERVICE_NAME: &str = "org.chromium.PermissionBroker";
pub const CHECK_PATH_ACCESS: &str = "CheckPathAccess";
pub const OPEN_PATH: &str = "OpenPath";
pub const REQUEST_TCP_PORT_ACCESS: &str = "RequestTcpPortAccess";
pub const REQUEST_UDP_PORT_ACCESS: &str = "RequestUdpPortAccess";
pub const RELEASE_TCP_PORT: &str = "ReleaseTcpPort";
pub const RELEASE_UDP_PORT: &str = "ReleaseUdpPort";
pub const REQUEST_VPN_SETUP: &str = "RequestVpnSetup";
pub const REMOVE_VPN_SETUP: &str = "RemoveVpnSetup";
pub const POWER_CYCLE_USB_PORTS: &str = "PowerCycleUsbPorts";

// lock_to_single_user dbus-constants.h
pub const LOCK_TO_SINGLE_USER_INTERFACE: &str = "org.chromium.LockToSingleUser";
pub const LOCK_TO_SINGLE_USER_SERVICE_PATH: &str = "/org/chromium/LockToSingleUser";
pub const LOCK_TO_SINGLE_USER_SERVICE_NAME: &str = "org.chromium.LockToSingleUser";
pub const NOTIFY_VM_STARTING_METHOD: &str = "NotifyVmStarting";

// DLC service dbus-constants.h
pub const DLC_SERVICE_INTERFACE: &str = "org.chromium.DlcServiceInterface";
pub const DLC_SERVICE_PATH: &str = "/org/chromium/DlcService";
pub const DLC_SERVICE_NAME: &str = "org.chromium.DlcService";
pub const DLC_INSTALL_METHOD: &str = "InstallDlc";
pub const DLC_GET_STATE_METHOD: &str = "GetDlcState";
