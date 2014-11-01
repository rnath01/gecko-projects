#!/usr/bin/env python
#
# Copyright 2008, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Module that assists in parsing the output of "am instrument" commands run on
the device."""

import re
import string


class Parser():
    def status(self, code):
        # For an individual test result, 'am' returns 0 for success, -1 for
        # errors, and -2 for failures.  Errors roughly correspond to unexpected
        # exceptions; failures correspond to failed assertions.
        # See http://androidxref.com/4.4.4_r1/xref/frameworks/base/test-runner/src/android/test/InstrumentationTestRunner.java#REPORT_VALUE_RESULT_START
        if code == -1:
            return 'ERROR'
        if code == -2:
            return 'FAIL'
        return 'PASS'

    def _transform(self, outputs):
        test = {'stack':None}
        for output in outputs:
            if isinstance(output, dict):
                if -1 != int(output['code']):
                    test['status'] = 'CRASH'
                    yield test
                test = {'stack':None}
                continue

            test['test'] = output.GetTestName().replace(':', '#')

            code = output.GetStatusCode()
            if code != 1:
                test['status'] = self.status(code)
                # -1 and -2 are error codes.  We might use positive values
                # (greater than 1) for additional information in future.
                if code < 0:
                    test['stack'] = output.GetFailureReason()

                yield test
                test = {'stack':None}
                continue

    def tests(self, file):
        if not isinstance(file, str):
            file = open(file, 'rt').read()
        return self._transform(am_instrument_outputs(file))


def am_instrument_outputs(result):
    """Given the raw output of an "am instrument" command that targets and
    InstrumentationTestRunner, return structured data.

    Args:
        result (string): Raw output of "am instrument"
    """

    re_status_code = re.compile(r'INSTRUMENTATION_STATUS_CODE: (?P<status_code>-?\d)$')

    result_block_string = ""
    for line in result.splitlines():
        result_block_string += line + '\n'

        if "INSTRUMENTATION_STATUS_CODE:" in line:
            test_result = TestResult(result_block_string)
            result_block_string = ""
            yield test_result
        if "INSTRUMENTATION_CODE:" in line:
            inst_finished_bundle = _ParseInstrumentationFinishedBundle(result_block_string)
            result_block_string = ""
            yield inst_finished_bundle


def _ParseInstrumentationFinishedBundle(result):
    """Given the raw output of "am instrument" returns a dictionary of the
    key/value pairs from the bundle passed into 
    ActivityManager.finishInstrumentation().

    Args:
        result (string): Raw output of "am instrument"

    Return:
    inst_finished_bundle (dict): Key/value pairs contained in the bundle that is
        passed into ActivityManager.finishInstrumentation(). Included in this bundle is the return
        code of the Instrumentation process, any error codes reported by the
        activity manager, and any results explicity added by the instrumentation
        code.
    """

    re_result = re.compile(r'INSTRUMENTATION_RESULT: ([^=]+)=(.*)$')
    re_code = re.compile(r'INSTRUMENTATION_CODE: (\-?\d)$')
    result_dict = {}
    key = ''
    val = ''
    last_tag = ''

    for line in result.split('\n'):
        line = line.strip(string.whitespace)
        if re_result.match(line):
            last_tag = 'INSTRUMENTATION_RESULT'
            key = re_result.search(line).group(1).strip(string.whitespace)
            if key.startswith('performance.'):
                key = key[len('performance.'):]
            val = re_result.search(line).group(2).strip(string.whitespace)
            try:
                result_dict[key] = float(val)
            except ValueError:
                result_dict[key] = val
            except TypeError:
                result_dict[key] = val
        elif re_code.match(line):
            last_tag = 'INSTRUMENTATION_CODE'
            key = 'code'
            val = re_code.search(line).group(1).strip(string.whitespace)
            result_dict[key] = val
        elif 'INSTRUMENTATION_ABORTED:' in line:
            last_tag = 'INSTRUMENTATION_ABORTED'
            key = 'INSTRUMENTATION_ABORTED'
            val = ''
            result_dict[key] = val
        elif last_tag == 'INSTRUMENTATION_RESULT':
            result_dict[key] += '\n' + line

    if not result_dict.has_key('code'):
        result_dict['code'] = '0'
        result_dict['shortMsg'] = "No result returned from instrumentation"

    return result_dict


class TestResult(object):
    """A class that contains information about a single test result."""

    def __init__(self, result_block_string):
        """
        Args:
            result_block_string (string): Is a single "block" of output. A single
            "block" would be either a "test started" status report, or a "test
            finished" status report.
        """

        self._test_name = None
        self._status_code = None
        self._failure_reason = None
        self._fields_map = {}

        re_status_code = re.search(r'INSTRUMENTATION_STATUS_CODE: '
                '(?P<status_code>1|0|-1|-2)', result_block_string)
        re_fields = re.compile(r'INSTRUMENTATION_STATUS: '
                '(?P<key>[\w.]+)=(?P<value>.*?)(?=\nINSTRUMENTATION_STATUS)', re.DOTALL)

        for field in re_fields.finditer(result_block_string):
            key, value = (field.group('key').strip(), field.group('value').strip())
            if key.startswith('performance.'):
                key = key[len('performance.'):]
            self._fields_map[key] = value
        self._fields_map.setdefault('class')
        self._fields_map.setdefault('test')

        self._test_name = '%s:%s' % (self._fields_map['class'],
                                                                 self._fields_map['test'])
        self._status_code = int(re_status_code.group('status_code'))
        self._fields_map['code'] = self._status_code
        if 'stack' in self._fields_map:
            self._failure_reason = self._fields_map['stack']

    def GetTestName(self):
        return self._test_name

    def GetStatusCode(self):
        return self._status_code

    def GetFailureReason(self):
        return self._failure_reason

    def GetResultFields(self):
        return self._fields_map
