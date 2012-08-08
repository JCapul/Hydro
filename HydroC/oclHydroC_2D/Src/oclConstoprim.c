/*
  A simple 2D hydro code
  (C) Romain Teyssier : CEA/IRFU           -- original F90 code
  (C) Pierre-Francois Lavallee : IDRIS      -- original F90 code
  (C) Guillaume Colin de Verdiere : CEA/DAM -- for the C version
*/
/*

  This software is governed by the CeCILL license under French law and
  abiding by the rules of distribution of free software.  You can  use, 
  modify and/ or redistribute the software under the terms of the CeCILL
  license as circulated by CEA, CNRS and INRIA at the following URL
  "http://www.cecill.info". 

  As a counterpart to the access to the source code and  rights to copy,
  modify and redistribute granted by the license, users are provided only
  with a limited warranty  and the software's author,  the holder of the
  economic rights,  and the successive licensors  have only  limited
  liability. 

  In this respect, the user's attention is drawn to the risks associated
  with loading,  using,  modifying and/or developing or reproducing the
  software by the user in light of its specific status of free software,
  that may mean  that it is complicated to manipulate,  and  that  also
  therefore means  that it is reserved for developers  and  experienced
  professionals having in-depth computer knowledge. Users are therefore
  encouraged to load and test the software's suitability as regards their
  requirements in conditions enabling the security of their systems and/or 
  data to be ensured and,  more generally, to use and operate it in the 
  same conditions as regards security. 

  The fact that you are presently reading this means that you have had
  knowledge of the CeCILL license and that you accept its terms.

*/

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

#include "parametres.h"
#include "utils.h"
#include "oclConstoprim.h"
#include "oclInit.h"
#include "ocltools.h"

void
oclConstoprim(const long n, const long Hnxyt, const long Hnvar, const double Hsmallr,
	      const int slices, const int Hnxystep, 
	      cl_mem u, cl_mem q, cl_mem e)
{

  WHERE("constoprim");

  OCLSETARG08(ker[Loop1KcuConstoprim], n, u, q, e, Hnxyt, Hsmallr, slices, Hnxystep);
  oclLaunchKernel(ker[Loop1KcuConstoprim], cqueue, Hnxyt * slices, THREADSSZ, __FILE__, __LINE__);
  if (Hnvar > IP + 1) {
    OCLSETARG07(ker[Loop2KcuConstoprim], n, u, q, Hnxyt, Hnvar, slices, Hnxystep);
    oclLaunchKernel(ker[Loop2KcuConstoprim], cqueue, Hnxyt * slices, THREADSSZ, __FILE__, __LINE__);
  }
}                               // constoprim

//EOF
