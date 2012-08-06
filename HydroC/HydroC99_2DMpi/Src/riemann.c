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
#include "perfcnt.h"
#include "utils.h"
#include "riemann.h"

#ifdef HMPP
#undef HMPP
#include "constoprim.c"
#include "equation_of_state.c"
#include "slope.c"
#include "trace.c"
#include "qleftright.c"
#include "cmpflx.c"
#include "conservar.c"
#define HMPP
#endif

#define PRECISION 1e-6

void
Dmemset(size_t nbr, double t[nbr], double motif) {
  int i;
  for (i = 0; i < nbr; i++) {
    t[i] = motif;
  }
}


#define DABS(x) (double) fabs((x))
#ifdef HMPP
#define MAX(x,y) fmax(x,y)
#endif

/* For CAL/IL */
/* #define sqrt(x) ((double) sqrtf((float)x)) */
/* #define DABS(x) (x > 0.0 ? x : -x) */

void
riemann(int narray, const double Hsmallr, const double Hsmallc, const double Hgamma, const int Hniter_riemann, const int Hnvar, const int Hnxyt, const int slices, const int Hstep, double qleft[Hnvar][Hstep][Hnxyt], double qright[Hnvar][Hstep][Hnxyt],      //
        double qgdnv[Hnvar][Hstep][Hnxyt],      //
        int sgnm[Hstep][Hnxyt]) {
  // #define IHVW(i, v) ((i) + (v) * Hnxyt)
  int i, s;
  double smallp_ = Square(Hsmallc) / Hgamma;
  double gamma6_ = (Hgamma + one) / (two * Hgamma);
  double smallpp_ = Hsmallr * smallp_;

 FLOPS(4, 2, 0, 0);
 // __declspec(align(256)) thevariable

// Pressure, density and velocity
#pragma omp parallel for schedule(static), private(s, i), shared(qgdnv, sgnm) reduction(+:flopsAri), reduction(+:flopsSqr), reduction(+:flopsMin), reduction(+:flopsTra)
  for (s = 0; s < slices; s++) {
#pragma ivdep
    for (i = 0; i < narray; i++) {
      double smallp = smallp_;
      double gamma6 = gamma6_;
      double smallpp = smallpp_;
      double rl_i = MAX(qleft[ID][s][i], Hsmallr);
      double ul_i = qleft[IU][s][i];
      double pl_i = MAX(qleft[IP][s][i], (double) (rl_i * smallp));
      double rr_i = MAX(qright[ID][s][i], Hsmallr);
      double ur_i = qright[IU][s][i];
      double pr_i = MAX(qright[IP][s][i], (double) (rr_i * smallp));

      // Lagrangian sound speed
      double cl_i = Hgamma * pl_i * rl_i;
      double cr_i = Hgamma * pr_i * rr_i;
      // First guess

      double wl_i = sqrt(cl_i);
      double wr_i = sqrt(cr_i);
      double pstar_i = MAX(((wr_i * pl_i + wl_i * pr_i) + wl_i * wr_i * (ul_i - ur_i)) / (wl_i + wr_i), 0.0);

      // Newton-Raphson iterations to find pstar at the required accuracy
      {
        int goon = 1;
	int iter;
        for (iter = 0; iter < Hniter_riemann ; iter++) { 
	  /* replace Hniter_riemann by 10 in the above line to make
	     the for i loop vectorize. This is an ugly cheat which
	     means more code restructuring to be done. */
          if (goon) {
            double wwl, wwr;
            wwl = sqrt(cl_i * (one + gamma6 * (pstar_i - pl_i) / pl_i));
            wwr = sqrt(cr_i * (one + gamma6 * (pstar_i - pr_i) / pr_i));
            double ql = two * wwl * Square(wwl) / (Square(wwl) + cl_i);
            double qr = two * wwr * Square(wwr) / (Square(wwr) + cr_i);
            double usl = ul_i - (pstar_i - pl_i) / wwl;
            double usr = ur_i + (pstar_i - pr_i) / wwr;
            double delp_i = MAX((qr * ql / (qr + ql) * (usl - usr)), (-pstar_i));
            pstar_i = pstar_i + delp_i;
            // Convergence indicator
            double uo_i = DABS(delp_i / (pstar_i + smallpp));
            goon = uo_i > PRECISION;
	    // FLOPS(29, 10, 2, 0);
          }
        }                       // iter_riemann
      }

      if (wr_i) {               // Bug CUDA !!
        wr_i = sqrt(cr_i * (one + gamma6 * (pstar_i - pr_i) / pr_i));
        wl_i = sqrt(cl_i * (one + gamma6 * (pstar_i - pl_i) / pl_i));
      }

      double ustar_i = half * (ul_i + (pl_i - pstar_i) / wl_i + ur_i - (pr_i - pstar_i) / wr_i);

      int left = ustar_i > 0;

      double ro_i, uo_i, po_i, wo_i;

      if (left) {
        sgnm[s][i] = 1;
        ro_i = rl_i;
        uo_i = ul_i;
        po_i = pl_i;
        wo_i = wl_i;
      } else {
        sgnm[s][i] = -1;
        ro_i = rr_i;
        uo_i = ur_i;
        po_i = pr_i;
        wo_i = wr_i;
      }

      double co_i = sqrt(DABS(Hgamma * po_i / ro_i));
      co_i = MAX(Hsmallc, co_i);

      double rstar_i = ro_i / (one + ro_i * (po_i - pstar_i) / Square(wo_i));
      rstar_i = MAX(rstar_i, Hsmallr);

      double cstar_i = sqrt(DABS(Hgamma * pstar_i / rstar_i));
      cstar_i = MAX(Hsmallc, cstar_i);

      double spout_i = co_i - sgnm[s][i] * uo_i;
      double spin_i = cstar_i - sgnm[s][i] * ustar_i;
      double ushock_i = wo_i / ro_i - sgnm[s][i] * uo_i;

      if (pstar_i >= po_i) {
        spin_i = ushock_i;
        spout_i = ushock_i;
      }

      double scr_i = MAX((double) (spout_i - spin_i), (double) (Hsmallc + DABS(spout_i + spin_i)));

      double frac_i = (one + (spout_i + spin_i) / scr_i) * half;
      frac_i = MAX(zero, (double) (MIN(one, frac_i)));

      int addSpout = spout_i < zero;
      int addSpin = spin_i > zero;
      // double originalQgdnv = !addSpout & !addSpin;
      double qgdnv_ID, qgdnv_IU, qgdnv_IP;

      if (addSpout) {
        qgdnv_ID = ro_i;
        qgdnv_IU = uo_i;
        qgdnv_IP = po_i;
      } else if (addSpin) {
        qgdnv_ID = rstar_i;
        qgdnv_IU = ustar_i;
        qgdnv_IP = pstar_i;
      } else {
        qgdnv_ID = (frac_i * rstar_i + (one - frac_i) * ro_i);
        qgdnv_IU = (frac_i * ustar_i + (one - frac_i) * uo_i);
        qgdnv_IP = (frac_i * pstar_i + (one - frac_i) * po_i);
      }

      qgdnv[ID][s][i] = qgdnv_ID;
      qgdnv[IU][s][i] = qgdnv_IU;
      qgdnv[IP][s][i] = qgdnv_IP;

      // transverse velocity
      if (left) {
        qgdnv[IV][s][i] = qleft[IV][s][i];
      } else {
        qgdnv[IV][s][i] = qright[IV][s][i];
      }
    }
  }
  { 
    int nops = slices * narray;
    FLOPS(57 * nops, 17 * nops, 14 * nops, 0 * nops);
  }

  // other passive variables
  if (Hnvar > IP) {
    int invar;
    for (invar = IP + 1; invar < Hnvar; invar++) {
      for (s = 0; s < slices; s++) {
        for (i = 0; i < narray; i++) {
          int left = (sgnm[s][i] == 1);
          qgdnv[invar][s][i] = qleft[invar][s][i] * left + qright[invar][s][i] * !left;
        }
      }
    }
  }
}                               // riemann

