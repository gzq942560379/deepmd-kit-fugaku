#include <vector>
#include <cassert>
#include <iostream>
#include <string.h>
#include "tabulate.h"

#ifdef __ARM_FEATURE_SVE 
#include <arm_sve.h> 
#endif /* __ARM_FEATURE_SVE */

/*
    This inline function was designed to get the table info and bias value for current input xx!
    lower:      indicate the lower boundary of the first table;
    upper:      indicate the upper boundary of the first table as well as the lower boundary of the second table;
    max:        indicate the upper boundary of the second table;
    stride0:    indicate the stride of the first table;
    stride1:    indicate the stride of the second table;
    xx:         indicate the inputs value;
    table_idx:  indicate the location of table info of input value xx;
*/
template <typename FPTYPE>
static inline void locate_xx(
    const FPTYPE& lower, 
    const FPTYPE& upper,
    const FPTYPE& max, 
    const FPTYPE& stride0, 
    const FPTYPE& stride1, 
    FPTYPE& xx, 
    int& table_idx) 
{
  if (xx < lower) {
    table_idx = 0;
    xx = 0;
  }
  else if (xx < upper) {
    table_idx = (int)((xx - lower) / stride0);
    xx -= (table_idx * stride0 + lower);
  }
  else if (xx < max) {
    int first_stride = int((upper - lower) / stride0);
    table_idx = first_stride + (int)((xx - upper) / stride1);
    xx -= ((table_idx - first_stride) * stride1 + upper);
  }
  else {
    table_idx = int((upper - lower) / stride0) + (int)((max - upper) / stride1) - 1;
    xx = 0;
  }
}

template <typename FPTYPE>
static inline FPTYPE dot(
    FPTYPE a[4], 
    FPTYPE b[4]) 
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; 
}

template<typename FPTYPE>
void deepmd::tabulate_fusion_cpu_packing(
    FPTYPE * out,
    const FPTYPE * table, 
    const FPTYPE * table_info, 
    const FPTYPE * em_x, 
    const FPTYPE * em, 
    const int nloc, 
    const int nnei, 
    const int last_layer_size)
{
  memset(out, 0.0, sizeof(FPTYPE) * nloc * 4 * last_layer_size);
  const FPTYPE lower   = table_info[0];
  const FPTYPE upper   = table_info[1];
  const FPTYPE _max    = table_info[2];
  const FPTYPE stride0 = table_info[3];
  const FPTYPE stride1 = table_info[4];

  // std::cout << "(nloc,nnei,last_layer_size)" << " : " << "(" << nloc << "," << nnei << "," << last_layer_size << ")" << std::endl;

  // for every atom, execute a small manual gemm ~
  // FPTYPE * res = new FPTYPE[4 * last_layer_size];
  // #pragma omp parallel for
  for (int ii = 0; ii < nloc; ii++) {
    FPTYPE ll[4] = {0};
    FPTYPE ago = em_x[ii * nnei + nnei - 1];
    bool unloop = false; 

    FPTYPE * out0 = &out[ii * last_layer_size * 4 + 0 * last_layer_size];
    FPTYPE * out1 = &out[ii * last_layer_size * 4 + 1 * last_layer_size];
    FPTYPE * out2 = &out[ii * last_layer_size * 4 + 2 * last_layer_size];
    FPTYPE * out3 = &out[ii * last_layer_size * 4 + 3 * last_layer_size];

    for (int jj = 0; jj < nnei; jj++) { 
      ll[0] = em[ii * nnei * 4 + jj * 4 + 0];
      ll[1] = em[ii * nnei * 4 + jj * 4 + 1];
      ll[2] = em[ii * nnei * 4 + jj * 4 + 2];
      ll[3] = em[ii * nnei * 4 + jj * 4 + 3];
      FPTYPE xx = em_x[ii * nnei + jj]; 
      if (ago == xx) {
        unloop = true;
      }
      int table_idx = 0;
      locate_xx(lower, upper, _max, stride0, stride1, xx, table_idx);

      for (int kbs = 0; kbs < last_layer_size; kbs+=16){
        int kbe = kbs + 16;
        const FPTYPE* table0 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 0];
        const FPTYPE* table1 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 1];
        const FPTYPE* table2 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 2];
        const FPTYPE* table3 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 3];
        const FPTYPE* table4 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 4];
        const FPTYPE* table5 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 5];
        for (int kk = kbs; kk < kbe; kk++) {
          FPTYPE a0  = table0[kk-kbs]; 
          FPTYPE a1  = table1[kk-kbs]; 
          FPTYPE a2  = table2[kk-kbs]; 
          FPTYPE a3  = table3[kk-kbs];
          FPTYPE a4  = table4[kk-kbs];
          FPTYPE a5  = table5[kk-kbs];
          FPTYPE var = a0 + (a1 + (a2 + (a3 + (a4 + a5 * xx) * xx) * xx) * xx) * xx;
          if (unloop) {
            out0[kk] += (nnei - jj) * var * ll[0];
            out1[kk] += (nnei - jj) * var * ll[1];
            out2[kk] += (nnei - jj) * var * ll[2];
            out3[kk] += (nnei - jj) * var * ll[3];
          }
          else {
            out0[kk] += var * ll[0];
            out1[kk] += var * ll[1];
            out2[kk] += var * ll[2];
            out3[kk] += var * ll[3];
          }
        }
      }
      
      if (unloop) break;
    }
  }
}

