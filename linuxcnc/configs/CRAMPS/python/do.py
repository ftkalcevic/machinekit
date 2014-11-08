#!/usr/bin/python

import sys,os

os.system( "./probe2stl.py -z 6 < ../probe.txt | ./stlcorr.py --load" )
