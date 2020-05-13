///
/// Copyright (c) 2017, Intel Corporation
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions
/// are met:
///
/// * Redistributions of source code must retain the above copyright
///       notice, this list of conditions and the following disclaimer.
/// * Redistributions in binary form must reproduce the above
///       copyright notice, this list of conditions and the following
///       disclaimer in the documentation and/or other materials provided
///       with the distribution.
/// * Neither the name of Intel Corporation nor the names of its
///       contributors may be used to endorse or promote products
///       derived from this software without specific prior written
///       permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
/// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
/// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
/// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
/// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
/// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
/// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
/// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
/// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
/// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
/// POSSIBILITY OF SUCH DAMAGE.

//////////////////////////////////////////////////////////////////////
///
/// NAME:    nstream
///
/// PURPOSE: To compute memory bandwidth when adding a vector of a given
///          number of double precision values to the scalar multiple of
///          another vector of the same length, and storing the result in
///          a third vector.
///
/// USAGE:   The program takes as input the number
///          of iterations to loop over the triad vectors, the length of the
///          vectors, and the offset between vectors
///
///          <progname> <# iterations> <vector length> <offset>
///
///          The output consists of diagnostics to make sure the
///          algorithm worked, and of timing statistics.
///
/// NOTES:   Bandwidth is determined as the number of words read, plus the
///          number of words written, times the size of the words, divided
///          by the execution time. For a vector length of N, the total
///          number of words read and written is 4*N*sizeof(double).
///
/// HISTORY: This code is loosely based on the Stream benchmark by John
///          McCalpin, but does not follow all the Stream rules. Hence,
///          reported results should not be associated with Stream in
///          external publications
///
///          Converted to C++11 by Jeff Hammond, November 2017.
///
//////////////////////////////////////////////////////////////////////

#include <CL/sycl.hpp>
#include <dpct/dpct.hpp>
#include "prk_util.h"
#include "prk_dpct.h"

void nstream(const unsigned n, const double scalar, double * A, const double * B, const double * C,
             sycl::nd_item<3> item_ct1)
{
    auto i = item_ct1.get_group(2) * item_ct1.get_local_range().get(2) + item_ct1.get_local_id(2);
    if (i < n) {
        A[i] += B[i] + scalar * C[i];
    }
}

void nstream2(const unsigned n, const double scalar, double * A, const double * B, const double * C,
              sycl::nd_item<3> item_ct1)
{
    for (unsigned int i =
             item_ct1.get_group(2) * item_ct1.get_local_range().get(2) +
             item_ct1.get_local_id(2);
         i < n;
         i += item_ct1.get_local_range().get(2) * item_ct1.get_group_range(2)) {
        A[i] += B[i] + scalar * C[i];
    }
}

