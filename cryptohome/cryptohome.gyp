{
  # Shouldn't need this, but doesn't work otherwise.
  # http://crbug.com/340086 and http://crbug.com/385186
  # Note: the unused dependencies are optimized out by the compiler.
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
        'libmetrics-<(libbase_ver)',
      ],
    },
  },
  'targets': [
    # Protobufs.
    {
      'target_name': 'cryptohome-proto-external',
      'type': 'none',
      'variables': {
        'shared_proto_path': '<(sysroot)/usr/include/chromeos/dbus/cryptohome',
        'proto_out_dir': '.',
      },
      'actions': [
        {
          'action_name': 'proto-mung',
          'inputs': [
            '<(shared_proto_path)/key.proto',
            '<(shared_proto_path)/rpc.proto',
            '<(shared_proto_path)/signed_secret.proto',
          ],
          'outputs': [
            '<(proto_out_dir)/key.proto',
            '<(proto_out_dir)/rpc.proto',
            '<(proto_out_dir)/signed_secret.proto',
          ],
          'action': [
            'sh', '-c',
            'cp <@(_inputs) <(proto_out_dir)/ && sed -i s:LITE_RUNTIME:CODE_SIZE:g <@(_outputs)'
          ],
        },
      ],
    },
    {
      'target_name': 'cryptohome-proto',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '.',
        'proto_out_dir': 'include',
      },
      'dependencies': [
        'cryptohome-proto-external',
      ],
      'sources': [
        '<(proto_in_dir)/attestation.proto',
        '<(proto_in_dir)/boot_lockbox_key.proto',
        '<(proto_in_dir)/install_attributes.proto',
        '<(proto_in_dir)/tpm_status.proto',
        '<(proto_in_dir)/vault_keyset.proto',
        'key.proto',
        'rpc.proto',
        'signed_secret.proto',
      ],
      'includes': [
        '../common-mk/protoc.gypi'
      ],
    },

    # D-Bus glib bindings.
    {
      'target_name': 'cryptohome-dbus-client',
      'type': 'none',
      'variables': {
        'dbus_glib_type': 'client',
        'dbus_glib_out_dir': 'include/bindings',
        'dbus_glib_prefix': 'cryptohome',
      },
      'sources': [
        'cryptohome.xml',
      ],
      'includes': ['../common-mk/dbus_glib.gypi'],
    },
    {
      'target_name': 'cryptohome-dbus-server',
      'type': 'none',
      'variables': {
        'dbus_glib_type': 'server',
        'dbus_glib_out_dir': 'include/bindings',
        'dbus_glib_prefix': 'cryptohome',
      },
      'sources': [
        'cryptohome.xml',
      ],
      'includes': ['../common-mk/dbus_glib.gypi'],
    },

    # Common objects.
    {
      'target_name': 'libcrostpm',
      'type': 'static_library',
      'dependencies': [
        'cryptohome-proto',
      ],
      'sources': [
        'attestation.cc',
        'boot_lockbox.cc',
        'crypto.cc',
        'cryptohome_metrics.cc',
        'cryptolib.cc',
        'install_attributes.cc',
        'lockbox.cc',
        'pkcs11_init.cc',
        'pkcs11_keystore.cc',
        'platform.cc',
        'tpm.cc',
        'tpm_init.cc',
      ],
    },
    {
      'target_name': 'libcryptohome',
      'type': 'static_library',
      'dependencies': [
        'cryptohome-dbus-server',
        'cryptohome-proto',
      ],
      'cflags': [
        # The generated dbus headers use "register".
        '-Wno-deprecated-register',
      ],
      'sources': [
        'attestation_task.cc',
        'boot_attributes.cc',
        'chaps_client_factory.cc',
        'crypto.cc',
        'cryptohome_event_source.cc',
        'dbus_transition.cc',
        'homedirs.cc',
        'interface.cc',
        'lockbox-cache.cc',
        'lockbox-cache-tpm.cc',
        'mount.cc',
        'mount_factory.cc',
        'mount_stack.cc',
        'mount_task.cc',
        'service.cc',
        'stateful_recovery.cc',
        'username_passkey.cc',
        'user_oldest_activity_timestamp_cache.cc',
        'user_session.cc',
        'vault_keyset.cc',
        'vault_keyset_factory.cc',
      ],
    },

    # Main programs.
    {
      'target_name': 'cryptohome',
      'type': 'executable',
      'dependencies': [
        'libcrostpm',
        'libcryptohome',
      ],
      'link_settings': {
        'libraries': [
          '-lchaps',
          '-lkeyutils',
          '-lpolicy-<(libbase_ver)',
          '-lpthread',
          '-lscrypt',
          '-ltspi',
          '-lvboot_host',
        ],
      },
      'variables': {
        'deps': [
          'dbus-1',
          'dbus-glib-1',
          'glib-2.0',
          'libecryptfs',
          'openssl',
          'protobuf',
        ],
      },
      'sources': [
        'cryptohome.cc',
      ],
    },
    {
      'target_name': 'cryptohome-path',
      'type': 'executable',
      'dependencies': [
        'libcrostpm',
        'libcryptohome',
      ],
      'sources': [
        'cryptohome-path.cc',
      ],
    },
    {
      'target_name': 'cryptohomed',
      'type': 'executable',
      'dependencies': [
        'libcrostpm',
        'libcryptohome',
      ],
      'link_settings': {
        'libraries': [
          '-lchaps',
          '-lkeyutils',
          '-lpolicy-<(libbase_ver)',
          '-lpthread',
          '-lscrypt',
          '-ltspi',
          '-lvboot_host',
        ],
      },
      'variables': {
        'deps': [
          'dbus-1',
          'dbus-glib-1',
          'glib-2.0',
          'libecryptfs',
          'openssl',
          'protobuf',
        ],
      },
      'sources': [
        'cryptohomed.cc',
      ],
    },
    {
      'target_name': 'lockbox-cache',
      'type': 'executable',
      'dependencies': [
        'cryptohome-proto',
        'libcrostpm',
      ],
      'link_settings': {
        'libraries': [
          '-lkeyutils',
          '-ltspi',
          '-lvboot_host',
        ],
      },
      'variables': {
        'deps': [
          'libecryptfs',
          'openssl',
        ],
      },
      'sources': [
        'lockbox.cc',
        'lockbox-cache.cc',
        'lockbox-cache-main.cc',
        'lockbox-cache-tpm.cc',
        'platform.cc',
      ],
    },
    {
      'target_name': 'mount-encrypted',
      'type': 'executable',
      'link_settings': {
        'libraries': [
          '-ltspi',
          '-lvboot_host',
        ],
      },
      'variables': {
        'deps': [
          'glib-2.0',
          'openssl',
        ],
      },
      'sources': [
        'mount-encrypted.c',
        'mount-helpers.c',
      ],
    },
    {
      'target_name': 'tpm-manager',
      'type': 'executable',
      'dependencies': [
        'libcrostpm',
      ],
      'link_settings': {
        'libraries': [
          '-lchaps',
          '-lscrypt',
          '-ltspi',
          '-lvboot_host',
        ],
      },
      'variables': {
        'deps': [
          'glib-2.0',
          'libecryptfs',
          'openssl',
          'protobuf',
        ],
      },
      'sources': [
        'tpm_manager.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'cryptohome_testrunner',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': [
            'libcrostpm',
            'libcryptohome',
          ],
          'link_settings': {
            'libraries': [
              '-lchaps',
              '-lkeyutils',
              '-lpolicy-<(libbase_ver)',
              '-lpthread',
              '-lscrypt',
              '-ltspi',
              '-lvboot_host',
            ],
          },
          'variables': {
            'deps': [
              'dbus-1',
              'dbus-glib-1',
              'glib-2.0',
              'libecryptfs',
              'openssl',
              'protobuf',
            ],
          },
          'sources': [
            'attestation_unittest.cc',
            'boot_attributes_unittest.cc',
            'boot_lockbox_unittest.cc',
            'cryptohome_event_source_unittest.cc',
            'cryptohome_testrunner.cc',
            'crypto_unittest.cc',
            'homedirs_unittest.cc',
            'install_attributes_unittest.cc',
            'lockbox_unittest.cc',
            'make_tests.cc',
            'mock_chaps_client_factory.cc',
            'mock_homedirs.cc',
            'mock_install_attributes.cc',
            'mock_keystore.cc',
            'mock_lockbox.cc',
            'mock_mount.cc',
            'mock_pkcs11_init.cc',
            'mock_platform.cc',
            'mock_service.cc',
            'mock_tpm_init.cc',
            'mock_tpm.cc',
            'mock_user_oldest_activity_timestamp_cache.cc',
            'mock_user_session.cc',
            'mock_vault_keyset.cc',
            'mount_stack_unittest.cc',
            'mount_task_unittest.cc',
            'mount_unittest.cc',
            'pkcs11_keystore_unittest.cc',
	    'platform_unittest.cc',
            # 'service_unittest.cc',
            'stateful_recovery_unittest.cc',
            'username_passkey_unittest.cc',
            'user_oldest_activity_timestamp_cache_unittest.cc',
            'user_session_unittest.cc',
            'vault_keyset_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
