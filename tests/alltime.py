#!/usr/bin/python

# a timing script for running multiple timing tests using timing.py

import sys, getopt
import numpy as np
from math import *
import os
from subprocess import * # for popen, running processes

def main(argv):
    usage = '''
    Usage:
    \nalltime.py
    -T <number of threads> 
    -R <ram in gigabytes> 
    -S <int> statistical choice (-1 keeps raw data). 
    -d dry run
    -D<outdir>
    -o<outfile>
    -r<implicit/explicit/pruned/fft>
    -A<quoted arg list for timed program>
    -B<quoted arg list inserted before timed program>
    '''

    dryrun=False
    R=0

    outdir = "timings"
    nthreads = 0
    #out = ""
    A = []
    B = []
    E = []
    runtype = "implicit"
    stats = 0

    try:
        opts, args = getopt.getopt(argv,"dp:T:r:R:S:o:D:g:A:B:E:")
    except getopt.GetoptError:
        print usage
        sys.exit(2)
    for opt, arg in opts:
        if opt in ("-T"):
            nthreads=int(arg)
        elif opt in ("-R"):
            R=float(arg)
        if opt in ("-S"):
            stats = int(arg)
        elif opt in ("-d"):
            dryrun=True
        elif opt in ("-D"):
            outdir = str(arg)
        elif opt in ("-g"):
            rname = str(arg)
        elif opt in ("-A"):
            A.append(str(arg))
        elif opt in ("-B"):
            B.append(str(arg))
        elif opt in ("-E"):
            E += [str(arg)]
        elif opt in ("-r"):
            runtype = str(arg)

    progs=[["cconv" , "conv", "tconv"], ["cconv2","conv2","tconv2"], ["cconv3","conv3"]]

    ab=[[6,20],[6,10],[2,6]] # problem size limits
    
    if runtype == "explicit":
        progs = [["cconv","conv"], ["cconv2","conv2"],["cconv3"]]
    if runtype == "pruned":
        progs = [["cconv2","conv2"],["cconv3"]]
        ab=[[6,10],[2,6]] # problem size limits

    print "extra args:", A
    print "environment args:", E

    i = 0
    while(i < len(progs)):
        a = ab[i][0]
        b = ab[i][1]
        for p in progs[i]:
            cmd = []
            cmd.append("./timing.py")
            cmd.append("-p" + p)
            cmd.append("-S" + str(stats))
            if R == 0.0:
                cmd.append("-a" + str(a))
                cmd.append("-b" + str(b))
            else:
                cmd.append("-R" + str(R))
            if not nthreads == 0 :
                cmd.append("-T" + str(nthreads))
            if runtype != "":
                cmd.append("-r" + runtype)
            cmd.append("-D" + outdir)
            cmd.append("-o" + p + "_" + runtype)
            iA = 0
            while iA < len(A):
                cmd.append("-A" + A[iA])
                iA += 1
            iB = 0
            while iB < len(B):
                cmd.append("-B" + B[iB])
                iB += 1
            iE = 0
            while iE < len(E):
                cmd.append("-E" + E[iE])
                iE += 1
            print " ".join(cmd)
            if not dryrun:
                p = Popen(cmd)
                p.wait() # sets the return code
                prc = p.returncode
            else:
                print " ".join(cmd)
    
            # pcmd=cmd+" -p "+p
            # print(pcmd)
            # if not dryrun:
            #     os.system(pcmd)
        i += 1 
            
if __name__ == "__main__":
    main(sys.argv[1:])