template<typename FPTYPE>
void deepmd::tabulate_fusion_grad_cpu_packing(
    FPTYPE * dy_dem_x, 
    FPTYPE * dy_dem,
    const FPTYPE * table, 
    const FPTYPE * table_info, 
    const FPTYPE * em_x, 
    const FPTYPE * em, 
    const FPTYPE * dy, 
    const int nloc, 
    const int nnei, 
    const int last_layer_size) 
{
  memset(dy_dem_x, 0.0, sizeof(FPTYPE) * nloc * nnei);
  memset(dy_dem, 0.0, sizeof(FPTYPE) * nloc * nnei * 4);
  FPTYPE const lower   = table_info[0];
  FPTYPE const upper   = table_info[1];
  FPTYPE const _max    = table_info[2];
  FPTYPE const stride0 = table_info[3];
  FPTYPE const stride1 = table_info[4];
  // for every atom, execute a small gemm~
  // FPTYPE * res = new FPTYPE[4 * last_layer_size];
  // #pragma omp parallel for
  for (int ii = 0; ii < nloc; ii++) {
    FPTYPE ll[4];
    FPTYPE rr[4];
    FPTYPE ago = em_x[ii * nnei + nnei - 1];
    const FPTYPE* dy0 = &dy[ii * last_layer_size * 4 + 0 * last_layer_size];
    const FPTYPE* dy1 = &dy[ii * last_layer_size * 4 + 1 * last_layer_size];
    const FPTYPE* dy2 = &dy[ii * last_layer_size * 4 + 2 * last_layer_size];
    const FPTYPE* dy3 = &dy[ii * last_layer_size * 4 + 3 * last_layer_size];
    bool unloop = false;
    for (int jj = 0; jj < nnei; jj++) {
      // construct the dy/dx
      ll[0] = em[ii * nnei * 4 + jj * 4 + 0];
      ll[1] = em[ii * nnei * 4 + jj * 4 + 1];
      ll[2] = em[ii * nnei * 4 + jj * 4 + 2];
      ll[3] = em[ii * nnei * 4 + jj * 4 + 3];
      FPTYPE xx = em_x[ii * nnei + jj]; 
      if (ago == xx) {
        unloop = true;
      }
      int table_idx = 0;
      locate_xx(lower, upper, _max, stride0, stride1, xx, table_idx);
      
      FPTYPE grad = 0.0;

      for (int kbs = 0; kbs < last_layer_size; kbs += 16){
        int kbe = kbs + 16;
        const FPTYPE* table0 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 0];
        const FPTYPE* table1 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 1];
        const FPTYPE* table2 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 2];
        const FPTYPE* table3 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 3];
        const FPTYPE* table4 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 4];
        const FPTYPE* table5 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 5];
        for (int kk = kbs; kk < kbe; kk++) {
          rr[0] = dy0[kk];
          rr[1] = dy1[kk];
          rr[2] = dy2[kk];
          rr[3] = dy3[kk];
          FPTYPE a0  = table0[kk-kbs]; 
          FPTYPE a1  = table1[kk-kbs]; 
          FPTYPE a2  = table2[kk-kbs]; 
          FPTYPE a3  = table3[kk-kbs];
          FPTYPE a4  = table4[kk-kbs];
          FPTYPE a5  = table5[kk-kbs];
          FPTYPE res = a0 + (a1 + (a2 + (a3 + (a4 + a5 * xx) * xx) * xx) * xx) * xx;

          if (unloop) {
            grad += (a1 + (2 * a2 + (3 * a3 + (4 * a4 + 5 * a5 * xx) * xx) * xx) * xx) * dot(ll, rr) * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 0] += res * rr[0] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 1] += res * rr[1] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 2] += res * rr[2] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 3] += res * rr[3] * (nnei - jj);
          }
          else {
            grad += (a1 + (2 * a2 + (3 * a3 + (4 * a4 + 5 * a5 * xx) * xx) * xx) * xx) * dot(ll, rr);
            dy_dem[ii * nnei * 4 + jj * 4 + 0] += res * rr[0];
            dy_dem[ii * nnei * 4 + jj * 4 + 1] += res * rr[1];
            dy_dem[ii * nnei * 4 + jj * 4 + 2] += res * rr[2];
            dy_dem[ii * nnei * 4 + jj * 4 + 3] += res * rr[3];
          }
        }
      }

      dy_dem_x[ii * nnei + jj] = grad;
      if (unloop) break;
    }
  }
}


template void deepmd::tabulate_fusion_cpu_packing<float>(float * out, const float * table, const float * table_info, const float * em_x, const float * em, const int nloc, const int nnei, const int last_layer_size);
template void deepmd::tabulate_fusion_cpu_packing<double>(double * out, const double * table, const double * table_info, const double * em_x, const double * em, const int nloc, const int nnei, const int last_layer_size);
template void deepmd::tabulate_fusion_grad_cpu_packing<float> (float * dy_dem_x, float * dy_dem, const float * table, const float * table_info, const float * em_x, const float * em, const float * dy, const int nloc, const int nnei, const int last_layer_size); 
template void deepmd::tabulate_fusion_grad_cpu_packing<double> (double * dy_dem_x, double * dy_dem, const double * table, const double * table_info, const double * em_x, const double * em, const double * dy, const int nloc, const int nnei, const int last_layer_size);


