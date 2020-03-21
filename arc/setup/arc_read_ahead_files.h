// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_ARC_READ_AHEAD_FILES_H_
#define ARC_SETUP_ARC_READ_AHEAD_FILES_H_

#include <array>

namespace arc {

// A list of files that should be fully read. The list is manually created based
// on an output of
//    localhost ~ # ureadahead --dump /opt/google/containers/android/rootfs/root
// on veyron_minnie 9957.0.0. The files in the list meet all of the following:
//  * Larger than 500KB.
//  * More than ~80% of the file is read.
//  * The mini instance does not load the file.
constexpr std::array<const char* const, 70> kImportantFilesP{{
    "ArcDocumentsUI.apk",
    "ArcIntentHelper.apk",
    "ArcSystemUI.apk",
    "Bluetooth.apk",
    "Bluetooth.vdex",
    "GoogleCameraArc.apk",
    "GoogleContactsSyncAdapterRelease.apk",
    "PrintSpooler.apk",
    "SettingsProvider.odex",
    "android.hardware.audio.effect@2.0.so",
    "android.hardware.audio.effect@4.0.so",
    "android.hardware.audio@2.0.so",
    "android.hardware.audio@4.0.so",
    "android.hardware.gnss@1.0.so",
    "android.hardware.media.omx@1.0.so",
    "arc-services.odex",
    "arc-services.vdex",
    "camera.cheets.so",
    "framework-res.apk",
    "hardware.google.media.c2@1.0.so",
    "icudt60l.dat",
    "ip6tables-restore",
    "iptables-restore",
    "libandroid_runtime.so",
    "libandroid_servers.so",
    "libarcbridge.so",
    "libart-compiler.so",
    "libart.so",
    "libaudioclient.so",
    "libaudioflinger.so",
    "libaudiopolicymanagerdefault.so",
    "libbinder.so",
    "libcameraservice.so",
    "libchrome.so",
    "libcrypto.so",
    "libdng_sdk.so",
    "libft2.so",
    "libgui.so",
    "libhidl-gen-utils.so",
    "libhidltransport.so",
    "libhwui.so",
    "libicui18n.so",
    "libicuuc.so",
    "libinputflinger.so",
    "libmedia.so",
    "libmedia_jni.so",
    "libmojo.so",
    "libpdx_default_transport.so",
    "libsqlite.so",
    "libstagefright.so",
    "libstagefright_codec2_vndk.so",
    "libstagefright_omx.so",
    "libstagefright_soft_aacenc.so",
    "libstagefright_soft_hevcdec.so",
    "libvintf.so",
    "libwayland_service.so",
    "libxml2.so",
    "netd",
    "org.apache.http.legacy.boot.odex",
    "org.apache.http.legacy.boot.vdex",
    "org.chromium.arc.mojom.odex",
    "org.chromium.arc.mojom.vdex",
    "services.art",
    "services.odex",
    "services.vdex",
    "statsd",
    "tzdata",
    "wifi-service.odex",
    "wifi-service.vdex",
    "wificond",
}};

// A list of files that should be read up to |kDefaultReadAheadSize| bytes.
constexpr std::array<const char* const, 2> kImportantExtensions{
    {".so", ".ttf"}};

}  // namespace arc

#endif  // ARC_SETUP_ARC_READ_AHEAD_FILES_H_
