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

#include <stdio.h>
// #include <stdlib.h>
#include <malloc.h>
// #include <unistd.h>
#include <math.h>

#ifdef HMPP
#undef HMPP
#endif

#include "parametres.h"
#include "compute_deltat.h"
#include "utils.h"
#include "perfcnt.h"
#include "equation_of_state.h"

#define DABS(x) (double) fabs((x))

static void
ComputeQEforRow(const int j,
                const double Hsmallr,
                const int Hnx,
                const int Hnxt,
                const int Hnyt,
                const int Hnxyt,
                const int Hnvar,
                const int slices, const int Hstep, double *uold, double q[Hnvar][Hstep][Hnxyt], double e[Hstep][Hnxyt]
		) {
  int i, s;

#define IHV(i, j, v)  ((i) + Hnxt * ((j) + Hnyt * (v)))

#pragma omp parallel for shared(q, e) private(s, i)
  for (s = 0; s < slices; s++) {
    for (i = 0; i < Hnx; i++) {
      double eken;
      double tmp;
      int idxuID = IHV(i + ExtraLayer, j + s, ID);
      int idxuIU = IHV(i + ExtraLayer, j + s, IU);
      int idxuIV = IHV(i + ExtraLayer, j + s, IV);
      int idxuIP = IHV(i + ExtraLayer, j + s, IP);
      q[ID][s][i] = MAX(uold[idxuID], Hsmallr);
      q[IU][s][i] = uold[idxuIU] / q[ID][s][i];
      q[IV][s][i] = uold[idxuIV] / q[ID][s][i];
      eken = half * (Square(q[IU][s][i]) + Square(q[IV][s][i]));
      tmp = uold[idxuIP] / q[ID][s][i] - eken;
      q[IP][s][i] = tmp;
      e[s][i] = tmp;
    }
  }
  { 
    int nops = slices * Hnx;
    FLOPS(5 * nops, 3 * nops, 1 * nops, 0 * nops);
  }
#undef IHV
#undef IHVW
}

static void
courantOnXY(double *cournox,
            double *cournoy,
            const int Hnx,
            const int Hnxyt,
            const int Hnvar, const int slices, const int Hstep, double c[Hstep][Hnxyt], double q[Hnvar][Hstep][Hnxyt]
	    ) 
{
#ifdef WOMP
  int s;
  // double maxValC = zero;
  double *tmpm1, *tmpm2;

  tmpm1 = (double *) malloc(slices * sizeof(double));
  tmpm2 = (double *) malloc(slices * sizeof(double));

#pragma omp parallel for shared(tmpm1, tmpm2) private(s)
  for (s = 0; s < slices; s++) {
    int i;
    tmpm1[s] = *cournox;
    tmpm2[s] = *cournoy;
    for (i = 0; i < Hnx; i++) {
      double tmp1, tmp2;
      tmp1 = c[s][i] + DABS(q[IU][s][i]);
      tmp2 = c[s][i] + DABS(q[IV][s][i]);
      tmpm1[s] = MAX(tmp1, tmpm1[s]);
      tmpm2[s] = MAX(tmp2, tmpm2[s]);
    }
  }
  { 
    int nops = (slices) * Hnx;
    FLOPS(2 * nops, 0 * nops, 2 * nops, 0 * nops);
  }

#pragma ivdep
  for (s = 0; s < slices; s++) {
    *cournox = MAX(*cournox, tmpm1[s]);
    *cournoy = MAX(*cournoy, tmpm2[s]);
  }
  { 
    int nops = (slices) * Hnx;
    FLOPS(0, 0 * nops, 2 * nops, 0 * nops);
  }

  free(tmpm1);
  free(tmpm2);
#else
  int i, s;
  double tmp1, tmp2;
  for (s = 0; s < slices; s++) {
    for (i = 0; i < Hnx; i++) {
      tmp1 = c[s][i] + DABS(q[IU][s][i]);
      tmp2 = c[s][i] + DABS(q[IV][s][i]);
      *cournox = MAX(*cournox, tmp1);
      *cournoy = MAX(*cournoy, tmp2);
    }
  }
  { 
    int nops = (slices) * Hnx;
    FLOPS(2 * nops, 0 * nops, 5 * nops, 0 * nops);
  }
#endif
#undef IHVW
}
void
compute_deltat(double *dt, const hydroparam_t H, hydrowork_t * Hw, hydrovar_t * Hv, hydrovarwork_t * Hvw) {
  double cournox, cournoy;
  int j, jend, slices, Hstep, Hmin, Hmax;
  double (*e)[H.nxyt];
  double (*c)[H.nxystep];
  double (*q)[H.nxystep][H.nxyt];
  WHERE("compute_deltat");

  //   compute time step on grid interior
  cournox = zero;
  cournoy = zero;
  Hvw->q = (double (*)) calloc(H.nvar * H.nxyt * H.nxystep, sizeof(double));
  Hw->e = (double (*))  calloc(         H.nxyt * H.nxystep, sizeof(double));
  Hw->c = (double (*))  calloc(         H.nxyt * H.nxystep, sizeof(double));

  c = (double (*)[H.nxystep]) Hw->c;
  e = (double (*)[H.nxystep]) Hw->e;
  q = (double (*)[H.nxystep][H.nxyt]) Hvw->q;

  Hstep = H.nxystep;
  Hmin = H.jmin + ExtraLayer;
  Hmax = H.jmax - ExtraLayer;
  for (j = Hmin; j < Hmax; j += Hstep) {
    jend = j + Hstep;
    if (jend >= Hmax)
      jend = Hmax;
    slices = jend - j;          // numbre of slices to compute
    ComputeQEforRow(j, H.smallr, H.nx, H.nxt, H.nyt, H.nxyt, H.nvar, slices, Hstep, Hv->uold, q, e);
    equation_of_state(0, H.nx, H.nxyt, H.nvar, H.smallc, H.gamma, slices, Hstep, e, q, c);
    courantOnXY(&cournox, &cournoy, H.nx, H.nxyt, H.nvar, slices, Hstep, c, q);
    // fprintf(stdout, "[%2d]\t%g %g %g %g\n", H.mype, cournox, cournoy, H.smallc, H.courant_factor);
  }
  Free(Hvw->q);
  Free(Hw->e);
  Free(Hw->c);
  *dt = H.courant_factor * H.dx / MAX(cournox, MAX(cournoy, H.smallc));
  FLOPS(1, 1, 2, 0);
  // fprintf(stdout, "[%2d]\t%g %g %g %g %g %g\n", H.mype, cournox, cournoy, H.smallc, H.courant_factor, H.dx, *dt);
}                               // compute_deltat

//EOF