#!/usr/bin/env python3
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Control file for the following tests

   rb_protection.py
"""

from __future__ import print_function

import os
import shutil
import sys


def main(argv):
  if len(argv) > 0:
    sys.exit('Test takes no args!')
  iterations = 1
  output_to_stdout = ' 2>&1 | tee '
  python_prefix = 'python3 '
  test_list = ['rb_protection']

  for test in test_list:
    logs_dir = 'logs/' + test
    if os.path.exists(logs_dir):
      shutil.rmtree(logs_dir)
    os.makedirs(logs_dir)
    for i in range(iterations):
      iteration_num = i + 1
      print('==========================================================')
      print('TEST NAME: ' + test)
      print('ITERATION ' + str(iteration_num) + ' OF ' +
            str(iterations))
      print('==========================================================')
      cmd = '{0}{1}{2}{3}{4}{5}{6}{7}{8}'.format(python_prefix, test,
                                                 '.py', output_to_stdout,
                                                 logs_dir, '/', test,
                                                 iteration_num, '.log')
      os.system(cmd)

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
