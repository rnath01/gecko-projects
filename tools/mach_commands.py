# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import unicode_literals

from mach.decorators import (
    CommandArgument,
    CommandProvider,
    Command,
)


@CommandProvider
class SearchProvider(object):
    @Command('mxr', category='misc',
        description='Search for something in MXR.')
    @CommandArgument('term', nargs='+', help='Term(s) to search for.')
    def mxr(self, term):
        import webbrowser
        term = ' '.join(term)
        uri = 'https://mxr.mozilla.org/mozilla-central/search?string=%s' % term
        webbrowser.open_new_tab(uri)

    @Command('dxr', category='misc',
        description='Search for something in DXR.')
    @CommandArgument('term', nargs='+', help='Term(s) to search for.')
    def dxr(self, term):
        import webbrowser
        term = ' '.join(term)
        uri = 'http://dxr.mozilla.org/search?tree=mozilla-central&q=%s' % term
        webbrowser.open_new_tab(uri)

    @Command('mdn', category='misc',
        description='Search for something on MDN.')
    @CommandArgument('term', nargs='+', help='Term(s) to search for.')
    def mdn(self, term):
        import webbrowser
        term = ' '.join(term)
        uri = 'https://developer.mozilla.org/search?q=%s' % term
        webbrowser.open_new_tab(uri)

    @Command('google', category='misc',
        description='Search for something on Google.')
    @CommandArgument('term', nargs='+', help='Term(s) to search for.')
    def google(self, term):
        import webbrowser
        term = ' '.join(term)
        uri = 'https://www.google.com/search?q=%s' % term
        webbrowser.open_new_tab(uri)

    @Command('search', category='misc',
        description='Search for something on the Internets. '
        'This will open 3 new browser tabs and search for the term on Google, '
        'MDN, and MXR.')
    @CommandArgument('term', nargs='+', help='Term(s) to search for.')
    def search(self, term):
        self.google(term)
        self.mdn(term)
        self.mxr(term)


@CommandProvider
class UUIDProvider(object):
    @Command('uuid', category='misc',
        description='Generate a uuid.')
    @CommandArgument('--format', '-f', choices=['idl', 'cpp'], default='idl',
                     help='Output format for the generated uuid.')
    def uuid(self, format):
        import uuid
        u = uuid.uuid4()
        if format == 'idl':
            print(u)
        else:
            u = u.hex
            print('{ 0x%s, 0x%s, 0x%s, \\' % (u[0:8], u[8:12], u[12:16]))
            pairs = tuple(map(lambda n: u[n:n+2], range(16, 32, 2)))
            print(('  { ' + '0x%s, ' * 7 + '0x%s } }') % pairs)
