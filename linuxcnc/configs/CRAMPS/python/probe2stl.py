#!/usr/bin/python
#
# probe2stl reads x,y,z points from stdin, and creates a triangular mesh
# opionally (-i inifile) the machine boundaries are read from the ini file (AXIS_0/1, MIN/MAX_LIMIT)
#
# units are mm - boundaries are converted from inch if needed
#
# the resultin mesh can be loaded into probekins like so:
#
# probe2stl [-i inifile] [-z boundary-z] <probe.txt | stlcorr -l
# probe2stl [-i inifile] [-z boundary-z] <probe.txt | stlvis [-b xmin,ymin,xmax,ymax]

import os,sys,math
from optparse import Option, OptionParser
import voronoi

options = [Option( '-d','--debug', action='store_true', dest='debug', help="enable debug output"),
           Option( '-i','--inifile', dest='inifile', help="add machine boundary points from ini file"),
           Option( '-z','--zbound', dest='zbound', type="float", help="add machine boundaries at this z value"),
           Option( '-r','--rbed', dest='rbed', type="string", help="radius,steps define round bed"),
          ]


class Point(object):
    def __init__(self,x,y,z):
        self.x = x
        self.y = y
        self.z = z

def stldump(file,points,facets):
    file.write("solid foo\n")
    for f in facets:
        file.write("  facet normal 0 0 0\n") # invalid normal; we dont use it
        file.write("   outer loop"+"\n")
        for j in range(3):
            file.write("    vertex %f %f %f\n" % (points[f[j]].x,points[f[j]].y,points[f[j]].z))
        file.write("   endloop\n")
        file.write("  endfacet"+"\n")
    file.write("endsolid foo\n")
    file.flush()

def readpoints():
    points = []
    for l in sys.stdin:
        l.strip()
        p = map(float,l.split()[0:3])
        if len(p) == 3:
            points.append(Point(p[0],p[1],p[2]))
    return points


def closestPoint(x,y,points):
    first = True
    closest = None
    distance = 0.0
    for p in points:
        if first:
            closest = p
            distance = 99999999.0
            first = False
        else:
            d = (x-p.x)**2 + (y-p.y)**2
            if d < distance:
                closest = p
                distance = d
    return closest

def main():
    global opts
    (progdir, progname) = os.path.split(sys.argv[0])

    usage = "usage: %prog [options] file.stl"
    parser = OptionParser(usage=usage)
    parser.disable_interspersed_args()
    parser.add_options(options)
    (opts, args) = parser.parse_args()

    points = readpoints()

    zbound = 0
    if opts.zbound:
        zbound = opts.zbound
 
    if opts.inifile:
        import emc
        inifile = emc.ini(opts.inifile)
        lu = inifile.find("TRAJ", "LINEAR_UNITS")
        if lu.lower() in ("mm", "metric"):
            units = 1.0
        else:
            units = 25.4
        xmin = float(inifile.find("AXIS_0", "MIN_LIMIT")) * units
        ymin = float(inifile.find("AXIS_1", "MIN_LIMIT")) * units
        xmax = float(inifile.find("AXIS_0", "MAX_LIMIT")) * units
        ymax = float(inifile.find("AXIS_1", "MAX_LIMIT")) * units
        points.append(Point(xmin,ymin,zbound))
        points.append(Point(xmax,ymax,zbound))
        points.append(Point(xmin,ymax,zbound))
        points.append(Point(xmax,ymin,zbound))

    if opts.rbed:
        # create a bed radius r, steps
        # use z of the nearest point
        bedPoints = []
        (r,s) = opts.rbed.split(",")
        for a in range(int(s)):
            x = float(r) * math.cos(2.0*float(a)*math.pi/(float(s)))
            y = float(r) * math.sin(2.0*float(a)*math.pi/(float(s)))
            np = closestPoint(x,y,points)
            bedPoints.append(Point(x,y,np.z))
        for p in bedPoints:
            points.append(p)

    triangles =  voronoi.computeDelaunayTriangulation(points)
    stldump(sys.stdout,points,triangles)


if __name__ == '__main__':
    main()
