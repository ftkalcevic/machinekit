#!/usr/bin/python
import sys,os

os.system( "python/probe2stl.py -r 100,18  < /tmp/probe.txt | python/stlcorr.py --load --translate 0,0,-6.75" )
os.system( "halcmd setp lineardeltaprobekins.enable-z-correct 1" )

