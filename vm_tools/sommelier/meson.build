# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

project('sommelier', 'c', 'cpp')

#===============#
# Configuration #
#===============#

peer_cmd_prefix = get_option('peer_cmd_prefix')
if peer_cmd_prefix == ''
  cpu_fam = target_machine.cpu_family()
  if cpu_fam == 'x86_64'
    peer_cmd_prefix = '''/opt/google/cros-containers/lib/ld-linux-x86-64.so.2 --library-path /opt/google/cros-containers/lib --inhibit-rpath ""'''
  elif cpu_fam == 'arm' or cpu_fam == 'aarch64'
    peer_cmd_prefix = '''/opt/google/cros-containers/lib/ld-linux-armhf.so.3 --library-path /opt/google/cros-containers/lib --inhibit-rpath ""'''
  else
    assert(false, 'No default peer_cmd_prefix for architecture: ' + cpu_fam)
  endif
endif

includes = []
cpp_args = []

#===============#
# Wayland Stuff #
#===============#

wl_scanner = find_program('wayland-scanner')

wl_generators = [
  generator(
    wl_scanner,
    output: '@BASENAME@-code.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@']
  ),
  generator(
    wl_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@']
  ),
  generator(
    wl_scanner,
    output: '@BASENAME@-server-protocol.h',
    arguments: ['server-header', '@INPUT@', '@OUTPUT@']
  ),
]

wl_protocols = [
    'protocol/aura-shell.xml',
    'protocol/drm.xml',
    'protocol/gaming-input-unstable-v2.xml',
    'protocol/gtk-shell.xml',
    'protocol/keyboard-extension-unstable-v1.xml',
    'protocol/linux-dmabuf-unstable-v1.xml',
    'protocol/pointer-constraints-unstable-v1.xml',
    'protocol/relative-pointer-unstable-v1.xml',
    'protocol/text-input-unstable-v1.xml',
    'protocol/viewporter.xml',
    'protocol/xdg-shell-unstable-v6.xml',
]

wl_outs = []

foreach p : wl_protocols
  foreach g : wl_generators
    wl_outs += g.process(p)
  endforeach
endforeach

#==========#
# Perfetto #
#==========#

tracing_sources = []
tracing_dependencies = []

if get_option('tracing')
  tracing_sources = [
    'perfetto/sdk/perfetto.cc',
  ]
  tracing_dependencies = [
    dependency('threads'),
  ]
  cpp_args += '-DPERFETTO_TRACING'
  includes += include_directories('perfetto/sdk')
endif

#==========#
# Gamepad support #
#==========#

gamepad_sources = []
gamepad_dependencies = []

if get_option('gamepad')
  gamepad_sources = [
    'sommelier-gaming.cc',
  ]
  gamepad_dependencies = [
    dependency('libevdev'),
  ]
  cpp_args += '-DGAMEPAD_SUPPORT'
endif

#===========#
# Sommelier #
#===========#

executable('sommelier',
  install: true,
  sources: [
    'sommelier-compositor.cc',
    'sommelier-data-device-manager.cc',
    'sommelier-display.cc',
    'sommelier-drm.cc',
    'sommelier-gtk-shell.cc',
    'sommelier-output.cc',
    'sommelier-pointer-constraints.cc',
    'sommelier-relative-pointer-manager.cc',
    'sommelier-seat.cc',
    'sommelier-shell.cc',
    'sommelier-shm.cc',
    'sommelier-subcompositor.cc',
    'sommelier-text-input.cc',
    'sommelier-tracing.cc',
    'sommelier-viewporter.cc',
    'sommelier-xdg-shell.cc',
    'sommelier.cc',
    'virtualization/virtwl_channel.cc',
  ] + wl_outs + tracing_sources + gamepad_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gbm'),
    dependency('libdrm'),
    dependency('pixman-1'),
    dependency('wayland-client'),
    dependency('wayland-server'),
    dependency('xcb'),
    dependency('xcb-composite'),
    dependency('xcb-xfixes'),
    dependency('xkbcommon'),
  ] + tracing_dependencies + gamepad_dependencies,
  cpp_args: cpp_args + [
    '-D_GNU_SOURCE',
    '-DWL_HIDE_DEPRECATED',
    '-DXWAYLAND_PATH="' + get_option('xwayland_path') + '"',
    '-DXWAYLAND_GL_DRIVER_PATH="' + get_option('xwayland_gl_driver_path') + '"',
    '-DXWAYLAND_SHM_DRIVER="' + get_option('xwayland_shm_driver') + '"',
    '-DSHM_DRIVER="' + get_option('shm_driver') + '"',
    '-DPEER_CMD_PREFIX="' + peer_cmd_prefix + '"',
    '-DFRAME_COLOR="' + get_option('frame_color') + '"',
    '-DDARK_FRAME_COLOR="' + get_option('dark_frame_color') + '"',
  ],
  include_directories: includes,
)
