#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import with_statement, print_function

import argparse
import copy
import json
import os
import sys
import tempfile

from collections import OrderedDict
from StringIO import StringIO

import manifestparser
import mozinfo

from mozdevice import (
    devicemanager,
    devicemanagerADB,
    devicemanagerSUT,
)
from mozlog import (
    structured,
)
from mozlog.structured import (
    commandline,
)
from am_instrument_parser import (
    Parser,
)


class InstrumentationTestRunner(object):
    def __init__(self, log, dm):
        self.log = log
        self.dm = dm

    def make_am_args(self, test):
        # java_package = 'org.mozilla.gecko'
        # java_class = 'TestRawResource'
        # instrumentation_package = 'org.mozilla.gecko.browser.tests'
        # instrumentation_runner = 'com.zutubi.android.junitreport.JUnitReportTestRunner'

        am_args = []
        # am_args.extend(['adb', 'shell'])
        am_args.extend(['am', 'instrument', '-r', '-w'])
        am_args.extend(['-e', 'class', '%s.%s' % ('org.mozilla.gecko', os.path.splitext(os.path.basename(test['name']))[0])])
        am_args.extend(['-e', 'error', 'true'])
        # am_args.extend(['-e', 'class', '%s.%s' % (java_package, java_class)])
        # am_args.extend(sys.argv[1:])
        am_args.extend(['%s/%s' % (test['instrumentation-package'], test['instrumentation-runner'])])
        return am_args

    def run_am_instrument(self, args):
        output = StringIO()
        self.dm.shell(args, output)
        output.seek(0)
        out = output.read()

        upload_dir = os.environ.get('MOZ_UPLOAD_DIR', None)
        if upload_dir:
            with open(os.path.join(upload_dir, 'am_instrument_log.txt'), 'a+') as f:
                f.write(out)

        return out

    def run_one_test(self, test):
        # We must start the test logging before spawning 'am instrument' to get
        # accurate test times.
        test_name = test['name']
        self.log.test_start(test_name)

        args = self.make_am_args(test)
        output = self.run_am_instrument(args)

        # test_status does not accept status='CRASH'.  Only test_end accepts
        # 'CRASH'. As a work around, we stop printing status results when we see
        # a crash -- it's almost certainly the last result, anyway -- and we
        # crash the test as a whole.  We want to show the test_status, though,
        # to help debugging.

        test_status = 'OK'
        for subtest in Parser().tests(output):
            subtest_status = subtest['status']

            if subtest_status != 'PASS':
                test_status = 'FAIL'

            if subtest_status == 'CRASH':
                test_status = 'CRASH'
                subtest_status = 'ERROR'

            self.log.test_status(test_name,
                                 subtest=subtest['test'],
                                 status=subtest_status,
                                 stack=subtest['stack'])

            if test_status == 'CRASH':
                break

        self.log.test_end(test_name, status=test_status)
        return test_status

    def run_tests(self, tests):
        pass_count = 0
        fail_count = 0

        self.log.suite_start([test['name'] for test in tests])

        for test in tests:
            test_status = self.run_one_test(test)
            if test_status == 'OK':
                pass_count += 1
            else:
                fail_count += 1

        self.log.suite_end()

        return pass_count, fail_count

# def log_am_results(log, results, overall):
#     # '''XXX'''

#     # status_handler = structured.handlers.StatusHandler()
#     # log.add_handler(status_handler)

#     # pass_count = 0
#     # fail_count = 0


#     # for subtests in tests:
#     #     test_
#     #     for subtest in subtest:
#     #         name = result['test']

#     #         current = int(result['current'])
#     #         numtests = int(result['numtests'])

#     #         if current == 1:
#     #             log.test_start(test)
#     #             test_status = 'OK'

#     #         # For an individual test result, Android returns 0 for success, -1 for
#     #         # errors, and -2 for failures.  Errors roughly correspond to unexpected
#     #         # exceptions; failures correspond to failed assertions.
#     #         status = 'PASS'
#     #         if result['code'] == -1:
#     #             status = 'ERROR'
#     #         if result['code'] == -2:
#     #             status = 'FAIL'

#     #         # Error is more severe than failure.
#     #         if status != 'PASS':
#     #             test_status = 'ERROR'


#     #         if current == numtests:
#     #             log.test_end(test=test, status=test_status)
#     #             if test_status == 'PASS':
#     #                 pass_count += 1
#     #             else:
#     #                 fail_count += 1
#     #             print(pass_count, fail_count)

#     # suite_status = status_handler.evaluate()
#     # log.remove_handler(status_handler)

