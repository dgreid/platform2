#!/usr/bin/env python
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper for running platform2 tests.

This handles the fun details like running against the right sysroot, via
qemu, bind mounts, etc...
"""

from __future__ import print_function

import argparse
import contextlib
import errno
import os
import re
import signal
import sys
import tempfile

from chromite.lib import cros_build_lib
from chromite.lib import namespaces
from chromite.lib import osutils
from chromite.lib import process_util
from chromite.lib import proctitle
from chromite.lib import qemu
from chromite.lib import retry_util
from chromite.lib import signals


# Compiled regular expressions for determining what environment variables to
# let through to the test env when we do sudo. If any character at the
# beginning of an environment variable matches one of the regular expression
# patterns (i.e. matching via re.match), the environment variable is let
# through.
ENV_PASSTHRU_REGEX_LIST = [re.compile(x) for x in (
    # Used by various sanitizers.
    '[AL]SAN_OPTIONS$',
    # Used by QEMU.
    'QEMU_',
    # Used to select profiling output location for gcov.
    'GCOV_',
)]


class Platform2Test(object):
  """Framework for running platform2 tests"""

  _BIND_MOUNT_PATHS = ('dev', 'dev/pts', 'proc', 'mnt/host/source', 'sys')

  def __init__(self, test_bin, board, host, framework,
               run_as_root, gtest_filter, user_gtest_filter,
               sysroot, test_bin_args):
    if not test_bin_args:
      test_bin_args = [test_bin]
    if not test_bin:
      test_bin = test_bin_args[0]
    self.bin = test_bin
    self.args = test_bin_args
    self.board = board
    self.host = host
    self.run_as_root = run_as_root
    (self.gtest_filter, self.user_gtest_filter) = \
        self.generateGtestFilter(gtest_filter, user_gtest_filter)

    if sysroot:
      self.sysroot = sysroot
    else:
      self.sysroot = cros_build_lib.GetSysroot(self.board)

    self.framework = framework
    if self.framework == 'auto':
      qemu_arch = qemu.Qemu.DetectArch(self.bin, self.sysroot)
      if qemu_arch is None:
        self.framework = 'ldso'
      else:
        self.framework = 'qemu'

    if self.framework == 'qemu':
      self.qemu = qemu.Qemu(self.sysroot, arch=qemu_arch)

  @classmethod
  def generateGtestSubfilter(cls, gtest_filter):
    """Split a gtest_filter down into positive and negative filters.

    Args:
      gtest_filter: A filter string as normally passed to --gtest_filter.

    Returns:
      A tuple of format (positive_filters, negative_filters).
    """

    filters = gtest_filter.split('-', 1)
    positive_filters = [x for x in filters[0].split(':') if x]
    if len(filters) > 1:
      negative_filters = [x for x in filters[1].split(':') if x]
    else:
      negative_filters = []

    return (positive_filters, negative_filters)

  @classmethod
  def generateGtestFilter(cls, filters, user_filters):
    """Merge internal gtest filters and user-supplied gtest filters.

    Returns:
      A string that can be passed to --gtest_filter.
    """

    gtest_filter = cls.generateGtestSubfilter(filters)
    user_gtest_filter = []

    if user_filters:
      filters = user_filters.split('::')[-1]
      user_gtest_filter = cls.generateGtestSubfilter(filters)

    return (gtest_filter, user_gtest_filter)

  def removeSysrootPrefix(self, path):
    """Returns the given path with any sysroot prefix removed."""
    # If the sysroot is /, then the paths are already normalized.
    if self.sysroot != '/' and path.startswith(self.sysroot):
      path = path.replace(self.sysroot, '', 1)

    return path

  @staticmethod
  def GetNonRootAccount():
    """Return details about the non-root account we want to use.

    Returns:
      A tuple of (username, uid, gid, home).
    """
    return (
        os.environ.get('SUDO_USER', 'nobody'),
        int(os.environ.get('SUDO_UID', '65534')),
        int(os.environ.get('SUDO_GID', '65534')),
        # Should we find a better home?
        '/tmp/portage',
    )

  @staticmethod
  @contextlib.contextmanager
  def LockDb(db):
    """Lock an account database.

    We use the same algorithm as shadow/user.eclass.  This way we don't race
    and corrupt things in parallel.
    """
    lock = '%s.lock' % db
    _, tmplock = tempfile.mkstemp(prefix='%s.platform.' % lock)

    # First try forever to grab the lock.
    retry = lambda e: e.errno == errno.EEXIST
    # Retry quickly at first, but slow down over time.
    try:
      retry_util.GenericRetry(retry, 60, os.link, tmplock, lock, sleep=0.1)
    except Exception:
      print('error: could not grab lock %s' % lock)
      raise

    # Yield while holding the lock, but try to clean it no matter what.
    try:
      os.unlink(tmplock)
      yield lock
    finally:
      os.unlink(lock)

  def SetupUser(self):
    """Propogate the user name<->id mapping from outside the chroot.

    Some unittests use getpwnam($USER), as does bash.  If the account
    is not registered in the sysroot, they get back errors.
    """
    MAGIC_GECOS = 'Added by your friendly platform test helper; do not modify'
    # This is kept in sync with what sdk_lib/make_chroot.sh generates.
    SDK_GECOS = 'ChromeOS Developer'

    user, uid, gid, home = self.GetNonRootAccount()
    if user == 'nobody':
      return

    passwd_db = os.path.join(self.sysroot, 'etc', 'passwd')
    with self.LockDb(passwd_db):
      data = osutils.ReadFile(passwd_db)
      accts = data.splitlines()
      for acct in accts:
        passwd = acct.split(':')
        if passwd[0] == user:
          # Did the sdk make this account?
          if passwd[4] == SDK_GECOS:
            # Don't modify it (see below) since we didn't create it.
            return

          # Did we make this account?
          if passwd[4] != MAGIC_GECOS:
            raise RuntimeError('your passwd db (%s) has unmanaged acct %s' %
                               (passwd_db, user))

          # Maybe we should see if it needs to be updated?  Like if they
          # changed UIDs?  But we don't really check that elsewhere ...
          return

      acct = '%(name)s:x:%(uid)s:%(gid)s:%(gecos)s:%(homedir)s:%(shell)s' % {
          'name': user,
          'uid': uid,
          'gid': gid,
          'gecos': MAGIC_GECOS,
          'homedir': home,
          'shell': '/bin/bash',
      }
      with open(passwd_db, 'a') as f:
        if data[-1] != '\n':
          f.write('\n')
        f.write('%s\n' % acct)

  def pre_test(self):
    """Runs pre-test environment setup.

    Sets up any required mounts and copying any required files to run tests
    (not those specific to tests) into the sysroot.
    """
    if not self.run_as_root:
      self.SetupUser()

    if self.framework == 'qemu':
      self.qemu.Install()
      self.qemu.RegisterBinfmt()

  def post_test(self):
    """Runs post-test teardown, removes mounts/files copied during pre-test."""

  def run(self):
    """Runs the test in a proper environment (e.g. qemu)."""

    # We know these pre-tests are fast (especially if they've already been run
    # once), so run them automatically for the user if they test by hand.
    self.pre_test()

    for mount in self._BIND_MOUNT_PATHS:
      path = os.path.join(self.sysroot, mount)
      osutils.SafeMakedirs(path)
      osutils.Mount('/' + mount, path, 'none', osutils.MS_BIND)

    positive_filters = self.gtest_filter[0]
    negative_filters = self.gtest_filter[1]

    if self.user_gtest_filter:
      positive_filters += self.user_gtest_filter[0]
      negative_filters += self.user_gtest_filter[1]

    filters = (':'.join(positive_filters), ':'.join(negative_filters))
    gtest_filter = '%s-%s' % filters

    cmd = self.removeSysrootPrefix(self.bin)
    argv = self.args[:]
    argv[0] = self.removeSysrootPrefix(argv[0])
    if gtest_filter != '-':
      argv.append('--gtest_filter=' + gtest_filter)

    # Some programs expect to find data files via $CWD, so doing a chroot
    # and dropping them into / would make them fail.
    cwd = self.removeSysrootPrefix(os.getcwd())

    # Fork off a child to run the test.  This way we can make tweaks to the
    # env that only affect the child (gid/uid/chroot/cwd/etc...).  We have
    # to fork anyways to run the test, so might as well do it all ourselves
    # to avoid (slow) chaining through programs like:
    #   sudo -u $SUDO_UID -g $SUDO_GID chroot $SYSROOT bash -c 'cd $CWD; $BIN'
    child = os.fork()
    if child == 0:
      print('chroot: %s' % self.sysroot)
      print('cwd: %s' % cwd)
      print('cmd: {%s} %s' % (cmd, ' '.join(map(repr, argv))))
      os.chroot(self.sysroot)
      os.chdir(cwd)
      # The TERM the user is leveraging might not exist in the sysroot.
      # Force a sane default that supports standard color sequences.
      os.environ['TERM'] = 'ansi'
      # Some progs want this like bash else they get super confused.
      os.environ['PWD'] = cwd
      if not self.run_as_root:
        _, uid, gid, home = self.GetNonRootAccount()
        os.setgid(gid)
        os.setuid(uid)
        os.environ['HOME'] = home
      sys.exit(os.execvp(cmd, argv))

    proctitle.settitle('sysroot watcher', cmd)

    # Mask SIGINT with the assumption that the child will catch & process it.
    # We'll pass that back up below.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    _, status = os.waitpid(child, 0)

    failmsg = None
    if os.WIFSIGNALED(status):
      sig = os.WTERMSIG(status)
      failmsg = 'signal %s(%i)' % (signals.StrSignal(sig), sig)
    else:
      exit_status = os.WEXITSTATUS(status)
      if exit_status:
        failmsg = 'exit code %i' % exit_status
    if failmsg:
      print('Error: %s: failed with %s' % (cmd, failmsg), file=sys.stderr)

    process_util.ExitAsStatus(status)


def _SudoCommand():
  """Get the 'sudo' command, along with all needed environment variables."""
  cmd = ['sudo']
  for key, value in os.environ.iteritems():
    for pattern in ENV_PASSTHRU_REGEX_LIST:
      if pattern.match(key):
        cmd += ['%s=%s' % (key, value)]
        break

  return cmd


def _ReExecuteIfNeeded(argv, ns_net=True, ns_pid=True):
  """Re-execute tests as root.

  We often need to do things as root, so make sure we're that.  Like chroot
  for proper library environment or do bind mounts.

  Also unshare the mount namespace so as to ensure that doing bind mounts for
  tests don't leak out to the normal chroot.  Also unshare the UTS namespace
  so changes to `hostname` do not impact the host.
  """
  # Disable the Gentoo sandbox if it's active to avoid warnings/errors.
  if os.environ.get('SANDBOX_ON') == '1':
    os.environ['SANDBOX_ON'] = '0'
    os.execvp(argv[0], argv)
  elif os.geteuid() != 0:
    # Clear the LD_PRELOAD var since it won't be usable w/sudo (and the Gentoo
    # sandbox normally sets it for us).
    os.environ.pop('LD_PRELOAD', None)
    cmd = _SudoCommand() + ['--'] + argv
    os.execvp(cmd[0], cmd)
  else:
    namespaces.SimpleUnshare(net=ns_net, pid=ns_pid)


class _ParseStringSetAction(argparse.Action):
  """Support flags that store into a set (vs a list)."""

  def __call__(self, parser, namespace, values, option_string=None):
    setattr(namespace, self.dest, set(values.split()))


def main(argv):
  actions = ['pre_test', 'post_test', 'run']

  parser = argparse.ArgumentParser()
  group = parser.add_argument_group('Namespaces')
  group.add_argument('--no-ns-net', dest='ns_net',
                     default=True, action='store_false',
                     help='Do not create a new network namespace')
  group.add_argument('--no-ns-pid', dest='ns_pid',
                     default=True, action='store_false',
                     help='Do not create a new PID namespace')

  parser.add_argument('--action', default='run',
                      choices=actions, help='action to perform')
  parser.add_argument('--bin',
                      help='test binary to run')
  parser.add_argument('--board', default=None,
                      help='board to build for')
  parser.add_argument('--sysroot', default=None,
                      help='sysroot to run tests inside')
  parser.add_argument('--framework', default='auto',
                      choices=('auto', 'ldso', 'qemu'),
                      help='framework to be used to run tests')
  parser.add_argument('--gtest_filter', default='',
                      help='args to pass to gtest/test binary')
  parser.add_argument('--host', action='store_true', default=False,
                      help='specify that we\'re testing for the host')
  parser.add_argument('--run_as_root', action='store_true',
                      help='should the test be run as root')
  parser.add_argument('--user_gtest_filter', default='',
                      help=argparse.SUPPRESS)
  parser.add_argument('cmdline', nargs='*')

  options = parser.parse_args(argv)

  if options.action == 'run' and ((not options.bin or len(options.bin) == 0)
                                  and not options.cmdline):
    raise AssertionError('You must specify a binary for the "run" action')

  if options.host and options.board:
    raise AssertionError('You must provide only one of --board or --host')
  elif not options.host and not options.board and not options.sysroot:
    raise AssertionError('You must provide --board or --host or --sysroot')

  if options.sysroot:
    # Normalize the value so we can assume certain aspects.
    options.sysroot = osutils.ExpandPath(options.sysroot)
    if not os.path.isdir(options.sysroot):
      raise AssertionError('Sysroot does not exist: %s' % options.sysroot)

  # Once we've finished sanity checking args, make sure we're root.
  _ReExecuteIfNeeded([sys.argv[0]] + argv, ns_net=options.ns_net,
                     ns_pid=options.ns_pid)

  p2test = Platform2Test(options.bin, options.board, options.host,
                         options.framework,
                         options.run_as_root, options.gtest_filter,
                         options.user_gtest_filter,
                         options.sysroot, options.cmdline)
  getattr(p2test, options.action)()


if __name__ == '__main__':
  main(sys.argv[1:])
