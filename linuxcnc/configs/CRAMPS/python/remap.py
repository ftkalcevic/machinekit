import sys,os
from stdglue import *
from interpreter import *
from emccanon import MESSAGE

cmd = "python/load_probe_data.py"

def probe_prolog(self, **words):
    #if emccannon.running:
    os.system( "halcmd setp lineardeltaprobekins.enable-z-correct 0" )
    return INTERP_OK


def probe_epilog(self, **words):
    print "Loading probe data"
    os.system( "pwd" )
    os.system( cmd )
    return INTERP_OK
