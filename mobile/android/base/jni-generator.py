# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import re

STUB_TEMPLATE = '''
typedef %(returnType)s (*%(functionName)s_t)(%(paramTypes)s);
static %(functionName)s_t f_%(functionName)s;
extern "C" NS_EXPORT %(returnType)s JNICALL
%(functionName)s(%(parameterList)s) {
    if (!f_%(functionName)s) {
        arg0->ThrowNew(arg0->FindClass("java/lang/UnsupportedOperationException"),
                       "JNI Function called before it was loaded");
        return %(returnValue)s;
    }
    %(returnKeyword)s f_%(functionName)s(%(arguments)s);
}
'''
BINDING_TEMPLATE = '  xul_dlsym("%(functionName)s", &f_%(functionName)s);\n'


class Generator:
    """
    Class to convert a javah-produced JNI stub file into stubs/bindings files
    for inclusion into mozglue.
    """
    def __init__(self, outputfile):
        self.outputfile = outputfile

    def write(self, guard, stuff):
        self.outputfile.write('#ifdef %s\n' % guard)
        self.outputfile.write(stuff)
        self.outputfile.write('#endif\n\n')

    def process(self, inputfile):
        self.outputfile.write('/* WARNING - This file is autogenerated by '
                              + 'mobile/android/base/jni-generator.py. '
                              + 'Do not edit manually! */\n')

        # this matches lines such as:
        # JNIEXPORT void JNICALL Java_org_mozilla_gecko_GeckoAppShell_onResume
        # and extracts the return type and the function name
        nameRegex = re.compile('''JNIEXPORT \s+
                                  (?P<returnType>\S+) \s+
                                  JNICALL \s+
                                  (?P<functionName>\S+)''', re.VERBOSE)

        # this matches lines such as:
        #   (JNIEnv *, jclass);
        # and extracts everything within the parens; this will be split
        # on commas to get the argument types.
        paramsRegex = re.compile('\((.*)\);')

        for line in inputfile:
            line = line.strip()

            match = re.match(nameRegex, line)
            if match:
                returnType = match.group('returnType')
                functionName = match.group('functionName')

            match = re.match(paramsRegex, line)
            if match:
                paramTypes = re.split('\s*,\s*', match.group(1))
                paramNames = ['arg%d' % i for i in range(0, len(paramTypes))]
                if returnType == 'void':
                    returnValue = ''
                elif returnType in ('jobject', 'jstring'):
                    returnValue = 'nullptr'
                elif returnType in ('jint', 'jfloat', 'jdouble', 'jlong'):
                    returnValue = '0'
                elif returnType == 'jboolean':
                    returnValue = 'false'
                else:
                    raise Exception(('Unsupported JNI return type %s found; '
                                     + 'please update mobile/android/base/'
                                     + 'jni-generator.py to handle this case!')
                                    % returnType)

                self.write('JNI_STUBS', STUB_TEMPLATE % {
                    'returnType': returnType,
                    'functionName': functionName,
                    'paramTypes': ', '.join(paramTypes),
                    'parameterList': ', '.join('%s %s' % param
                                     for param in zip(paramTypes, paramNames)),
                    'arguments': ', '.join(paramNames),
                    'returnValue': returnValue,
                    'returnKeyword': 'return' if returnType != 'void' else ''})
                self.write('JNI_BINDINGS', BINDING_TEMPLATE % {
                    'functionName': functionName})


def main():
    parser = argparse.ArgumentParser(
        description='Generate mozglue bindings for JNI functions.')
    parser.add_argument('inputfile', type=argparse.FileType('r'))
    parser.add_argument('outputfile', type=argparse.FileType('w'))
    args = parser.parse_args()
    gen = Generator(args.outputfile)
    try:
        gen.process(args.inputfile)
    finally:
        args.outputfile.close()
        args.inputfile.close()

if __name__ == '__main__':
    main()