void deepmd::tabulate_fusion_cpu_packing_sve(
    double * out,
    const double * table, 
    const double * table_info, 
    const double * em_x, 
    const double * em, 
    const int nloc, 
    const int nnei, 
    const int last_layer_size)
{
  memset(out, 0.0, sizeof(double) * nloc * 4 * last_layer_size);
  const double lower   = table_info[0];
  const double upper   = table_info[1];
  const double _max    = table_info[2];
  const double stride0 = table_info[3];
  const double stride1 = table_info[4];

  // std::cout << "(nloc,nnei,last_layer_size)" << " : " << "(" << nloc << "," << nnei << "," << last_layer_size << ")" << std::endl;

  // for every atom, execute a small manual gemm ~
  // double * res = new double[4 * last_layer_size];
  // #pragma omp parallel for
  for (int ii = 0; ii < nloc; ii++) {
    double ll[4] = {0};
    double ago = em_x[ii * nnei + nnei - 1];
    bool unloop = false; 

    double * out0 = &out[ii * last_layer_size * 4 + 0 * last_layer_size];
    double * out1 = &out[ii * last_layer_size * 4 + 1 * last_layer_size];
    double * out2 = &out[ii * last_layer_size * 4 + 2 * last_layer_size];
    double * out3 = &out[ii * last_layer_size * 4 + 3 * last_layer_size];

    for (int jj = 0; jj < nnei; jj++) { 
      ll[0] = em[ii * nnei * 4 + jj * 4 + 0];
      ll[1] = em[ii * nnei * 4 + jj * 4 + 1];
      ll[2] = em[ii * nnei * 4 + jj * 4 + 2];
      ll[3] = em[ii * nnei * 4 + jj * 4 + 3];
      double xx = em_x[ii * nnei + jj]; 
      if (ago == xx) {
        unloop = true;
      }
      int table_idx = 0;
      locate_xx(lower, upper, _max, stride0, stride1, xx, table_idx);

#ifdef __ARM_FEATURE_SVE 
      
      assert(last_layer_size % svcntd() == 0);

      svbool_t ptrue = svptrue_b64();
      svfloat64_t vnei_sub_jj = svdup_f64((double(nnei - jj)));
      svfloat64_t vxx = svdup_f64(xx);
      svfloat64_t vxx2 = svmul_z(ptrue, vxx, vxx);
      svfloat64_t vxx3 = svmul_z(ptrue, vxx2, vxx);
      svfloat64_t vxx4 = svmul_z(ptrue, vxx2, vxx2);
      svfloat64_t vxx5 = svmul_z(ptrue, vxx3, vxx2);
      svfloat64_t vll0 = svdup_f64(ll[0]);
      svfloat64_t vll1 = svdup_f64(ll[1]);
      svfloat64_t vll2 = svdup_f64(ll[2]);
      svfloat64_t vll3 = svdup_f64(ll[3]);
      svfloat64_t vll0_ = svmul_z(ptrue, vll0, vnei_sub_jj);
      svfloat64_t vll1_ = svmul_z(ptrue, vll1, vnei_sub_jj);
      svfloat64_t vll2_ = svmul_z(ptrue, vll2, vnei_sub_jj);
      svfloat64_t vll3_ = svmul_z(ptrue, vll3, vnei_sub_jj);

      for(int kk = 0; kk < last_layer_size; kk += svcntd() * 2){
        const double* TABLE = &table[table_idx * last_layer_size * 6 + kk * 6];
        svfloat64_t va0_0 = svld1_vnum(ptrue, TABLE, 0);
        svfloat64_t va0_1 = svld1_vnum(ptrue, TABLE, 1);
        svfloat64_t va1_0 = svld1_vnum(ptrue, TABLE, 2);
        svfloat64_t va1_1 = svld1_vnum(ptrue, TABLE, 3);
        svfloat64_t va2_0 = svld1_vnum(ptrue, TABLE, 4);
        svfloat64_t va2_1 = svld1_vnum(ptrue, TABLE, 5);
        svfloat64_t va3_0 = svld1_vnum(ptrue, TABLE, 6);
        svfloat64_t va3_1 = svld1_vnum(ptrue, TABLE, 7);
        svfloat64_t va4_0 = svld1_vnum(ptrue, TABLE, 8);
        svfloat64_t va4_1 = svld1_vnum(ptrue, TABLE, 9);
        svfloat64_t va5_0 = svld1_vnum(ptrue, TABLE, 10);
        svfloat64_t va5_1 = svld1_vnum(ptrue, TABLE, 11);

        svfloat64_t tmp1_0 = svmla_z(ptrue, va0_0, va1_0, vxx);
        svfloat64_t tmp1_1 = svmla_z(ptrue, va0_1, va1_1, vxx);
        svfloat64_t tmp2_0 = svmul_z(ptrue, va2_0, vxx2);
        svfloat64_t tmp2_1 = svmul_z(ptrue, va2_1, vxx2);
        svfloat64_t tmp3_0 = svmul_z(ptrue, va3_0, vxx3);
        svfloat64_t tmp3_1 = svmul_z(ptrue, va3_1, vxx3);
        svfloat64_t tmp4_0 = svmul_z(ptrue, va4_0, vxx4);
        svfloat64_t tmp4_1 = svmul_z(ptrue, va4_1, vxx4);
        svfloat64_t tmp5_0 = svmul_z(ptrue, va5_0, vxx5);
        svfloat64_t tmp5_1 = svmul_z(ptrue, va5_1, vxx5);
        svfloat64_t tmp6_0 = svadd_z(ptrue, tmp1_0, tmp2_0);
        svfloat64_t tmp6_1 = svadd_z(ptrue, tmp1_1, tmp2_1);
        svfloat64_t tmp7_0 = svadd_z(ptrue, tmp3_0, tmp4_0);
        svfloat64_t tmp7_1 = svadd_z(ptrue, tmp3_1, tmp4_1);
        svfloat64_t tmp8_0 = svadd_z(ptrue, tmp6_0, tmp5_0);
        svfloat64_t tmp8_1 = svadd_z(ptrue, tmp6_1, tmp5_1);
        svfloat64_t vvar_0 = svadd_z(ptrue, tmp7_0, tmp8_0);
        svfloat64_t vvar_1 = svadd_z(ptrue, tmp7_1, tmp8_1);

        svfloat64_t vout0_0 = svld1(ptrue, out0 + kk);
        svfloat64_t vout0_1 = svld1(ptrue, out0 + kk + svcntd());
        svfloat64_t vout1_0 = svld1(ptrue, out1 + kk);
        svfloat64_t vout1_1 = svld1(ptrue, out1 + kk + svcntd());
        svfloat64_t vout2_0 = svld1(ptrue, out2 + kk);
        svfloat64_t vout2_1 = svld1(ptrue, out2 + kk + svcntd());
        svfloat64_t vout3_0 = svld1(ptrue, out3 + kk);
        svfloat64_t vout3_1 = svld1(ptrue, out3 + kk + svcntd());

        if(unloop){
          vout0_0 = svmla_z(ptrue, vout0_0, vvar_0, vll0_);
          vout0_1 = svmla_z(ptrue, vout0_1, vvar_1, vll0_);
          vout1_0 = svmla_z(ptrue, vout1_0, vvar_0, vll1_);
          vout1_1 = svmla_z(ptrue, vout1_1, vvar_1, vll1_);
          vout2_0 = svmla_z(ptrue, vout2_0, vvar_0, vll2_);
          vout2_1 = svmla_z(ptrue, vout2_1, vvar_1, vll2_);
          vout3_0 = svmla_z(ptrue, vout3_0, vvar_0, vll3_);
          vout3_1 = svmla_z(ptrue, vout3_1, vvar_1, vll3_);
        }else{
          vout0_0 = svmla_z(ptrue, vout0_0, vvar_0, vll0);
          vout0_1 = svmla_z(ptrue, vout0_1, vvar_1, vll0);
          vout1_0 = svmla_z(ptrue, vout1_0, vvar_0, vll1);
          vout1_1 = svmla_z(ptrue, vout1_1, vvar_1, vll1);
          vout2_0 = svmla_z(ptrue, vout2_0, vvar_0, vll2);
          vout2_1 = svmla_z(ptrue, vout2_1, vvar_1, vll2);
          vout3_0 = svmla_z(ptrue, vout3_0, vvar_0, vll3);
          vout3_1 = svmla_z(ptrue, vout3_1, vvar_1, vll3);
        }
        svst1(ptrue, out0 + kk, vout0_0);
        svst1(ptrue, out0 + kk + svcntd(), vout0_1);
        svst1(ptrue, out1 + kk, vout1_0);
        svst1(ptrue, out1 + kk + svcntd(), vout1_1);
        svst1(ptrue, out2 + kk, vout2_0);
        svst1(ptrue, out2 + kk + svcntd(), vout2_1);
        svst1(ptrue, out3 + kk, vout3_0);
        svst1(ptrue, out3 + kk + svcntd(), vout3_1);
      }

#else

      for (int kbs = 0; kbs < last_layer_size; kbs+=16){
        int kbe = kbs + 16;
        const double* table0 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 0];
        const double* table1 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 1];
        const double* table2 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 2];
        const double* table3 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 3];
        const double* table4 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 4];
        const double* table5 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 5];
        for (int kk = kbs; kk < kbe; kk++) {
          double a0  = table0[kk-kbs]; 
          double a1  = table1[kk-kbs]; 
          double a2  = table2[kk-kbs]; 
          double a3  = table3[kk-kbs];
          double a4  = table4[kk-kbs];
          double a5  = table5[kk-kbs];
          double var = a0 + (a1 + (a2 + (a3 + (a4 + a5 * xx) * xx) * xx) * xx) * xx;
          if (unloop) {
            out0[kk] += (nnei - jj) * var * ll[0];
            out1[kk] += (nnei - jj) * var * ll[1];
            out2[kk] += (nnei - jj) * var * ll[2];
            out3[kk] += (nnei - jj) * var * ll[3];
          }
          else {
            out0[kk] += var * ll[0];
            out1[kk] += var * ll[1];
            out2[kk] += var * ll[2];
            out3[kk] += var * ll[3];
          }
        }
      }

