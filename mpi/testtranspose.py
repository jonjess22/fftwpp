#!/usr/bin/python -u

import sys # so that we can return a value at the end.
import random # for randum number generators

import os.path

from testutils import *
import time



retval = 0

print "MPI transpose unit test"

if not os.path.isfile("transpose"):
    print "Error: transpose executable not present!"
    retval += 1
else:
    
    logfile = 'testtranspose.log' 
    print "Log in " + logfile + "\n"
    log = open(logfile, 'w')
    log.close()

    start=30
    stop=40
    Xlist = [10,9,8,7,6,5,4,3,2,1,random.randint(start,stop)]
    Ylist = [9,10,8,7,6,5,4,3,2,1,random.randint(start,stop)]
    Zlist = [1,2,3,10,random.randint(start,stop)]
    Plist = [2,3,4,5,6,7,8,9,10,11,12,1]

    tstart = time.time()
    
    timeout = 0
    ntests = 0
    nfails = 0
    for X in Xlist:
        for Y in Ylist:
            for Z in Zlist:
                for P in Plist:
                    for a in range(1,max(P,2)):
                        if(P % a) :
                            continue
                        ntests += 1
                        for A in range(0,2):
#                            print "Test", ntest, "of", ntests
                            rtest = runtest("transpose", X, Y, P, ["-tq", "-Z"+str(Z), "-a"+str(a), "-A"+str(A)], logfile, timeout)
                            if not rtest == 0:
                                nfails += 1
                
    tend = time.time()
    print "\nElapsed time (s):", tend - tstart
    print "\n", nfails, "failures out of", ntests, "tests." 

sys.exit(retval)