//EOF
#ifdef NOTDEF
#define ITER_RIEMANN(iter)						\
			if (goon) {							\
	  		double wwl, wwr;						\
	  		wwl = sqrt(cl_i * (one + gamma6 * (pstar_i - pl_i) / pl_i));	\
	  		wwr = sqrt(cr_i * (one + gamma6 * (pstar_i - pr_i) / pr_i));	\
	  		double ql = two * wwl * Square(wwl) / (Square(wwl) + cl_i);	\
	  		double qr = two * wwr * Square(wwr) / (Square(wwr) + cr_i);	\
	  		double usl = ul_i - (pstar_i - pl_i) / wwl;			\
	  		double usr = ur_i + (pstar_i - pr_i) / wwr;			\
	  		double delp_i = MAX((qr * ql / (qr + ql) * (usl - usr)), (-pstar_i)); \
	  		pstar_i = pstar_i + delp_i;					\
	  		double uo_i = DABS(delp_i / (pstar_i + smallpp));		\
	  		goon = uo_i > PRECISION;					\
			}

        ITER_RIEMANN(0);
        ITER_RIEMANN(1);
        ITER_RIEMANN(2);
        ITER_RIEMANN(3);
        ITER_RIEMANN(4);
        ITER_RIEMANN(5);
        ITER_RIEMANN(6);
        ITER_RIEMANN(7);
        ITER_RIEMANN(8);
        ITER_RIEMANN(9);
#endif