#     # # print(suite_status)
#     # # fail_count = suite_status['unexpected']
#     # # pass_count = suite_status['test_count']

#     # Mozharness-parseable summary formatting.
#     log.info('Result summary:')
#     log.info('\tINFO | Passed: %d' % pass_count)
#     log.info('\tINFO | Failed: %d' % fail_count)

#     # Future: log should just track this.  Bug 1068732.
#     # Suite is good, no failures, and we didn't crash.
#     return suite_status['status'] == 'PASS' and \
#            fail_count == 0 and \
#            overall['code'] == -1


def create_parser():
    parser = argparse.ArgumentParser('instrumentation',
                                     description='Runner for Android instrumentation tests.')

    parser.add_argument('manifest',
                        metavar='MANIFEST',
                        help='Instrumentation test manifest.')

    parser.add_argument('--suite', dest='subsuite', # XXX should be --subsuite.
                        default=None,
                        help='Subsuite to run.')

    parser.add_argument('--device-ip',
                        default=None,
                        help='Connect to device with given IP via SUT. (default: connect via ADB)')

    commandline.add_logging_group(parser)
    return parser

def get_device_manager(ip):
    if not ip:
        return devicemanagerADB.DeviceManagerADB()
    else:
        if ':' in ip:
            ip, port = ip.split(':')
        else:
            ip, port = (ip, 20701)
        return devicemanagerSUT.DeviceManagerSUT(ip, port)

def resolve_tests_from_manifest(manifest, info=None, subsuite=None, names=None):
    """Some test suites differ from the standard (xpcshell, Mochitest, etc) test
suites in that the test sources themselves are compiled and not present in the
test package.  Such tests cannot be resolved by the standard mechanism, but
instead are resolved from a particular manifest and optionally filtered by
name."""

    mp = manifestparser.TestManifest(strict=False)
    mp.read(manifest)

    if not info:
        info = {}
    options = argparse.Namespace(subsuite=subsuite) if subsuite else None
    active_tests = mp.active_tests(exists=False, options=options, **info)

    return active_tests

def main(argv):
    parser = create_parser()
    args = parser.parse_args(argv)

    log = structured.commandline.setup_logging('instrumentation',
                                               args,
                                               {'tbpl': sys.stdout})
    log.info('called with argv: {argv}'.format(argv=argv));
    log.info('called with cwd: {cwd}'.format(cwd=os.getcwd()));

    # for root, dirs, files in os.walk('.'):
    manifest_dir = os.path.dirname(os.path.abspath(args.manifest))
    for f in sorted(os.listdir(manifest_dir)):
        log.info('{f}'.format(f=f))

    # if not args.device_ip:
    #     dm._checkCmd(['install', '-r', ''])
    # log.info(os.getcwd())
    # dm.installApp(

    # enabled_tests, disabled_tests = resolve_tests_from_manifest(args.manifest, info=mozinfo.info)
    info = copy.deepcopy(mozinfo.info)
    # info['subsuite'] = 'browser'
    tests = resolve_tests_from_manifest(args.manifest, subsuite=args.subsuite, info=info)
    # for test in tests:
    #     log.info('test: {test}'.format(test=json.dumps(test, indent=2)))

    apks = set()
    for test in tests:
        apks.add(os.path.join(manifest_dir, test['instrumentation-apk']))
    apks = list(sorted(apks))

    for apk in apks:
        log.info('apk: {apk}'.format(apk=apk))

    dm = get_device_manager(args.device_ip)

    def install_apk(apk):
        log.info("Installing {apk}.".format(apk=apk))
        if not args.device_ip:
            print(dm._checkCmd(['install', '-r', apk]))
        else:
            remoteApk = os.path.join(dm.deviceRoot, os.path.basename(apk))
            try:
                log.info("Deleting {remoteApk}.".format(remoteApk=remoteApk))
                log.info(dm.removeFile(remoteApk))
                log.info("Pushing {apk} to {remoteApk}.".format(apk=apk, remoteApk=remoteApk))
                log.info(dm.pushFile(apk, remoteApk))
                log.info("Installing {remoteApk}.".format(remoteApk=remoteApk))
                log.info(dm.installApp(remoteApk))
            finally:
                try:
                    dm.removeFile(remoteApk)
                except:
                    pass # Ignore problems cleaning up after ourselves.

    for apk in apks:
        install_apk(apk)

    tester = InstrumentationTestRunner(log, dm)
    pass_count, fail_count = tester.run_tests(tests)

    # Mozharness-parseable summary formatting.
    log.info('Result summary:')
    log.info('\tPassed: %d' % pass_count)
    log.info('\tFailed: %d' % fail_count)

    return 0 if fail_count == 0 else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
