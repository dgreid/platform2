{
  # Shouldn't need this, but doesn't work otherwise.
  # http://crbug.com/340086 and http://crbug.com/385186
  # Note: the unused dependencies are optimized out by the compiler.
  'target_defaults': {
    'variables': {
      'deps': [
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libcrash',
      'type': 'static_library',
      'variables': {
        'exported_deps': [
          'libbrillo-<(libbase_ver)',
          'libchrome-<(libbase_ver)',
          'libdebugd-client',
          'libpcrecpp',
          'libsession_manager-client',
        ],
        'deps': ['<@(exported_deps)'],
      },
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
      },
      'sources': [
        'chrome_collector.cc',
        'crash_collector.cc',
        'kernel_collector.cc',
        'kernel_warning_collector.cc',
        'udev_collector.cc',
        'unclean_shutdown_collector.cc',
        'user_collector.cc',
        'user_collector_base.cc',
      ],
      'conditions': [
        ['USE_cheets == 1', {
          'sources': [
            'arc_collector.cc',
          ],
        }],
      ],
    },
    {
      'target_name': 'crash_reporter',
      'type': 'executable',
      'variables': {
        'deps': [
          'dbus-1',
          'libmetrics-<(libbase_ver)',
        ],
      },
      'dependencies': [
        'libcrash',
      ],
      'defines': [
        'USE_CHEETS=<(USE_cheets)',
      ],
      'sources': [
        'crash_reporter.cc',
      ],
    },
    {
      'target_name': 'list_proxies',
      'type': 'executable',
      'variables': {
        'deps': [
          'dbus-1',
          'libbrillo-<(libbase_ver)',
          'libchrome-<(libbase_ver)',
        ],
      },
      'sources': [
        'list_proxies.cc',
      ],
      'actions': [
        {
          'action_name': 'generate-lib-cros-service-proxies',
          'variables': {
            'proxy_output_file': 'include/libcrosservice/dbus-proxies.h'
          },
          'sources': [
            './dbus_bindings/org.chromium.LibCrosService.xml',
          ],
          'includes': ['../common-mk/generate-dbus-proxies.gypi'],
        },
      ],
    },
    {
      'target_name': 'warn_collector',
      'type': 'executable',
      'variables': {
        'lexer_out_dir': 'crash-reporter',
        'deps': [
          'libmetrics-<(libbase_ver)',
        ],
      },
      'link_settings': {
        'libraries': [
          '-lfl',
        ],
      },
      'sources': [
        'warn_collector.l',
      ],
      'includes': ['../common-mk/lex.gypi'],
    },
  ],
  'conditions': [
    ['USE_cheets == 1', {
      'targets': [
        {
          'target_name': 'core_collector',
          'type': 'executable',
          'variables': {
            'USE_amd64%': 0,
            'USE_cros_i686%': 0,
            'deps': [
              'breakpad-client',
            ],
          },
          'sources': [
            'core-collector/core_collector.cc',
            'core-collector/coredump_writer.cc',
            'core-collector/coredump_writer.h',
          ],
          'conditions': [
            # This condition matches the "use_i686" helper in the "cros-i686"
            # eclass. The "amd64" check allows the "cros_i686" USE flag to be
            # enabled for an overlay inherited by non-x86 boards.
            ['USE_cros_i686 == 1 and USE_amd64 == 1', {
              # Link to libc and libstdc++ statically, because the i686 shared
              # libraries are not available on x86_64.
              'cflags!': ['-fPIE'],
              'ldflags': ['-static'],
              'ldflags!': ['-pie'],
            }],
          ],
        },
      ],
    }],
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'crash_reporter_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': ['libcrash'],
          'sources': [
            'chrome_collector_test.cc',
            'crash_collector_test.cc',
            'crash_collector_test.h',
            'crash_reporter_logs_test.cc',
            'kernel_collector_test.cc',
            'kernel_collector_test.h',
            'testrunner.cc',
            'udev_collector_test.cc',
            'unclean_shutdown_collector_test.cc',
            'user_collector_test.cc',
          ],
          'conditions': [
            ['USE_cheets == 1', {
              'sources': [
                'arc_collector_test.cc',
              ],
            }],
          ],
        },
      ],
    }],
  ],
}
