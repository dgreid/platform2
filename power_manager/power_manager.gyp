{
  'target_defaults': {
    'variables': {
      'deps': [
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libudev',
        # system_api depends on protobuf (or protobuf-lite). It must appear
        # before protobuf here or the linker flags won't be in the right
        # order.
        'system_api',
        'protobuf-lite',
      ],
    },
    'defines': [
      'USE_BUFFET=<(USE_buffet)',
    ],
  },
  'targets': [
    {
      'target_name': 'libutil',
      'type': 'static_library',
      'variables': {
        'exported_deps': [
          'libmetrics-<(libbase_ver)',
        ],
        'deps': [
          '<@(exported_deps)',
        ],
      },
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
      },
      'sources': [
        'common/clock.cc',
        'common/dbus_sender.cc',
        'common/metrics_constants.cc',
        'common/metrics_sender.cc',
        'common/power_constants.cc',
        'common/prefs.cc',
        'common/util.cc',
      ],
    },
    {
      'target_name': 'libsystem',
      'type': 'static_library',
      'link_settings': {
        'libraries': [
          '-lrt',
        ],
      },
      'sources': [
        'powerd/system/acpi_wakeup_helper.cc',
        'powerd/system/ambient_light_sensor.cc',
        'powerd/system/async_file_reader.cc',
        'powerd/system/audio_client.cc',
        'powerd/system/dark_resume.cc',
        'powerd/system/display/display_info.cc',
        'powerd/system/display/display_power_setter.cc',
        'powerd/system/display/display_watcher.cc',
        'powerd/system/display/external_display.cc',
        'powerd/system/ec_wakeup_helper.cc',
        'powerd/system/event_device.cc',
        'powerd/system/input_watcher.cc',
        'powerd/system/internal_backlight.cc',
        'powerd/system/peripheral_battery_watcher.cc',
        'powerd/system/power_supply.cc',
        'powerd/system/rolling_average.cc',
        'powerd/system/tagged_device.cc',
        'powerd/system/udev.cc',
      ],
    },
    {
      'target_name': 'libsystem_stub',
      'type': 'static_library',
      'sources': [
        'powerd/system/acpi_wakeup_helper_stub.cc',
        'powerd/system/ambient_light_sensor_stub.cc',
        'powerd/system/backlight_stub.cc',
        'powerd/system/dark_resume_stub.cc',
        'powerd/system/display/display_power_setter_stub.cc',
        'powerd/system/display/display_watcher_stub.cc',
        'powerd/system/ec_wakeup_helper_stub.cc',
        'powerd/system/event_device_stub.cc',
        'powerd/system/input_watcher_stub.cc',
        'powerd/system/power_supply_stub.cc',
        'powerd/system/udev_stub.cc',
      ],
    },
    {
      'target_name': 'libpolicy',
      'type': 'static_library',
      'link_settings': {
         'libraries': [
           '-lm',
         ],
       },
      'sources': [
        'powerd/policy/ambient_light_handler.cc',
        'powerd/policy/external_backlight_controller.cc',
        'powerd/policy/input_controller.cc',
        'powerd/policy/internal_backlight_controller.cc',
        'powerd/policy/keyboard_backlight_controller.cc',
        'powerd/policy/state_controller.cc',
        'powerd/policy/suspend_delay_controller.cc',
        'powerd/policy/suspender.cc',
        'powerd/policy/wakeup_controller.cc',
      ],
    },
    {
      'target_name': 'libpolicy_stub',
      'type': 'static_library',
      'sources': [
        'powerd/policy/backlight_controller_observer_stub.cc',
        'powerd/policy/backlight_controller_stub.cc',
      ],
    },
    {
      'target_name': 'libpowerd',
      'type': 'static_library',
      'dependencies': [
        'libpolicy',
        'libsystem',
        'libutil',
      ],
      'variables': {
        'exported_deps': [
          'libmetrics-<(libbase_ver)',
        ],
        'deps': [
          '<@(exported_deps)',
        ],
      },
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
      },
      'sources': [
        'powerd/daemon.cc',
        'powerd/metrics_collector.cc',
      ],
      'conditions': [
        ['USE_buffet == 1', {
          'dependencies': ['buffet_command_handlers'],
        }],
      ],
    },
    {
      'target_name': 'powerd',
      'type': 'executable',
      'dependencies': ['libpowerd'],
      'sources': ['powerd/main.cc'],
    },
    {
      'target_name': 'powerd_setuid_helper',
      'type': 'executable',
      'sources': ['powerd/powerd_setuid_helper.cc'],
    },
    {
      'target_name': 'set_wifi_transmit_power',
      'type': 'executable',
      'sources': ['powerd/set_wifi_transmit_power.cc'],
      'variables': {
        'deps': [
          'libnl-genl-3.0',
        ],
      },
    },
    {
      'target_name': 'backlight_dbus_tool',
      'type': 'executable',
      'sources': ['tools/backlight_dbus_tool.cc'],
    },
    {
      'target_name': 'backlight_tool',
      'type': 'executable',
      'dependencies': [
        'libutil',
        'libsystem',
      ],
      'sources': ['tools/backlight_tool.cc'],
    },
    {
      'target_name': 'check_powerd_config',
      'type': 'executable',
      'dependencies': [
        'libutil',
      ],
      'sources': ['tools/check_powerd_config.cc'],
    },
    {
      'target_name': 'get_powerd_initial_backlight_level',
      'type': 'executable',
      'dependencies': [
        'libpolicy',
        'libsystem',
        'libsystem_stub',
        'libutil',
      ],
      'sources': ['tools/get_powerd_initial_backlight_level.cc'],
    },
    {
      'target_name': 'memory_suspend_test',
      'type': 'executable',
      'sources': ['tools/memory_suspend_test.cc'],
    },
    {
      'target_name': 'powerd_dbus_suspend',
      'type': 'executable',
      'sources': ['tools/powerd_dbus_suspend.cc'],
    },
    {
      'target_name': 'power_supply_info',
      'type': 'executable',
      'dependencies': [
        'libsystem',
        'libsystem_stub',
        'libutil',
      ],
      'sources': ['tools/power_supply_info.cc'],
    },
    {
      'target_name': 'dump_power_status',
      'type': 'executable',
      'dependencies': [
        'libsystem',
        'libsystem_stub',
        'libutil',
      ],
      'sources': ['tools/dump_power_status.cc'],
    },
    {
      'target_name': 'send_debug_power_status',
      'type': 'executable',
      'sources': ['tools/send_debug_power_status.cc'],
    },
    {
      'target_name': 'set_power_policy',
      'type': 'executable',
      'sources': ['tools/set_power_policy.cc'],
    },
    {
      'target_name': 'suspend_delay_sample',
      'type': 'executable',
      'sources': ['tools/suspend_delay_sample.cc'],
    },
  ],
  'conditions': [
    ['USE_buffet == 1', {
      'targets': [
        {
          'target_name': 'buffet_command_handlers',
          'type': 'static_library',
          'sources': [
            'powerd/buffet/command_handlers.cc',
          ],
          'actions': [
            {
              'action_name': 'generate-buffet-proxies',
              'variables': {
                'dbus_service_config': '../buffet/dbus_bindings/dbus-service-config.json',
                'proxy_output_file': 'include/buffet/dbus-proxies.h'
              },
              'sources': [
                '../buffet/dbus_bindings/org.chromium.Buffet.Command.xml',
              ],
              'includes': ['../common-mk/generate-dbus-proxies.gypi'],
            },
          ],
        },
      ]},
    ],
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'libutil_test',
          'type': 'static_library',
          'sources': [
            'common/action_recorder.cc',
            'common/dbus_sender_stub.cc',
            'common/fake_prefs.cc',
            'common/metrics_sender_stub.cc',
            'common/test_main_loop_runner.cc',
          ],
        },
        {
          'target_name': 'power_manager_util_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': [
            'libutil',
            'libutil_test',
          ],
          'sources': [
            'common/prefs_unittest.cc',
            'common/testrunner.cc',
            'common/util_unittest.cc',
          ],
        },
        {
          'target_name': 'power_manager_system_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': [
            'libsystem',
            'libsystem_stub',
            'libutil',
            'libutil_test',
          ],
          'variables': {
            'deps': [
              # For base::MockTimer.
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'common/testrunner.cc',
            'powerd/system/acpi_wakeup_helper_unittest.cc',
            'powerd/system/ambient_light_sensor_unittest.cc',
            'powerd/system/async_file_reader_unittest.cc',
            'powerd/system/dark_resume_unittest.cc',
            'powerd/system/display/display_watcher_unittest.cc',
            'powerd/system/display/external_display_unittest.cc',
            'powerd/system/input_watcher_unittest.cc',
            'powerd/system/internal_backlight_unittest.cc',
            'powerd/system/peripheral_battery_watcher_unittest.cc',
            'powerd/system/power_supply_unittest.cc',
            'powerd/system/rolling_average_unittest.cc',
            'powerd/system/tagged_device_unittest.cc',
          ],
        },
        {
          'target_name': 'power_manager_policy_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': [
            'libpolicy',
            'libpolicy_stub',
            'libsystem',
            'libsystem_stub',
            'libutil',
            'libutil_test',
          ],
          'sources': [
            'common/testrunner.cc',
            'powerd/policy/ambient_light_handler_unittest.cc',
            'powerd/policy/external_backlight_controller_unittest.cc',
            'powerd/policy/input_controller_unittest.cc',
            'powerd/policy/internal_backlight_controller_unittest.cc',
            'powerd/policy/keyboard_backlight_controller_unittest.cc',
            'powerd/policy/state_controller_unittest.cc',
            'powerd/policy/suspend_delay_controller_unittest.cc',
            'powerd/policy/suspender_unittest.cc',
            'powerd/policy/wakeup_controller_unittest.cc',
          ],
        },
        {
          'target_name': 'power_manager_daemon_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': [
            'libpolicy',
            'libpolicy_stub',
            'libpowerd',
            'libsystem',
            'libsystem_stub',
            'libutil',
            'libutil_test',
          ],
          'sources': [
            'common/testrunner.cc',
            'powerd/metrics_collector_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
