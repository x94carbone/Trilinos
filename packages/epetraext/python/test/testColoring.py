#! /usr/bin/env python

# @HEADER
# ************************************************************************
#
#           PyTrilinos.EpetraExt: Python Interface to EpetraExt
#                   Copyright (2005) Sandia Corporation
#
# Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
# license for use of this work by or on behalf of the U.S. Government.
#
# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA
# Questions? Contact Michael A. Heroux (maherou@sandia.gov)
#
# ************************************************************************
# @HEADER

# Imports.  Users importing an installed version of PyTrilinos should use the
# "from PyTrilinos import ..." syntax.  Here, the setpath module adds the build
# directory, including "PyTrilinos", to the front of the search path.  We thus
# use "import ..." for Trilinos modules.  This prevents us from accidentally
# picking up a system-installed version and ensures that we are testing the
# build module.
import sys

try:
    import setpath
    import Epetra
    import EpetraExt
except ImportError:
    from PyTrilinos import Epetra, EpetraExt
    print >>sys.stderr, "Using system-installed Epetra, EpetraExt"

from   Numeric  import *
import unittest

####################################################################

class ColoringTestCase(unittest.TestCase):
    "TestCase class for EpetraExt coloring objects"

    def setUp(self):
        self.comm = Epetra.PyComm()
        self.size = 9 * self.comm.NumProc()
        self.map  = Epetra.Map(self.size,0,self.comm)
        self.crsg = Epetra.CrsGraph(Epetra.Copy, self.map, 3)
        n         = self.size
        for lrid in range(self.crsg.NumMyRows()):
            grid = self.crsg.GRID(lrid)
            if   grid == 0  : indices = [0,1]
            elif grid == n-1: indices = [n-2,n-1]
            else            : indices = [grid-1,grid,grid+1]
            self.crsg.InsertGlobalIndices(grid,len(indices),indices)
        self.crsg.FillComplete()

    def testMapColoring(self):
        "Test EpetraExt CrsGraph-to-MapColoring transform"
        mapColoring  = EpetraExt.CrsGraph_MapColoring(False)
        colorMap     = mapColoring(self.crsg)
        #for p in range(self.comm.NumProc()):
        #    if p == self.comm.MyPID(): print colorMap
        #    self.comm.Barrier()
        numColors    = colorMap.NumColors()
        defaultColor = colorMap.DefaultColor()
        self.assertEqual(numColors   , 3)
        self.assertEqual(defaultColor, 0)
        for c in range(numColors):
            self.assert_(colorMap.NumElementsWithColor(c+1) in (3,4))

    def testColorMapIndex(self):
        "Test EpetraExt MapColoring-to-ColorMapIndex transform"
        mapColoring   = EpetraExt.CrsGraph_MapColoring(False)
        colorMap      = mapColoring(self.crsg)
        colorMapIndex = EpetraExt.CrsGraph_MapColoringIndex(colorMap)
        columns       = colorMapIndex(self.crsg)
        # No assert test here; just executing has to be enough

####################################################################

if __name__ == "__main__":

    # Create the test suite object
    suite = unittest.TestSuite()

    # Add the test cases to the test suite
    suite.addTest(unittest.makeSuite(ColoringTestCase))

    # Create a communicator
    comm = Epetra.PyComm()

    # Run the test suite
    if comm.MyPID() == 0: print >>sys.stderr, \
       "\n**************************\nTesting EpetraExt.Coloring\n**************************\n"
    verbosity = 2 * int(comm.MyPID() == 0)
    result = unittest.TextTestRunner(verbosity=verbosity).run(suite)

    # Exit with a code that indicates the total number of errors and failures
    sys.exit(len(result.errors) + len(result.failures))
