#ifndef DECAY_H
#define DECAY_H

// #include <cstdio>

const int FAKE_GAM_LINE_ID = 3;

#include "types.h"
#include "cuda.h"

namespace decay
{
  __host__ __device__ void init_nuclides(void);
  __host__ __device__ int get_num_nuclides(void);
  __host__ __device__ int get_nuc_z(int nucindex);
  __host__ __device__ int get_nuc_a(int nucindex);
  __host__ __device__ int get_nuc_index(int z, int a);
  __host__ __device__ double nucdecayenergygamma(int z, int a);
  __host__ __device__ void set_nucdecayenergygamma(int z, int a, double value);
  __host__ __device__ double nucdecayenergy(int z, int a);
  __host__ __device__ double get_meanlife(int z, int a);
  __host__ __device__ double nucmass(int z, int a);
  __host__ __device__ void update_abundances(const int modelgridindex, const int timestep, const double t_current);
  __host__ __device__ double get_endecay_per_ejectamass_t0_to_time_withexpansion(const int modelgridindex, const double tstart);
  __host__ __device__ double get_simtime_endecay_per_ejectamass(const int mgi, const bool from_parent_abund, const int z, const int a);
  __host__ __device__ double get_modelcell_decay_energy_density(const int mgi);
  __host__ __device__ double get_positroninjection_rate_density(const int modelgridindex, const double t);
  __host__ __device__ double get_global_etot_t0_tinf(void);
  void setup_radioactive_pellet(const double e0, const int mgi, PKT *pkt_ptr);
}

#endif //DECAY_H
