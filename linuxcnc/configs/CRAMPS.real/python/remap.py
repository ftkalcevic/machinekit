import sys,os
from stdglue import *
from interpreter import *
from emccanon import MESSAGE

cmd = "python/load_probe_data.py"

def probe_prolog(self, **words):
    if self.task:
        os.system( "halcmd setp lineardeltaprobekins.enable-z-correct 0" )


def probe_epilog(self, **words):
    print "Loading probe data"
    #yield INTERP_EXECUTE_FINISH
    if self.task:
        os.system( cmd )

def g84(self, **words):
    print "g84"
    yield INTERP_EXECUTE_FINISH
    if self.task:
        print "machine off"
        os.system( "halcmd setp halui.machine.off 1" )


