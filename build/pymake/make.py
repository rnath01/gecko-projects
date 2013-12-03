#!/usr/bin/env python

"""
make.py

A drop-in or mostly drop-in replacement for GNU make.
"""

import sys, os, subprocess

if __name__ == '__main__':
    make = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'mozmake.exe')
    cmd = [make] + sys.argv[1:] + ['SHELL=%s.exe' % os.environ['SHELL']]
    sys.exit(subprocess.call(cmd))
