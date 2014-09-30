{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'static_node_tool',
      'type': 'executable',
      'sources': ['static_node_tool.cc'],
    },
  ],
}
