// @HEADER
// ***********************************************************************
//
//                 Anasazi: Block Eigensolvers Package
//                 Copyright (2004) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ***********************************************************************
// @HEADER
//
//  This test is for the LOBPCG solver
//
#include "AnasaziConfigDefs.hpp"
#include "AnasaziTypes.hpp"

#include "AnasaziEpetraAdapter.hpp"
#include "Epetra_CrsMatrix.h"
#include "Epetra_Vector.h"

#include "AnasaziLOBPCG.hpp"

#include "AnasaziBasicEigenproblem.hpp"
#include "AnasaziBasicOutputManager.hpp"
#include "AnasaziSVQBOrthoManager.hpp"
#include "AnasaziBasicSort.hpp"
#include "AnasaziStatusTestMaxIters.hpp"
#include "Teuchos_CommandLineProcessor.hpp"

#ifdef HAVE_MPI
#include "Epetra_MpiComm.h"
#include <mpi.h>
#else
#include "Epetra_SerialComm.h"
#endif

#include "ModeLaplace1DQ1.h"

using namespace Teuchos;
using namespace Anasazi;

typedef double                              ScalarType;
typedef ScalarTraits<ScalarType>                   SCT;
typedef SCT::magnitudeType               MagnitudeType;
typedef Epetra_MultiVector                 MV;
typedef Epetra_Operator                    OP;
typedef MultiVecTraits<ScalarType,MV>     MVT;
typedef OperatorTraits<ScalarType,MV,OP>  OPT;

class get_out : public std::logic_error {
  public: get_out(const std::string &whatarg) : std::logic_error(whatarg) {}
};

void testsolver( RefCountPtr<BasicEigenproblem<ScalarType,MV,OP> > problem,
                 RefCountPtr< OutputManager<ScalarType> > printer,
                 RefCountPtr< MatOrthoManager<ScalarType,MV,OP> > ortho,
                 RefCountPtr< SortManager<ScalarType,MV,OP> > sorter,
                 ParameterList &pls)
{
  // create a status tester
  RefCountPtr< StatusTest<ScalarType,MV,OP> > tester = rcp( new StatusTestMaxIters<ScalarType,MV,OP>(1) );
  // create the solver
  RefCountPtr< LOBPCG<ScalarType,MV,OP> > solver = rcp( new LOBPCG<ScalarType,MV,OP>(problem,sorter,printer,tester,ortho,pls) );

  LOBPCGState<ScalarType,MV> state;
  // solver should be uninitialized
  state = solver->getState();
  TEST_FOR_EXCEPTION(solver->isInitialized() != false,get_out,"Solver should be un-initialized after instantiation.");  
  TEST_FOR_EXCEPTION(solver->getBlockSize() != pls.get<int>("Block Size"), get_out,"Solver block size does not match specified block size.");  
  TEST_FOR_EXCEPTION(solver->getFullOrtho() != pls.get<bool>("Full Ortho"), get_out,"Solver full ortho does not match specified state.");
  TEST_FOR_EXCEPTION(solver->getNumIters() != 0,get_out,"Number of iterations after initialization should be zero.")
  TEST_FOR_EXCEPTION(solver->hasP() != false,get_out,"Uninitialized solver should not have valid search directions.");
  TEST_FOR_EXCEPTION(&solver->getProblem() != problem.get(),get_out,"getProblem() did not return the submitted problem.");
  TEST_FOR_EXCEPTION(solver->getAuxVecs().size() != 0,get_out,"getAuxVecs() should return empty.");
  TEST_FOR_EXCEPTION(MVT::GetNumberVecs(*state.X) != solver->getBlockSize(),get_out,"blockSize() does not match allocated size for X");
  TEST_FOR_EXCEPTION(MVT::GetNumberVecs(*state.R) != solver->getBlockSize(),get_out,"blockSize() does not match allocated size for R");
  TEST_FOR_EXCEPTION(MVT::GetNumberVecs(*state.P) != solver->getBlockSize(),get_out,"blockSize() does not match allocated size for R");

  // initialize solver and perform checks
  solver->initialize();
  state = solver->getState();
  TEST_FOR_EXCEPTION(solver->isInitialized() != true,get_out,"Solver should be initialized after call to initialize().");  
  TEST_FOR_EXCEPTION(solver->getBlockSize() != pls.get<int>("Block Size"),get_out,"Solver block size does not match ParameterList.");  
  TEST_FOR_EXCEPTION(solver->getFullOrtho() != pls.get<bool>("Full Ortho"),get_out,"Solver full ortho does not match ParameterList.");
  TEST_FOR_EXCEPTION(solver->getNumIters() != 0,get_out,"Number of iterations should be zero.")
  TEST_FOR_EXCEPTION(solver->hasP() != false,get_out,"Solver should not have valid P.");
  TEST_FOR_EXCEPTION(&solver->getProblem() != problem.get(),get_out,"getProblem() did not return the submitted problem.");
  TEST_FOR_EXCEPTION(solver->getAuxVecs().size() != 0,get_out,"getAuxVecs() should return empty.");
  TEST_FOR_EXCEPTION(MVT::GetNumberVecs(*state.X) != solver->getBlockSize(),get_out,"blockSize() does not match allocated size for X");
  TEST_FOR_EXCEPTION(MVT::GetNumberVecs(*state.R) != solver->getBlockSize(),get_out,"blockSize() does not match allocated size for R");
  // finish: test getIterate(), getResidualVecs(), getEigenvalues(), getResNorms(), getRes2Norms()
  // finish: test orthonormality of X

  // call iterate(); solver should perform exactly one iteration and return
}

