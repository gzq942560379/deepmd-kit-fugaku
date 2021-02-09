#include <stdexcept>
#include <cstring>
#include "prod_force.h"

inline void
make_index_range (
    int & idx_start,
    int & idx_end,
    const int & nei_idx, 
    const int & nnei) 
{
  if (nei_idx < nnei) {
    idx_start = nei_idx * 4;
    idx_end   = nei_idx * 4 + 4;
  }
  else {
    throw std::runtime_error("should no reach here");
  }
}


template<typename FPTYPE>
void prod_force_a_cpu(
    FPTYPE * force, 
    const FPTYPE * net_deriv, 
    const FPTYPE * env_deriv, 
    const int * nlist, 
    const int nloc, 
    const int nall, 
    const int nnei) 
{
  const int ndescrpt = 4 * nnei;

  memset(force, 0.0, sizeof(FPTYPE) * nall * 3);
  // compute force of a frame
  for (int i_idx = 0; i_idx < nloc; ++i_idx) {
    // deriv wrt center atom
    for (int aa = 0; aa < ndescrpt; ++aa) {
      force[i_idx * 3 + 0] -= net_deriv[i_idx * ndescrpt + aa] * env_deriv[i_idx * ndescrpt * 3 + aa * 3 + 0];
      force[i_idx * 3 + 1] -= net_deriv[i_idx * ndescrpt + aa] * env_deriv[i_idx * ndescrpt * 3 + aa * 3 + 1];
      force[i_idx * 3 + 2] -= net_deriv[i_idx * ndescrpt + aa] * env_deriv[i_idx * ndescrpt * 3 + aa * 3 + 2];
    }
    // deriv wrt neighbors
    for (int jj = 0; jj < nnei; ++jj) {
      int j_idx = nlist[i_idx * nnei + jj];
      if (j_idx < 0) continue;
      int aa_start, aa_end;
      make_index_range (aa_start, aa_end, jj, nnei);
      for (int aa = aa_start; aa < aa_end; ++aa) {
	force[j_idx * 3 + 0] += net_deriv[i_idx * ndescrpt + aa] * env_deriv[i_idx * ndescrpt * 3 + aa * 3 + 0];
	force[j_idx * 3 + 1] += net_deriv[i_idx * ndescrpt + aa] * env_deriv[i_idx * ndescrpt * 3 + aa * 3 + 1];
	force[j_idx * 3 + 2] += net_deriv[i_idx * ndescrpt + aa] * env_deriv[i_idx * ndescrpt * 3 + aa * 3 + 2];
      }
    }
  }
}



template
void prod_force_a_cpu<double>(
    double * force, 
    const double * net_deriv, 
    const double * env_deriv, 
    const int * nlist, 
    const int nloc, 
    const int nall, 
    const int nnei);

template
void prod_force_a_cpu<float>(
    float * force, 
    const float * net_deriv, 
    const float * env_deriv, 
    const int * nlist, 
    const int nloc, 
    const int nall, 
    const int nnei);
