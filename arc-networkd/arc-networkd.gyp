{
  'target_defaults': {
    'variables': {
      'deps': [
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libndp',
        'libshill-client',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'arc-networkd',
      'type': 'executable',
      'sources': [
        'arc_ip_config.cc',
        'main.cc',
        'manager.cc',
        'multicast_forwarder.cc',
        'multicast_socket.cc',
        'ndp_handler.cc',
        'neighbor_finder.cc',
        'router_finder.cc',
        'shill_client.cc',
      ],
    },
  ],
}