int main(int argc, char *argv[]) 
{

#ifdef HAVE_MPI
  // Initialize MPI
  MPI_Init(&argc,&argv);
  Epetra_MpiComm Comm(MPI_COMM_WORLD);
#else
  Epetra_SerialComm Comm;
#endif

  bool testFailed;
  bool verbose = false;

  CommandLineProcessor cmdp(false,true);
  cmdp.setOption("verbose","quiet",&verbose,"Print messages and results.");
  if (cmdp.parse(argc,argv) != CommandLineProcessor::PARSE_SUCCESSFUL) {
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return -1;
  }

  // create the output manager
  RefCountPtr< OutputManager<ScalarType> > printer = rcp( new BasicOutputManager<ScalarType>() );

  if (verbose) {
    printer->stream(Errors) << Anasazi_Version() << endl << endl;
  }

  //  Problem information
  int space_dim = 1;
  std::vector<double> brick_dim( space_dim );
  brick_dim[0] = 1.0;
  std::vector<int> elements( space_dim );
  elements[0] = 100;

  // Create problem
  RefCountPtr<ModalProblem> testCase = rcp( new ModeLaplace1DQ1(Comm, brick_dim[0], elements[0]) );
  //
  // Get the stiffness and mass matrices
  RefCountPtr<Epetra_CrsMatrix> K = rcp( const_cast<Epetra_CrsMatrix *>(testCase->getStiffness()), false );
  RefCountPtr<Epetra_CrsMatrix> M = rcp( const_cast<Epetra_CrsMatrix *>(testCase->getMass()), false );
  //
  // Create the initial vectors
  const int blockSize = 10;
  RefCountPtr<Epetra_MultiVector> ivec = rcp( new Epetra_MultiVector(K->OperatorDomainMap(), blockSize) );
  ivec->Random();
  //
  // Create eigenproblem: one standard and one generalized
  const int nev = 4;
  RefCountPtr<BasicEigenproblem<ScalarType,MV,OP> > probstd = rcp( new BasicEigenproblem<ScalarType, MV, OP>(K, ivec) );
  RefCountPtr<BasicEigenproblem<ScalarType,MV,OP> > probgen = rcp( new BasicEigenproblem<ScalarType, MV, OP>(K, M, ivec) );
  //
  // Inform the eigenproblem that the operator A is symmetric
  probstd->setHermitian(true);
  probgen->setHermitian(true);
  //
  // Set the number of eigenvalues requested
  probstd->setNEV( nev );
  probgen->setNEV( nev );
  //
  // Inform the eigenproblem that you are finishing passing it information
  if ( probstd->setProblem() != true || probgen->setProblem() != true ) {
    if (verbose) {
      printer->stream(Errors) << "Anasazi::BasicEigenproblem::SetProblem() returned with error." << endl
                              << "End Result: TEST FAILED" << endl;	
    }
#ifdef HAVE_MPI
    MPI_Finalize() ;
#endif
    return -1;
  }

  // create the orthogonalization managers: one standard and one M-based
  RefCountPtr< MatOrthoManager<ScalarType,MV,OP> > orthostd = rcp( new SVQBOrthoManager<ScalarType,MV,OP>() );
  RefCountPtr< MatOrthoManager<ScalarType,MV,OP> > orthogen = rcp( new SVQBOrthoManager<ScalarType,MV,OP>(M) );
  // create the sort manager
  RefCountPtr< SortManager<ScalarType,MV,OP> > sorter = rcp( new BasicSort<ScalarType,MV,OP>("LM") );
  // create the parameter list specifying blocksize > nev and full orthogonalization
  ParameterList pls;
  pls.set<int>("Block Size",blockSize);
  pls.set<bool>("Full Ortho",true);

  // begin testing 
  testFailed = false;

  try 
  {

    if (verbose) {
      printer->stream(Errors) << "Testing solver with standard eigenproblem..." << endl;
    }
    // test a solver on the standard eigenvalue problem
    testsolver(probstd,printer,orthostd,sorter,pls);

    if (verbose) {
      printer->stream(Errors) << "Testing solver with generalized eigenproblem..." << endl;
    }
    // test a solver on the generalized eigenvalue problem
    testsolver(probgen,printer,orthogen,sorter,pls);

  }
  catch (get_out go) {
    printer->stream(Errors) << "Test failed: " << go.what() << endl;
    testFailed = true;
  }
  catch (std::exception e) {
    printer->stream(Errors) << "Caught unexpected exception: " << e.what() << endl;
    testFailed = true;
  }

  
#ifdef HAVE_MPI
  MPI_Finalize() ;
#endif

  if (testFailed) {
    if (verbose) {
      printer->stream(Errors) << "End Result: TEST FAILED" << endl;	
    }
    return -1;
  }
  //
  // Default return value
  //
  if (verbose) {
    printer->stream(Errors) << "End Result: TEST PASSED" << endl;
  }
  return 0;

}	