int main(int argc, char * argv[])
{
  std::cout << "Parallel Research Kernels version " << PRKVERSION << std::endl;
  std::cout << "C++11/DPCT STREAM triad: A = B + scalar * C" << std::endl;

  //////////////////////////////////////////////////////////////////////
  /// Read and test input parameters
  //////////////////////////////////////////////////////////////////////

  int iterations;
  int length;
  bool grid_stride;
  try {
      if (argc < 3) {
        throw "Usage: <# iterations> <vector length> [<grid_stride>]";
      }

      iterations  = std::atoi(argv[1]);
      if (iterations < 1) {
        throw "ERROR: iterations must be >= 1";
      }

      length = std::atoi(argv[2]);
      if (length <= 0) {
        throw "ERROR: vector length must be positive";
      }

      grid_stride   = (argc>3) ? prk::parse_boolean(std::string(argv[4])) : false;
  }
  catch (const char * e) {
    std::cout << e << std::endl;
    return 1;
  }

  std::cout << "Number of iterations = " << iterations << std::endl;
  std::cout << "Vector length        = " << length << std::endl;
  std::cout << "Grid stride          = " << (grid_stride   ? "yes" : "no") << std::endl;

  const int blockSize = 256;
  sycl::range<3> dimBlock(blockSize, 1, 1);
  sycl::range<3> dimGrid(prk::divceil(length, blockSize), 1, 1);

  //////////////////////////////////////////////////////////////////////
  // Allocate space and perform the computation
  //////////////////////////////////////////////////////////////////////

  double nstream_time(0);

  const size_t bytes = length * sizeof(double);

  double * h_A = sycl::malloc_host<double>(length, dpct::get_default_context());
  double * h_B = sycl::malloc_host<double>(length, dpct::get_default_context());
  double * h_C = sycl::malloc_host<double>(length, dpct::get_default_context());

  for (size_t i=0; i<length; ++i) {
    h_A[i] = 0;
    h_B[i] = 2;
    h_C[i] = 2;
  }

  double * d_A = sycl::malloc_device<double>(length, dpct::get_default_context());
  double * d_B = sycl::malloc_device<double>(length, dpct::get_default_context());
  double * d_C = sycl::malloc_device<double>(length, dpct::get_default_context());
  dpct::get_default_queue().memcpy(d_A, &(h_A[0]), bytes).wait();
  dpct::get_default_queue().memcpy(d_B, &(h_B[0]), bytes).wait();
  dpct::get_default_queue().memcpy(d_C, &(h_C[0]), bytes).wait();

  double scalar(3);
  {
    for (int iter = 0; iter<=iterations; iter++) {

      if (iter==1) nstream_time = prk::wtime();

      if (grid_stride) {
          dpct::get_default_queue().submit([&](sycl::handler &cgh) {
              auto dpct_global_range = dimGrid * dimBlock;

              cgh.parallel_for(
                  sycl::nd_range<3>( sycl::range<3>(dpct_global_range.get(2), dpct_global_range.get(1), dpct_global_range.get(0)), sycl::range<3>(dimBlock.get(2), dimBlock.get(1), dimBlock.get(0))),
                  [=](sycl::nd_item<3> item_ct1) {
                      nstream2(static_cast<unsigned>(length), scalar, d_A, d_B, d_C, item_ct1);
                  });
          });
      } else {
          dpct::get_default_queue().submit([&](sycl::handler &cgh) {
              auto dpct_global_range = dimGrid * dimBlock;

              cgh.parallel_for(
                  sycl::nd_range<3>( sycl::range<3>(dpct_global_range.get(2), dpct_global_range.get(1), dpct_global_range.get(0)), sycl::range<3>(dimBlock.get(2), dimBlock.get(1), dimBlock.get(0))),
                  [=](sycl::nd_item<3> item_ct1) {
                      nstream(static_cast<unsigned>(length), scalar, d_A, d_B, d_C, item_ct1);
                  });
          });
      }
      dpct::get_current_device().queues_wait_and_throw();
    }
    nstream_time = prk::wtime() - nstream_time;
  }

  dpct::get_default_queue().memcpy(&(h_A[0]), d_A, bytes).wait();

  sycl::free(d_C, dpct::get_default_context());
  sycl::free(d_B, dpct::get_default_context());
  sycl::free(d_A, dpct::get_default_context());

  sycl::free(h_B, dpct::get_default_context());
  sycl::free(h_C, dpct::get_default_context());

  //////////////////////////////////////////////////////////////////////
  /// Analyze and output results
  //////////////////////////////////////////////////////////////////////

  double ar(0);
  double br(2);
  double cr(2);
  for (int i=0; i<=iterations; i++) {
      ar += br + scalar * cr;
  }

  ar *= length;

  double asum(0);
  for (int i=0; i<length; i++) {
    asum += std::fabs(h_A[i]);
  }

  sycl::free(h_A, dpct::get_default_context();

  double epsilon=1.e-8;
  if (std::fabs(ar - asum) / asum > epsilon) {
      std::cout << "Failed Validation on output array\n"
                << std::setprecision(16)
                << "       Expected checksum: " << ar << "\n"
                << "       Observed checksum: " << asum << std::endl;
      std::cout << "ERROR: solution did not validate" << std::endl;
      return 1;
  } else {
      std::cout << "Solution validates" << std::endl;
      double avgtime = nstream_time/iterations;
      double nbytes = 4.0 * length * sizeof(double);
      std::cout << "Rate (MB/s): " << 1.e-6*nbytes/avgtime
                << " Avg time (s): " << avgtime << std::endl;
  }

  return 0;
}