#endif /* __ARM_FEATURE_SVE */

      if (unloop) break;
    }
  }
}

void deepmd::tabulate_fusion_cpu_packing_sve(
    float * out,
    const float * table, 
    const float * table_info, 
    const float * em_x, 
    const float * em, 
    const int nloc, 
    const int nnei, 
    const int last_layer_size)
{
  memset(out, 0.0, sizeof(float) * nloc * 4 * last_layer_size);
  const float lower   = table_info[0];
  const float upper   = table_info[1];
  const float _max    = table_info[2];
  const float stride0 = table_info[3];
  const float stride1 = table_info[4];

  // std::cout << "(nloc,nnei,last_layer_size)" << " : " << "(" << nloc << "," << nnei << "," << last_layer_size << ")" << std::endl;

  // for every atom, execute a small manual gemm ~
  // float * res = new float[4 * last_layer_size];
  // #pragma omp parallel for
  for (int ii = 0; ii < nloc; ii++) {
    float ll[4] = {0};
    float ago = em_x[ii * nnei + nnei - 1];
    bool unloop = false; 

    float * out0 = &out[ii * last_layer_size * 4 + 0 * last_layer_size];
    float * out1 = &out[ii * last_layer_size * 4 + 1 * last_layer_size];
    float * out2 = &out[ii * last_layer_size * 4 + 2 * last_layer_size];
    float * out3 = &out[ii * last_layer_size * 4 + 3 * last_layer_size];

    for (int jj = 0; jj < nnei; jj++) { 
      ll[0] = em[ii * nnei * 4 + jj * 4 + 0];
      ll[1] = em[ii * nnei * 4 + jj * 4 + 1];
      ll[2] = em[ii * nnei * 4 + jj * 4 + 2];
      ll[3] = em[ii * nnei * 4 + jj * 4 + 3];
      float xx = em_x[ii * nnei + jj]; 
      if (ago == xx) {
        unloop = true;
      }
      int table_idx = 0;
      locate_xx(lower, upper, _max, stride0, stride1, xx, table_idx);

      for (int kbs = 0; kbs < last_layer_size; kbs+=16){
        int kbe = kbs + 16;
        const float* table0 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 0];
        const float* table1 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 1];
        const float* table2 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 2];
        const float* table3 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 3];
        const float* table4 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 4];
        const float* table5 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 5];
        for (int kk = kbs; kk < kbe; kk++) {
          float a0  = table0[kk-kbs]; 
          float a1  = table1[kk-kbs]; 
          float a2  = table2[kk-kbs]; 
          float a3  = table3[kk-kbs];
          float a4  = table4[kk-kbs];
          float a5  = table5[kk-kbs];
          float var = a0 + (a1 + (a2 + (a3 + (a4 + a5 * xx) * xx) * xx) * xx) * xx;
          if (unloop) {
            out0[kk] += (nnei - jj) * var * ll[0];
            out1[kk] += (nnei - jj) * var * ll[1];
            out2[kk] += (nnei - jj) * var * ll[2];
            out3[kk] += (nnei - jj) * var * ll[3];
          }
          else {
            out0[kk] += var * ll[0];
            out1[kk] += var * ll[1];
            out2[kk] += var * ll[2];
            out3[kk] += var * ll[3];
          }
        }
      }
      
      if (unloop) break;
    }
  }
}



