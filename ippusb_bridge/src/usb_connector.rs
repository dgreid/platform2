// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;
use std::fmt;
use std::io::{self, Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use std::vec::Vec;

use rusb::{Direction, GlobalContext, Registration, TransferType, UsbContext};
use sync::{Condvar, Mutex};
use sys_util::{debug, error, info, EventFd};

const USB_TRANSFER_TIMEOUT: Duration = Duration::from_secs(30);

#[derive(Debug)]
pub enum Error {
    ClaimInterface(rusb::Error),
    DetachDrivers(rusb::Error),
    DeviceList(rusb::Error),
    OpenDevice(rusb::Error),
    ReadConfigDescriptor(rusb::Error),
    ReadDeviceDescriptor(rusb::Error),
    RegisterCallback(rusb::Error),
    SetActiveConfig(rusb::Error),
    SetAlternateSetting(rusb::Error),
    NoDevice,
    NoFreeInterface,
    NotIppUsb,
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            ClaimInterface(err) => write!(f, "Failed to claim interface: {}", err),
            DetachDrivers(err) => write!(f, "Failed to detach kernel drivers: {}", err),
            DeviceList(err) => write!(f, "Failed to read device list: {}", err),
            OpenDevice(err) => write!(f, "Failed to open device: {}", err),
            ReadConfigDescriptor(err) => write!(f, "Failed to read config descriptor: {}", err),
            ReadDeviceDescriptor(err) => write!(f, "Failed to read device descriptor: {}", err),
            RegisterCallback(err) => write!(f, "Failed to register for hotplug callback: {}", err),
            SetActiveConfig(err) => write!(f, "Failed to set active config: {}", err),
            SetAlternateSetting(err) => write!(f, "Failed to set alternate setting: {}", err),
            NoDevice => write!(f, "No valid IPP USB device found."),
            NoFreeInterface => write!(f, "There is no free IPP USB interface to claim."),
            NotIppUsb => write!(f, "The specified device is not an IPP USB device."),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

fn is_ippusb_interface(descriptor: &rusb::InterfaceDescriptor) -> bool {
    descriptor.class_code() == 0x07
        && descriptor.sub_class_code() == 0x01
        && descriptor.protocol_code() == 0x04
}

/// The information for an interface descriptor that supports IPPUSB.
/// Bulk transfers can be read/written to the in/out endpoints, respectively.
#[derive(Copy, Clone)]
struct IppusbDescriptor {
    interface_number: u8,
    alternate_setting: u8,
    in_endpoint: u8,
    out_endpoint: u8,
}

/// The configuration and descriptors that support IPPUSB for a USB device.
///  A valid IppusbDevice will have at least two interfaces.
struct IppusbDevice {
    config: u8,
    interfaces: Vec<IppusbDescriptor>,
}

/// Given a libusb Device, searches through the device's configurations to see if there is a
/// particular configuration that supports IPP over USB.  If such a configuration is found, returns
/// an IppusbDevice struct, which specifies the configuration as well as the IPPUSB interfaces
/// within that configuration.
///
/// If the given device does not support IPP over USB, returns None.
///
/// A device is considered to support IPP over USB if it has a configuration with at least two
/// IPPUSB interfaces.
///
/// An interface is considered an IPPUSB interface if it has the proper class, sub-class, and
/// protocol, and if it has a bulk-in and bulk-out endpoint.
fn read_ippusb_device_info<T: UsbContext>(
    device: &rusb::Device<T>,
) -> Result<Option<IppusbDevice>> {
    let desc = device
        .device_descriptor()
        .map_err(Error::ReadDeviceDescriptor)?;
    for i in 0..desc.num_configurations() {
        let config = device
            .config_descriptor(i)
            .map_err(Error::ReadConfigDescriptor)?;

        let mut interfaces = Vec::new();
        for interface in config.interfaces() {
            'alternates: for alternate in interface.descriptors() {
                if !is_ippusb_interface(&alternate) {
                    continue;
                }
                info!(
                    "Device {}:{} - Found IPPUSB interface. config {}, interface {}, alternate {}",
                    device.bus_number(),
                    device.address(),
                    config.number(),
                    interface.number(),
                    alternate.setting_number()
                );

                // Find the bulk in and out endpoints for this interface.
                let mut in_endpoint: Option<u8> = None;
                let mut out_endpoint: Option<u8> = None;
                for endpoint in alternate.endpoint_descriptors() {
                    match (endpoint.direction(), endpoint.transfer_type()) {
                        (Direction::In, TransferType::Bulk) => {
                            in_endpoint.get_or_insert(endpoint.address());
                        }
                        (Direction::Out, TransferType::Bulk) => {
                            out_endpoint.get_or_insert(endpoint.address());
                        }
                        _ => {}
                    };

                    if in_endpoint.is_some() && out_endpoint.is_some() {
                        break;
                    }
                }

                if let (Some(in_endpoint), Some(out_endpoint)) = (in_endpoint, out_endpoint) {
                    interfaces.push(IppusbDescriptor {
                        interface_number: interface.number(),
                        alternate_setting: alternate.setting_number(),
                        in_endpoint,
                        out_endpoint,
                    });
                    // We must consider at most one alternate setting when detecting IPPUSB
                    // interfaces.
                    break 'alternates;
                }
            }
        }

        // A device must have at least two IPPUSB interfaces in order to be considered an IPPUSB device.
        if interfaces.len() >= 2 {
            return Ok(Some(IppusbDevice {
                config: config.number(),
                interfaces,
            }));
        }
    }

    Ok(None)
}

struct ClaimedInterface {
    handle: rusb::DeviceHandle<GlobalContext>,
    descriptor: IppusbDescriptor,
}

/// InterfaceManager is responsible for managing a pool of claimed USB interfaces.
/// At construction, it is provided with a set of interfaces, and then clients
/// can use its member functions in order to request and free interfaces.
///
/// If no interfaces are currently available, requesting an interface will block
/// until an interface is freed by another thread.
#[derive(Clone)]
pub struct InterfaceManager {
    interface_available: Arc<Condvar>,
    free_interfaces: Arc<Mutex<VecDeque<ClaimedInterface>>>,
}

impl InterfaceManager {
    fn new(interfaces: Vec<ClaimedInterface>) -> Self {
        let deque: VecDeque<ClaimedInterface> = interfaces.into();
        Self {
            interface_available: Arc::new(Condvar::new()),
            free_interfaces: Arc::new(Mutex::new(deque)),
        }
    }

    /// Claim an interface from the pool of interfaces.
    /// Will block until an interface is available.
    fn request_interface(&mut self) -> ClaimedInterface {
        let mut free_interfaces = self.free_interfaces.lock();
        loop {
            if let Some(interface) = free_interfaces.pop_front() {
                debug!(
                    "* Claimed interface {}",
                    interface.descriptor.interface_number
                );
                return interface;
            }

            free_interfaces = self.interface_available.wait(free_interfaces);
        }
    }

    /// Return an interface to the pool of interfaces.
    fn free_interface(&mut self, interface: ClaimedInterface) {
        debug!(
            "* Released interface {}",
            interface.descriptor.interface_number
        );
        let mut free_interfaces = self.free_interfaces.lock();
        free_interfaces.push_back(interface);
        self.interface_available.notify_one();
    }
}

pub struct UnplugDetector {
    registration: Registration,
    event_thread_run: Arc<AtomicBool>,
    // This is always Some until the destructor runs.
    event_thread: Option<std::thread::JoinHandle<()>>,
}

impl UnplugDetector {
    pub fn new(
        device: rusb::Device<GlobalContext>,
        shutdown_fd: EventFd,
        shutdown: &'static AtomicBool,
    ) -> Result<Self> {
        let handler = CallbackHandler::new(device, shutdown_fd, shutdown);
        let context = GlobalContext::default();
        let registration = context
            .register_callback(None, None, None, Box::new(handler))
            .map_err(Error::RegisterCallback)?;

        // Spawn thread to handle triggering the plug/unplug events.
        // While this is technically busy looping, the thread wakes up
        // only once every 60 seconds unless an event is detected.
        // When the callback is unregistered in Drop, an unplug event will
        // be triggered so we will wake up immediately.
        let run = Arc::new(AtomicBool::new(true));
        let thread_run = run.clone();
        let event_thread = std::thread::spawn(move || {
            while thread_run.load(Ordering::Relaxed) {
                let result = context.handle_events(None);
                if let Err(e) = result {
                    error!("Failed to handle libusb events: {}", e);
                }
            }
            info!("Shutting down libusb event thread.");
        });

        Ok(Self {
            registration,
            event_thread_run: run,
            event_thread: Some(event_thread),
        })
    }
}

impl Drop for UnplugDetector {
    fn drop(&mut self) {
        self.event_thread_run.store(false, Ordering::Relaxed);
        let context = GlobalContext::default();
        context.unregister_callback(self.registration);

        // Calling unregister_callback wakes the event thread, so this should complete quickly.
        // Unwrap is safe because event_thread only becomes None at drop.
        let _ = self.event_thread.take().unwrap().join();
    }
}

struct CallbackHandler {
    device: rusb::Device<GlobalContext>,
    shutdown_fd: EventFd,
    shutdown: &'static AtomicBool,
}

impl CallbackHandler {
    fn new(
        device: rusb::Device<GlobalContext>,
        shutdown_fd: EventFd,
        shutdown: &'static AtomicBool,
    ) -> Self {
        Self {
            device,
            shutdown_fd,
            shutdown,
        }
    }
}

impl rusb::Hotplug<GlobalContext> for CallbackHandler {
    fn device_arrived(&mut self, _device: rusb::Device<GlobalContext>) {
        // Do nothing.
    }

    fn device_left(&mut self, device: rusb::Device<GlobalContext>) {
        if device == self.device {
            info!("Device was unplugged, shutting down");
            self.shutdown.store(true, Ordering::Relaxed);
            if let Err(e) = self.shutdown_fd.write(1) {
                error!("Failed to trigger shutdown: {}", e);
            }
        }
    }
}

/// A UsbConnector represents an active connection to an IPPUSB device.
/// Users can temporarily request a UsbConnection from the UsbConnector using
/// get_connection(), and use that UsbConnection to perform I/O to the device.
#[derive(Clone)]
pub struct UsbConnector {
    handle: Arc<rusb::DeviceHandle<GlobalContext>>,
    manager: InterfaceManager,
}

impl UsbConnector {
    pub fn new(bus_device: Option<(u8, u8)>) -> Result<UsbConnector> {
        let device_list = rusb::DeviceList::new().map_err(Error::DeviceList)?;

        let (device, info) = match bus_device {
            Some((bus, address)) => {
                let device = device_list
                    .iter()
                    .find(|d| d.bus_number() == bus && d.address() == address)
                    .ok_or(Error::NoDevice)?;

                let info = read_ippusb_device_info(&device)?.ok_or(Error::NotIppUsb)?;
                (device, info)
            }
            None => {
                let mut selected_device: Option<(rusb::Device<GlobalContext>, IppusbDevice)> = None;
                for device in device_list.iter() {
                    if let Some(info) = read_ippusb_device_info(&device)? {
                        selected_device = Some((device, info));
                        break;
                    }
                }
                selected_device.ok_or(Error::NoDevice)?
            }
        };

        info!(
            "Selected device {}:{}",
            device.bus_number(),
            device.address()
        );
        let mut handle = device.open().map_err(Error::OpenDevice)?;
        handle
            .set_auto_detach_kernel_driver(true)
            .map_err(Error::DetachDrivers)?;

        // Detach any outstanding kernel drivers for the current config.
        let config = device
            .active_config_descriptor()
            .map_err(Error::ReadConfigDescriptor)?;

        for interface in config.interfaces() {
            match handle.detach_kernel_driver(interface.number()) {
                Err(e) if e != rusb::Error::NotFound => return Err(Error::DetachDrivers(e)),
                _ => {}
            }
        }

        handle
            .set_active_configuration(info.config)
            .map_err(Error::SetActiveConfig)?;

        // Open the IPPUSB interfaces.
        let mut connections = Vec::new();
        for descriptor in info.interfaces {
            let mut interface_handle = device.open().map_err(Error::OpenDevice)?;
            interface_handle
                .claim_interface(descriptor.interface_number)
                .map_err(Error::ClaimInterface)?;
            interface_handle
                .set_alternate_setting(descriptor.interface_number, descriptor.alternate_setting)
                .map_err(Error::SetAlternateSetting)?;
            connections.push(ClaimedInterface {
                handle: interface_handle,
                descriptor,
            });
        }

        Ok(UsbConnector {
            handle: Arc::new(handle),
            manager: InterfaceManager::new(connections),
        })
    }

    pub fn device(&self) -> rusb::Device<GlobalContext> {
        self.handle.device()
    }

    pub fn get_connection(&mut self) -> UsbConnection {
        let interface = self.manager.request_interface();
        UsbConnection::new(self.manager.clone(), interface)
    }
}

/// A struct representing a claimed IPPUSB interface. The owner of this struct
/// can communicate with the IPPUSB device via the Read and Write.
pub struct UsbConnection {
    manager: InterfaceManager,
    // `interface` is never None until the UsbConnection is dropped, at which point the
    // ClaimedInterface is returned to the pool of connections in InterfaceManager.
    interface: Option<ClaimedInterface>,
}

impl UsbConnection {
    fn new(manager: InterfaceManager, interface: ClaimedInterface) -> Self {
        Self {
            manager,
            interface: Some(interface),
        }
    }
}

impl Drop for UsbConnection {
    fn drop(&mut self) {
        // Unwrap because interface only becomes None at drop.
        let interface = self.interface.take().unwrap();
        self.manager.free_interface(interface);
    }
}

fn to_io_error(err: rusb::Error) -> io::Error {
    let kind = match err {
        rusb::Error::InvalidParam => io::ErrorKind::InvalidInput,
        rusb::Error::NotFound => io::ErrorKind::NotFound,
        rusb::Error::Timeout => io::ErrorKind::TimedOut,
        rusb::Error::Pipe => io::ErrorKind::BrokenPipe,
        rusb::Error::Interrupted => io::ErrorKind::Interrupted,
        _ => io::ErrorKind::Other,
    };
    io::Error::new(kind, err)
}

impl Write for &UsbConnection {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        // Unwrap because interface only becomes None at drop.
        let interface = self.interface.as_ref().unwrap();
        let endpoint = interface.descriptor.out_endpoint;
        interface
            .handle
            .write_bulk(endpoint, buf, USB_TRANSFER_TIMEOUT)
            .map_err(to_io_error)
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl Read for &UsbConnection {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        // Unwrap because interface only becomes None at drop.
        let interface = self.interface.as_ref().unwrap();
        let endpoint = interface.descriptor.in_endpoint;
        interface
            .handle
            .read_bulk(endpoint, buf, USB_TRANSFER_TIMEOUT)
            .map_err(to_io_error)
    }
}
