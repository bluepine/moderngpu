// moderngpu copyright (c) 2016, Sean Baxter http://www.moderngpu.com
#pragma once

#include <random>
#include <algorithm>
#include "launch_box.hxx"

BEGIN_MGPU_NAMESPACE

// TODO: pass this state instead of tid/cta. This allows us to read in the
// count from device memory.
struct cta_state_t {
  int tid;
  int cta;
  size_t index;
  size_t count;
};

struct device_size_t {
  size_t host_size;
  const size_t* device_size;

  MGPU_HOST_DEVICE operator size_t() const { 
#ifdef __CUDA_ARCH__
    return *device_size; 
#else
    return host_size;
#endif
  }
};

////////////////////////////////////////////////////////////////////////////////
// Launch a grid given a number of CTAs.

template<typename launch_box, typename func_t, typename... args_t>
void cta_launch(func_t f, int num_ctas, context_t& context, args_t... args) { 
  cta_dim_t cta = launch_box::cta_dim(context.ptx_version());
  launch_box_cta_k<launch_box, func_t>
    <<<num_ctas, cta.nt, 0, context.stream()>>>(f, args...);
}

template<int nt, typename func_t, typename... args_t>
void cta_launch(func_t f, int num_ctas, context_t& context, args_t... args) {
  cta_launch<launch_params_t<nt, 1> >(f, num_ctas, context, args...);
}

////////////////////////////////////////////////////////////////////////////////
// Launch a grid given a number of work-items.

template<typename launch_box, typename func_t, typename... args_t>
void cta_transform(func_t f, int count, context_t& context, args_t... args) {
  cta_dim_t cta = launch_box::cta_dim(context.ptx_version());
  int num_ctas = div_up(count, cta.nv());
  cta_launch<launch_box>(f, num_ctas, context, args...);
}

template<int nt, int vt = 1, typename func_t, typename... args_t>
void cta_transform(func_t f, int count, context_t& context, args_t... args) {
  cta_transform<launch_params_t<nt, vt> >(f, count, context, args...);
}

////////////////////////////////////////////////////////////////////////////////
// Launch persistent CTAs and loop through num_ctas values.

template<typename launch_box, typename func_t, typename... args_t>
void cta_launch(func_t f, const int* num_tiles, context_t& context, 
  args_t... args) {

  // Over-subscribe the device by a factor of 8.
  // This reduces the penalty if we can't schedule all the CTAs to run 
  // concurrently.
  int num_ctas = 8 * occupancy<launch_box>(f, context);

  auto k = [=] MGPU_DEVICE(int tid, int cta, args_t... args) {
    int count = *num_tiles;
    while(cta < count) {
      f(tid, cta, args...);
      cta += num_ctas;
    }
  };
  cta_launch<launch_box>(k, num_ctas, context, args...);
}

////////////////////////////////////////////////////////////////////////////////
// Ordinary transform launch. This uses the standard launch box mechanism 
// so we can query its occupancy and other things.

template<typename launch_t, typename func_t, typename... args_t>
void transform(func_t f, size_t count, context_t& context, args_t... args) {

  cta_transform<launch_t>([=]MGPU_DEVICE(int tid, int cta, args_t... args) {
    typedef typename launch_t::sm_ptx params_t;
    enum { nt = params_t::nt, vt = params_t::vt, vt0 = params_t::vt0 };

   range_t range = get_tile(cta, nt * vt, count);

    strided_iterate<nt, vt, vt0>([=](int i, int j) {
      f(range.begin + j, args...);
    }, tid, range.count());

  }, count, context, args...);
}

template<size_t nt = 128, typename func_t, typename... args_t>
void transform(func_t f, size_t count, context_t& context, args_t... args) {
  transform<launch_params_t<nt, 1, 0> >(f, count, context, args...);
}

END_MGPU_NAMESPACE