void deepmd::tabulate_fusion_grad_cpu_packing_sve(
    double * dy_dem_x, 
    double * dy_dem,
    const double * table, 
    const double * table_info, 
    const double * em_x, 
    const double * em, 
    const double * dy, 
    const int nloc, 
    const int nnei, 
    const int last_layer_size) 
{
  memset(dy_dem_x, 0.0, sizeof(double) * nloc * nnei);
  memset(dy_dem, 0.0, sizeof(double) * nloc * nnei * 4);
  double const lower   = table_info[0];
  double const upper   = table_info[1];
  double const _max    = table_info[2];
  double const stride0 = table_info[3];
  double const stride1 = table_info[4];
  // for every atom, execute a small gemm~
  // double * res = new double[4 * last_layer_size];
  // #pragma omp parallel for
  for (int ii = 0; ii < nloc; ii++) {
    double ll[4];
    double rr[4];
    double ago = em_x[ii * nnei + nnei - 1];
    const double* dy0 = &dy[ii * last_layer_size * 4 + 0 * last_layer_size];
    const double* dy1 = &dy[ii * last_layer_size * 4 + 1 * last_layer_size];
    const double* dy2 = &dy[ii * last_layer_size * 4 + 2 * last_layer_size];
    const double* dy3 = &dy[ii * last_layer_size * 4 + 3 * last_layer_size];
    bool unloop = false;
    for (int jj = 0; jj < nnei; jj++) {
      // construct the dy/dx
      ll[0] = em[ii * nnei * 4 + jj * 4 + 0];
      ll[1] = em[ii * nnei * 4 + jj * 4 + 1];
      ll[2] = em[ii * nnei * 4 + jj * 4 + 2];
      ll[3] = em[ii * nnei * 4 + jj * 4 + 3];
      double xx = em_x[ii * nnei + jj]; 
      if (ago == xx) {
      unloop = true;
      }
      int table_idx = 0;
      locate_xx(lower, upper, _max, stride0, stride1, xx, table_idx);

      double* dy_dem_tmp = &dy_dem[ii * nnei * 4 + jj * 4];
      double grad = 0.0;

#ifdef __ARM_FEATURE_SVE 

      assert(last_layer_size % svcntd() == 0);
      svfloat64_t vtwo = svdup_f64(2.);
      svfloat64_t vthree = svdup_f64(3.);
      svfloat64_t vfour = svdup_f64(4.);
      svfloat64_t vfive = svdup_f64(5.);

      svbool_t ptrue = svptrue_b64();
      svfloat64_t vnei_sub_jj = svdup_f64((double(nnei - jj)));
      svfloat64_t vxx = svdup_f64(xx);

      svfloat64_t vxx2 = svmul_z(ptrue, vxx, vxx);
      svfloat64_t vxx3 = svmul_z(ptrue, vxx2, vxx);
      svfloat64_t vxx4 = svmul_z(ptrue, vxx2, vxx2);
      svfloat64_t vxx5 = svmul_z(ptrue, vxx3, vxx2);
      svfloat64_t v2xx1 = svmul_z(ptrue, vtwo, vxx);
      svfloat64_t v3xx2 = svmul_z(ptrue, vthree, vxx2);
      svfloat64_t v4xx3 = svmul_z(ptrue, vfour, vxx3);
      svfloat64_t v5xx4 = svmul_z(ptrue, vfive, vxx4);
      svfloat64_t vll0 = svdup_f64(ll[0]);
      svfloat64_t vll1 = svdup_f64(ll[1]);
      svfloat64_t vll2 = svdup_f64(ll[2]);
      svfloat64_t vll3 = svdup_f64(ll[3]);
      svfloat64_t vll0_ = svmul_z(ptrue, vll0, vnei_sub_jj);
      svfloat64_t vll1_ = svmul_z(ptrue, vll1, vnei_sub_jj);
      svfloat64_t vll2_ = svmul_z(ptrue, vll2, vnei_sub_jj);
      svfloat64_t vll3_ = svmul_z(ptrue, vll3, vnei_sub_jj);
      for(int kk = 0; kk < last_layer_size; kk += svcntd() * 2){
        svfloat64_t vrr0_0 = svld1(ptrue, dy0 + kk);
        svfloat64_t vrr0_1 = svld1(ptrue, dy0 + kk + svcntd());
        svfloat64_t vrr1_0 = svld1(ptrue, dy1 + kk);
        svfloat64_t vrr1_1 = svld1(ptrue, dy1 + kk + svcntd());
        svfloat64_t vrr2_0 = svld1(ptrue, dy2 + kk);
        svfloat64_t vrr2_1 = svld1(ptrue, dy2 + kk + svcntd());
        svfloat64_t vrr3_0 = svld1(ptrue, dy3 + kk);
        svfloat64_t vrr3_1 = svld1(ptrue, dy3 + kk + svcntd());

        const double* TABLE = &table[table_idx * last_layer_size * 6 + kk * 6];
        svfloat64_t va0_0 = svld1_vnum(ptrue, TABLE, 0);
        svfloat64_t va0_1 = svld1_vnum(ptrue, TABLE, 1);
        svfloat64_t va1_0 = svld1_vnum(ptrue, TABLE, 2);
        svfloat64_t va1_1 = svld1_vnum(ptrue, TABLE, 3);
        svfloat64_t va2_0 = svld1_vnum(ptrue, TABLE, 4);
        svfloat64_t va2_1 = svld1_vnum(ptrue, TABLE, 5);
        svfloat64_t va3_0 = svld1_vnum(ptrue, TABLE, 6);
        svfloat64_t va3_1 = svld1_vnum(ptrue, TABLE, 7);
        svfloat64_t va4_0 = svld1_vnum(ptrue, TABLE, 8);
        svfloat64_t va4_1 = svld1_vnum(ptrue, TABLE, 9);
        svfloat64_t va5_0 = svld1_vnum(ptrue, TABLE, 10);
        svfloat64_t va5_1 = svld1_vnum(ptrue, TABLE, 11);

        // double res = a0 + a1 * xx + a2 * xx2 + a3 * xx3 + a4 * xx4 + a5 * xx5;
        svfloat64_t tmp1_0 = svmla_z(ptrue, va0_0, va1_0, vxx);
        svfloat64_t tmp1_1 = svmla_z(ptrue, va0_1, va1_1, vxx);
        svfloat64_t tmp2_0 = svmul_z(ptrue, va2_0, vxx2);
        svfloat64_t tmp2_1 = svmul_z(ptrue, va2_1, vxx2);
        svfloat64_t tmp3_0 = svmul_z(ptrue, va3_0, vxx3);
        svfloat64_t tmp3_1 = svmul_z(ptrue, va3_1, vxx3);
        svfloat64_t tmp4_0 = svmul_z(ptrue, va4_0, vxx4);
        svfloat64_t tmp4_1 = svmul_z(ptrue, va4_1, vxx4);
        svfloat64_t tmp5_0 = svmul_z(ptrue, va5_0, vxx5);
        svfloat64_t tmp5_1 = svmul_z(ptrue, va5_1, vxx5);
        svfloat64_t tmp6_0 = svadd_z(ptrue, tmp1_0, tmp2_0);
        svfloat64_t tmp6_1 = svadd_z(ptrue, tmp1_1, tmp2_1);
        svfloat64_t tmp7_0 = svadd_z(ptrue, tmp3_0, tmp4_0);
        svfloat64_t tmp7_1 = svadd_z(ptrue, tmp3_1, tmp4_1);
        svfloat64_t tmp8_0 = svadd_z(ptrue, tmp6_0, tmp5_0);
        svfloat64_t tmp8_1 = svadd_z(ptrue, tmp6_1, tmp5_1);
        svfloat64_t vres_0 = svadd_z(ptrue, tmp7_0, tmp8_0);
        svfloat64_t vres_1 = svadd_z(ptrue, tmp7_1, tmp8_1);

        // a1 + 2 * a2 * xx + 3 * a3 * xx2 + 4 * a4 * xx3 + 5 * a5 *xx4
        svfloat64_t tmp9_0 = svmla_z(ptrue, va1_0, va2_0, v2xx1);
        svfloat64_t tmp9_1 = svmla_z(ptrue, va1_1, va2_1, v2xx1);
        svfloat64_t tmp10_0 = svmul_z(ptrue, va3_0, v3xx2);
        svfloat64_t tmp10_1 = svmul_z(ptrue, va3_1, v3xx2);
        svfloat64_t tmp11_0 = svmul_z(ptrue, va4_0, v4xx3);
        svfloat64_t tmp11_1 = svmul_z(ptrue, va4_1, v4xx3);
        svfloat64_t tmp12_0 = svmul_z(ptrue, va5_0, v5xx4);
        svfloat64_t tmp12_1 = svmul_z(ptrue, va5_1, v5xx4);
        svfloat64_t tmp13_0 = svadd_z(ptrue, tmp9_0, tmp10_0);
        svfloat64_t tmp13_1 = svadd_z(ptrue, tmp9_1, tmp10_1);
        svfloat64_t tmp14_0 = svadd_z(ptrue, tmp11_0, tmp12_0);
        svfloat64_t tmp14_1 = svadd_z(ptrue, tmp11_1, tmp12_1);
        svfloat64_t tmp15_0 = svadd_z(ptrue, tmp13_0, tmp14_0); 
        svfloat64_t tmp15_1 = svadd_z(ptrue, tmp13_1, tmp14_1); 

        // dot(ll, rr);
        svfloat64_t tmp16_0 = svmul_z(ptrue, vll0, vrr0_0);
        svfloat64_t tmp16_1 = svmul_z(ptrue, vll0, vrr0_1);
        svfloat64_t tmp17_0 = svmul_z(ptrue, vll1, vrr1_0);
        svfloat64_t tmp17_1 = svmul_z(ptrue, vll1, vrr1_1);
        svfloat64_t tmp18_0 = svmul_z(ptrue, vll2, vrr2_0);
        svfloat64_t tmp18_1 = svmul_z(ptrue, vll2, vrr2_1);
        svfloat64_t tmp19_0 = svmul_z(ptrue, vll3, vrr3_0);
        svfloat64_t tmp19_1 = svmul_z(ptrue, vll3, vrr3_1);
        svfloat64_t tmp20_0 = svadd_z(ptrue, tmp16_0, tmp17_0);
        svfloat64_t tmp20_1 = svadd_z(ptrue, tmp16_1, tmp17_1);
        svfloat64_t tmp21_0 = svadd_z(ptrue, tmp18_0, tmp19_0);
        svfloat64_t tmp21_1 = svadd_z(ptrue, tmp18_1, tmp19_1);
        svfloat64_t tmp22_0 = svadd_z(ptrue, tmp20_0, tmp21_0);
        svfloat64_t tmp22_1 = svadd_z(ptrue, tmp20_1, tmp21_1);

        // grad = (a1 + 2 * a2 * xx + 3 * a3 * xx2 + 4 * a4 * xx3 + 5 * a5 *xx4 ) * dot(ll, rr);
        svfloat64_t vgard_0 = svmul_z(ptrue, tmp15_0, tmp22_0);
        svfloat64_t vgard_1 = svmul_z(ptrue, tmp15_1, tmp22_1);

        svfloat64_t vres0_0 = svmul_z(ptrue, vres_0, vrr0_0);
        svfloat64_t vres0_1 = svmul_z(ptrue, vres_1, vrr0_1);
        svfloat64_t vres1_0 = svmul_z(ptrue, vres_0, vrr1_0);
        svfloat64_t vres1_1 = svmul_z(ptrue, vres_1, vrr1_1);
        svfloat64_t vres2_0 = svmul_z(ptrue, vres_0, vrr2_0);
        svfloat64_t vres2_1 = svmul_z(ptrue, vres_1, vrr2_1);
        svfloat64_t vres3_0 = svmul_z(ptrue, vres_0, vrr3_0);
        svfloat64_t vres3_1 = svmul_z(ptrue, vres_1, vrr3_1);
        if(unloop){
          vgard_0 = svmul_z(ptrue, vgard_0, vnei_sub_jj);
          vgard_1 = svmul_z(ptrue, vgard_1, vnei_sub_jj);
          vres0_0 = svmul_z(ptrue, vres0_0, vnei_sub_jj);
          vres0_1 = svmul_z(ptrue, vres0_1, vnei_sub_jj);
          vres1_0 = svmul_z(ptrue, vres1_0, vnei_sub_jj);
          vres1_1 = svmul_z(ptrue, vres1_1, vnei_sub_jj);
          vres2_0 = svmul_z(ptrue, vres2_0, vnei_sub_jj);
          vres2_1 = svmul_z(ptrue, vres2_1, vnei_sub_jj);
          vres3_0 = svmul_z(ptrue, vres3_0, vnei_sub_jj);
          vres3_1 = svmul_z(ptrue, vres3_1, vnei_sub_jj);
        }
        grad += svaddv(ptrue, vgard_0);
        dy_dem_tmp[0] += svaddv(ptrue, vres0_0);
        dy_dem_tmp[1] += svaddv(ptrue, vres1_0);
        dy_dem_tmp[2] += svaddv(ptrue, vres2_0);
        dy_dem_tmp[3] += svaddv(ptrue, vres3_0);
        grad += svaddv(ptrue, vgard_1);
        dy_dem_tmp[0] += svaddv(ptrue, vres0_1);
        dy_dem_tmp[1] += svaddv(ptrue, vres1_1);
        dy_dem_tmp[2] += svaddv(ptrue, vres2_1);
        dy_dem_tmp[3] += svaddv(ptrue, vres3_1);
      }
#else
      for (int kbs = 0; kbs < last_layer_size; kbs += 16){
        int kbe = kbs + 16;
        const double* table0 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 0];
        const double* table1 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 1];
        const double* table2 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 2];
        const double* table3 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 3];
        const double* table4 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 4];
        const double* table5 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 5];
        for (int kk = kbs; kk < kbe; kk++) {
          rr[0] = dy0[kk];
          rr[1] = dy1[kk];
          rr[2] = dy2[kk];
          rr[3] = dy3[kk];
          double a0  = table0[kk-kbs]; 
          double a1  = table1[kk-kbs]; 
          double a2  = table2[kk-kbs]; 
          double a3  = table3[kk-kbs];
          double a4  = table4[kk-kbs];
          double a5  = table5[kk-kbs];
          double res = a0 + (a1 + (a2 + (a3 + (a4 + a5 * xx) * xx) * xx) * xx) * xx;

          if (unloop) {
            grad += (a1 + (2 * a2 + (3 * a3 + (4 * a4 + 5 * a5 * xx) * xx) * xx) * xx) * dot(ll, rr) * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 0] += res * rr[0] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 1] += res * rr[1] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 2] += res * rr[2] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 3] += res * rr[3] * (nnei - jj);
          }
          else {
            grad += (a1 + (2 * a2 + (3 * a3 + (4 * a4 + 5 * a5 * xx) * xx) * xx) * xx) * dot(ll, rr);
            dy_dem[ii * nnei * 4 + jj * 4 + 0] += res * rr[0];
            dy_dem[ii * nnei * 4 + jj * 4 + 1] += res * rr[1];
            dy_dem[ii * nnei * 4 + jj * 4 + 2] += res * rr[2];
            dy_dem[ii * nnei * 4 + jj * 4 + 3] += res * rr[3];
          }
        }
      }

#endif /* __ARM_FEATURE_SVE */
      dy_dem_x[ii * nnei + jj] = grad;
      if (unloop) break;
    }
  }
}



void deepmd::tabulate_fusion_grad_cpu_packing_sve(
    float * dy_dem_x, 
    float * dy_dem,
    const float * table, 
    const float * table_info, 
    const float * em_x, 
    const float * em, 
    const float * dy, 
    const int nloc, 
    const int nnei, 
    const int last_layer_size) 
{
  memset(dy_dem_x, 0.0, sizeof(float) * nloc * nnei);
  memset(dy_dem, 0.0, sizeof(float) * nloc * nnei * 4);
  float const lower   = table_info[0];
  float const upper   = table_info[1];
  float const _max    = table_info[2];
  float const stride0 = table_info[3];
  float const stride1 = table_info[4];
  // for every atom, execute a small gemm~
  // float * res = new float[4 * last_layer_size];
  // #pragma omp parallel for
  for (int ii = 0; ii < nloc; ii++) {
    float ll[4];
    float rr[4];
    float ago = em_x[ii * nnei + nnei - 1];
    const float* dy0 = &dy[ii * last_layer_size * 4 + 0 * last_layer_size];
    const float* dy1 = &dy[ii * last_layer_size * 4 + 1 * last_layer_size];
    const float* dy2 = &dy[ii * last_layer_size * 4 + 2 * last_layer_size];
    const float* dy3 = &dy[ii * last_layer_size * 4 + 3 * last_layer_size];
    bool unloop = false;
    for (int jj = 0; jj < nnei; jj++) {
      // construct the dy/dx
      ll[0] = em[ii * nnei * 4 + jj * 4 + 0];
      ll[1] = em[ii * nnei * 4 + jj * 4 + 1];
      ll[2] = em[ii * nnei * 4 + jj * 4 + 2];
      ll[3] = em[ii * nnei * 4 + jj * 4 + 3];
      float xx = em_x[ii * nnei + jj]; 
      if (ago == xx) {
        unloop = true;
      }
      int table_idx = 0;
      locate_xx(lower, upper, _max, stride0, stride1, xx, table_idx);
      
      float grad = 0.0;

      for (int kbs = 0; kbs < last_layer_size; kbs += 16){
        int kbe = kbs + 16;
        const float* table0 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 0];
        const float* table1 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 1];
        const float* table2 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 2];
        const float* table3 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 3];
        const float* table4 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 4];
        const float* table5 = &table[table_idx * last_layer_size * 6 + kbs * 6 + 16 * 5];
        for (int kk = kbs; kk < kbe; kk++) {
          rr[0] = dy0[kk];
          rr[1] = dy1[kk];
          rr[2] = dy2[kk];
          rr[3] = dy3[kk];
          float a0  = table0[kk-kbs]; 
          float a1  = table1[kk-kbs]; 
          float a2  = table2[kk-kbs]; 
          float a3  = table3[kk-kbs];
          float a4  = table4[kk-kbs];
          float a5  = table5[kk-kbs];
          float res = a0 + (a1 + (a2 + (a3 + (a4 + a5 * xx) * xx) * xx) * xx) * xx;

          if (unloop) {
            grad += (a1 + (2 * a2 + (3 * a3 + (4 * a4 + 5 * a5 * xx) * xx) * xx) * xx) * dot(ll, rr) * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 0] += res * rr[0] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 1] += res * rr[1] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 2] += res * rr[2] * (nnei - jj);
            dy_dem[ii * nnei * 4 + jj * 4 + 3] += res * rr[3] * (nnei - jj);
          }
          else {
            grad += (a1 + (2 * a2 + (3 * a3 + (4 * a4 + 5 * a5 * xx) * xx) * xx) * xx) * dot(ll, rr);
            dy_dem[ii * nnei * 4 + jj * 4 + 0] += res * rr[0];
            dy_dem[ii * nnei * 4 + jj * 4 + 1] += res * rr[1];
            dy_dem[ii * nnei * 4 + jj * 4 + 2] += res * rr[2];
            dy_dem[ii * nnei * 4 + jj * 4 + 3] += res * rr[3];
          }
        }
      }

      dy_dem_x[ii * nnei + jj] = grad;
      if (unloop) break;
    }
  }
}