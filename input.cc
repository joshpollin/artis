#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdio>
//#include <cmath>
//#include <cstdlib>
#include "sn3d.h"
#include "atomic.h"
#include "gamma.h"
#include "grid_init.h"
#include "input.h"
#include "nltepop.h"
#include "radfield.h"
#include "rpkt.h"
#include "vpkt.h"
#ifdef DO_EXSPEC
  #include "exspec.h"
#endif

const bool single_level_top_ion = false; // Only include a single level for the highest ion stage

const int groundstate_index_in = 1; // starting level index in the input files

typedef struct transitions_t
{
  int *to;
} transitions_t;

static transitions_t *transitions;

typedef struct
{
  int lower;
  int upper;
  double A;
  double coll_str;
  bool forbidden;
} transitiontable_entry;  /// only used temporarily during input

const int inputlinecommentcount = 24;
std::string inputlinecomments[inputlinecommentcount] = {
  "pre_zseed: specific random number seed if > 0 or random if negative",
  "globals::ntstep: number of timesteps",
  "itstep ftstep: number of start and end time step",
  "tmin_days tmax_days: start and end times [day]",
  "nusyn_min_mev nusyn_max_mev: lowest and highest frequency to synthesise [MeV]",
  "nsyn_time: number of times for synthesis",
  "start and end times for synthesis",
  "model_type: number of dimensions (1, 2, or 3)",
  "compute r-light curve (1: no estimators, 2: thin cells, 3: thick cells, 4: gamma-ray heating)",
  "n_out_it: number of iterations",
  "globals::CLIGHT_PROP/CLIGHT: change speed of light by some factor",
  "use grey opacity for gammas?",
  "syn_dir: x, y, and z components of unit vector (will be normalised after input or randomised if zero length)",
  "opacity_case: opacity choice",
  "rho_crit_para: free parameter for calculation of rho_crit",
  "UNUSED debug_packet: (>=0: activate debug output for packet id, <0: ignore)",
  "simulation_continued_from_saved: (0: start new simulation, 1: continue from gridsave and packets files)",
  "UNUSED rfcut_angstroms: wavelength (in Angstroms) at which the parameterisation of the radiation field switches from the nebular approximation to LTE.",
  "n_lte_timesteps",
  "cell_is_optically_thick n_grey_timesteps",
  "UNUSED max_bf_continua: (>0: max bound-free continua per ion, <0 unlimited)",
  "nprocs_exspec: extract spectra for n MPI tasks",
  "do_emission_res: Extract line-of-sight dependent information of last emission for spectrum_res (1: yes, 2: no)",
  "kpktdiffusion_timescale n_kpktdiffusion_timesteps: kpkts diffuse x of a time step's length for the first y time steps"
};


static void read_phixs_data_table(
  FILE *phixsdata, const int element, const int lowerion, const int lowerlevel, const int upperion, int upperlevel_in,
  const double phixs_threshold_ev,
  long *mem_usage_phixs, long *mem_usage_phixsderivedcoeffs)
{
  //double phixs_threshold_ev = (epsilon(element, upperion, upperlevel) - epsilon(element, lowerion, lowerlevel)) / EV;
  globals::elements[element].ions[lowerion].levels[lowerlevel].phixs_threshold = phixs_threshold_ev * EV;
  if (upperlevel_in >= 0) // file gives photoionisation to a single target state only
  {
    int upperlevel = upperlevel_in - groundstate_index_in;
    assert(upperlevel >= 0);
    globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets = 1;
    mem_usage_phixs += sizeof(phixstarget_entry);
    if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets = (phixstarget_entry *) calloc(1, sizeof(phixstarget_entry))) == NULL)
    {
      printout("[fatal] input: not enough memory to initialize phixstargets... abort\n");
      abort();
    }
    if (single_level_top_ion && (upperion == get_nions(element) - 1)) // top ion has only one level, so send it to that level
      upperlevel = 0;
    globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].levelindex = upperlevel;
    globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].probability = 1.0;
  }
  else // upperlevel < 0, indicating that a table of upper levels and their probabilities will follow
  {
    int in_nphixstargets;
    fscanf(phixsdata,"%d\n", &in_nphixstargets);
    assert(in_nphixstargets >= 0);
    // read in a table of target states and probabilities and store them
    if (!single_level_top_ion || upperion < get_nions(element) - 1) // in case the top ion has nlevelsmax = 1
    {
      globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets = in_nphixstargets;
      mem_usage_phixs += in_nphixstargets * sizeof(phixstarget_entry);
      if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets = (phixstarget_entry *) calloc(in_nphixstargets, sizeof(phixstarget_entry))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize phixstargets list... abort\n");
        abort();
      }
      double probability_sum = 0.;
      for (int i = 0; i < in_nphixstargets; i++)
      {
        double phixstargetprobability;
        fscanf(phixsdata, "%d %lg\n", &upperlevel_in, &phixstargetprobability);
        const int upperlevel = upperlevel_in - groundstate_index_in;
        assert(upperlevel >= 0);
        assert(phixstargetprobability > 0);
        globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[i].levelindex = upperlevel;
        globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[i].probability = phixstargetprobability;
        probability_sum += phixstargetprobability;
      }
      if (fabs(probability_sum - 1.0) > 0.01)
      {
        printout("WARNING: photoionisation table for Z=%d ionstage %d has probabilities that sum to %g",
                 get_element(element), get_ionstage(element, lowerion), probability_sum);
      }
    }
    else // file has table of target states and probabilities but our top ion is limited to one level
    {
      globals::elements[element].ions[lowerion].levels[lowerlevel].nphixstargets = 1;
      mem_usage_phixs += sizeof(phixstarget_entry);
      if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets = (phixstarget_entry *) calloc(1, sizeof(phixstarget_entry))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize phixstargets... abort\n");
        abort();
      }
      for (int i = 0; i < in_nphixstargets; i++)
      {
        double phixstargetprobability;
        fscanf(phixsdata, "%d %lg\n", &upperlevel_in, &phixstargetprobability);
      }
      // send it to the ground state of the top ion
      globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].levelindex = 0;
      globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[0].probability = 1.0;
    }
  }

  /// The level contributes to the ionisinglevels if its energy
  /// is below the ionisiation potential and the level doesn't
  /// belong to the topmost ion included.
  /// Rate coefficients are only available for ionising levels.
  //  also need (levelenergy < ionpot && ...)?
  if (lowerion < get_nions(element) - 1) ///thats only an option for pure LTE && level < TAKE_N_BFCONTINUA)
  {
    for (int phixstargetindex = 0; phixstargetindex < get_nphixstargets(element, lowerion, lowerlevel); phixstargetindex++)
    {
      const int upperlevel = get_phixsupperlevel(element, lowerion, lowerlevel, phixstargetindex);
      if (upperlevel > get_maxrecombininglevel(element, lowerion + 1))
        globals::elements[element].ions[lowerion + 1].maxrecombininglevel = upperlevel;

      mem_usage_phixsderivedcoeffs += TABLESIZE * sizeof(double);
      if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[phixstargetindex].spontrecombcoeff = (double *) calloc(TABLESIZE, sizeof(double))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize spontrecombcoeff table for element %d, ion %d, level %d\n",element,lowerion,lowerlevel);
        abort();
      }
      #if (!NO_LUT_PHOTOION)
      mem_usage_phixsderivedcoeffs += TABLESIZE * sizeof(double);
      if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[phixstargetindex].corrphotoioncoeff = (double *) calloc(TABLESIZE, sizeof(double))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize photoioncoeff table for element %d, ion %d, level %d\n",element,lowerion,lowerlevel);
        abort();
      }
      #endif
      #if (!NO_LUT_BFHEATING)
      mem_usage_phixsderivedcoeffs += TABLESIZE * sizeof(double);
      if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[phixstargetindex].bfheating_coeff = (double *) calloc(TABLESIZE, sizeof(double))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize modified_photoioncoeff table for element %d, ion %d, level %d\n",element,lowerion,lowerlevel);
        abort();
      }
      #endif
      mem_usage_phixsderivedcoeffs += TABLESIZE * sizeof(double);
      if ((globals::elements[element].ions[lowerion].levels[lowerlevel].phixstargets[phixstargetindex].bfcooling_coeff = (double *) calloc(TABLESIZE, sizeof(double))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize bfcooling table for element %d, ion %d, level %d\n",element,lowerion,lowerlevel);
        abort();
      }
    }
  }

  mem_usage_phixs += globals::NPHIXSPOINTS * sizeof(float);
  if ((globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs = (float *) calloc(globals::NPHIXSPOINTS, sizeof(float))) == NULL)
  {
    printout("[fatal] input: not enough memory to initialize photoion_xslist... abort\n");
    abort();
  }
  for (int i = 0; i < globals::NPHIXSPOINTS; i++)
  {
    float phixs;
    fscanf(phixsdata,"%g\n", &phixs);
    assert(phixs >= 0);

    ///the photoionisation cross-sections in the database are given in Mbarn = 1e6 * 1e-28m^2
    ///to convert to cgs units multiply by 1e-18
    globals::elements[element].ions[lowerion].levels[lowerlevel].photoion_xs[i] = phixs * 1e-18;
    //fprintf(database_file,"%g %g\n", nutable[i], phixstable[i]);
  }

  //nbfcontinua++;
  //printout("[debug] element %d, ion %d, level %d: phixs exists %g\n",element,lowerion,lowerlevel,phixs*1e-18);
  globals::nbfcontinua += get_nphixstargets(element, lowerion, lowerlevel);
  if (lowerlevel < get_nlevels_groundterm(element, lowerion))
    globals::nbfcontinua_ground += get_nphixstargets(element, lowerion, lowerlevel);
}


static void read_phixs_data(void)
{
  globals::nbfcontinua_ground = 0;
  globals::nbfcontinua = 0;
  long mem_usage_phixs = 0;
  long mem_usage_phixsderivedcoeffs = 0;
  printout("readin phixs data\n");

  FILE *phixsdata = fopen_required("phixsdata_v2.txt", "r");

  fscanf(phixsdata,"%d\n",&globals::NPHIXSPOINTS);
  assert(globals::NPHIXSPOINTS > 0);
  fscanf(phixsdata,"%lg\n",&globals::NPHIXSNUINCREMENT);
  assert(globals::NPHIXSNUINCREMENT > 0.);
  int Z;
  int upperionstage;
  int upperlevel_in;
  int lowerionstage;
  int lowerlevel_in;
  double phixs_threshold_ev;
  while (fscanf(phixsdata,"%d %d %d %d %d %lg\n",&Z,&upperionstage,&upperlevel_in,&lowerionstage,&lowerlevel_in,&phixs_threshold_ev) != EOF)
  {
    assert(Z > 0);
    assert(upperionstage >= 2);
    assert(lowerionstage >= 1);
    bool skip_this_phixs_table = false;
    //printout("[debug] Z %d, upperion %d, upperlevel %d, lowerion %d, lowerlevel, %d\n",Z,upperion,upperlevel,lowerion,lowerlevel);
    /// translate readin anumber to element index
    const int element = get_elementindex(Z);

    /// store only photoionization crosssections for elements that are part of the current model atom
    if (element >= 0)
    {
      /// translate readin ionstages to ion indices

      const int upperion = upperionstage - get_ionstage(element, 0);
      const int lowerion = lowerionstage - get_ionstage(element, 0);
      const int lowerlevel = lowerlevel_in - groundstate_index_in;
      assert(lowerionstage >= 0);
      assert(lowerlevel >= 0);
      /// store only photoionization crosssections for ions that are part of the current model atom
      if (lowerion >= 0 && lowerlevel < get_nlevels(element, lowerion) && upperion < get_nions(element))
      {
        read_phixs_data_table(phixsdata, element, lowerion, lowerlevel, upperion, upperlevel_in, phixs_threshold_ev, &mem_usage_phixs, &mem_usage_phixsderivedcoeffs);

        skip_this_phixs_table = false;
      }
      else
      {
        skip_this_phixs_table = true;
      }
    }
    else
    {
      skip_this_phixs_table = true;
    }

    if (skip_this_phixs_table) // for ions or elements that are not part of the current model atom, proceed through the lines and throw away the data
    {
      if (upperlevel_in < 0) // a table of target states and probabilities will follow, so read past those lines
      {
        int nphixstargets;
        fscanf(phixsdata, "%d\n", &nphixstargets);
        for (int i = 0; i < nphixstargets; i++)
        {
          double phixstargetprobability;
          fscanf(phixsdata, "%d %lg\n", &upperlevel_in, &phixstargetprobability);
        }
      }
      for (int i = 0; i < globals::NPHIXSPOINTS; i++) //skip through cross section list
      {
        float phixs;
        fscanf(phixsdata, "%g\n", &phixs);
      }
    }
  }

  fclose(phixsdata);
  printout("mem_usage: photoionisation tables occupy %.1f MB\n", mem_usage_phixs / 1024. / 1024.);
  printout("mem_usage: lookup tables derived from photoionisation (spontrecombcoeff, bfcooling and corrphotoioncoeff/bfheating if enabled) occupy %.1f MB\n",
           mem_usage_phixsderivedcoeffs / 1024. / 1024.);
}


static void read_ion_levels(
  FILE* adata, const int element, const int ion, const int nions, const int nlevels, const int nlevelsmax,
  const double energyoffset, const double ionpot)
{
  for (int level = 0; level < nlevels; level++)
  {
    int levelindex_in;
    double levelenergy;
    double statweight;
    int ntransitions;
    fscanf(adata, "%d %lg %lg %d%*[^\n]\n", &levelindex_in, &levelenergy, &statweight, &ntransitions);
    assert(levelindex_in == level + groundstate_index_in);
    // assert((ion < nions - 1) || (ntransitions > 0) || (nlevels == 1));
    //if (element == 1 && ion == 0) printf("%d %16.10f %g %d\n",levelindex,levelenergy,statweight,ntransitions);
    if (level < nlevelsmax)
    {
      //globals::elements[element].ions[ion].levels[level].epsilon = (energyoffset + levelenergy) * EV;
      const double currentlevelenergy = (energyoffset + levelenergy) * EV;
      //if (element == 1 && ion == 0) printf("%d %16.10e\n",levelindex,currentlevelenergy);
      //printout("energy for level %d of ionstage %d of element %d is %g\n",level,ionstage,element,currentlevelenergy/EV);
      globals::elements[element].ions[ion].levels[level].epsilon = currentlevelenergy;
      //printout("epsilon(%d,%d,%d)=%g",element,ion,level,globals::elements[element].ions[ion].levels[level].epsilon);

      //if (level == 0 && ion == 0) energyoffset = levelenergy;
      globals::elements[element].ions[ion].levels[level].stat_weight = statweight;
      ///Moved to the section with ionising levels below
      //globals::elements[element].ions[ion].levels[level].cont_index = cont_index;
      //cont_index--;
      /// Initialise the metastable flag to true. Set it to false if a downward transition exists.
      globals::elements[element].ions[ion].levels[level].metastable = true;
      //globals::elements[element].ions[ion].levels[level].main_qn = mainqn;

      /// The level contributes to the ionisinglevels if its energy
      /// is below the ionization potential and the level doesn't
      /// belong to the topmost ion included.
      /// Rate coefficients are only available for ionising levels.
      if (levelenergy < ionpot && ion < nions - 1) ///thats only an option for pure LTE && level < TAKE_N_BFCONTINUA)
      {
        globals::elements[element].ions[ion].ionisinglevels++;
      }


      /// store the possible downward transitions from the current level in following order to memory
      ///     A_level,level-1; A_level,level-2; ... A_level,1
      /// entries which are not explicitly set are zero (the zero is set/initialized by calloc!)
      if ((transitions[level].to = (int *) calloc(level, sizeof(int))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize transitionlist ... abort\n");
        abort();
      }
      for (int i = 0; i < level; i++)
      {
        transitions[level].to[i] = -99.;
      }

      globals::elements[element].ions[ion].levels[level].downtrans_lineindicies = NULL;

      /// initialize number of downward transitions to zero
      set_ndowntrans(element, ion, level, 0);

      globals::elements[element].ions[ion].levels[level].uptrans_lineindicies = NULL;

      /// initialize number of upward transitions to zero
      set_nuptrans(element, ion, level, 0);
    }
    else
    {
      // globals::elements[element].ions[ion].levels[nlevelsmax - 1].stat_weight += statweight;
    }
  }
}


static transitiontable_entry *read_ion_transitions(
  FILE *transitiondata, const int tottransitions_in,
  int *tottransitions, transitiontable_entry *transitiontable,
  const int nlevels_requiretransitions, const int nlevels_requiretransitions_upperlevels,
  const int Z, const int ionstage)
{
  if (*tottransitions == 0)
  {
    for (int i = 0; i < tottransitions_in; i++)
    {
      double A, coll_str;
      int lower,upper,intforbidden;
      fscanf(transitiondata, "%d %d %lg %lg %d\n", &lower, &upper, &A, &coll_str, &intforbidden);
      //printout("index %d, lower %d, upper %d, A %g\n",transitionindex,lower,upper,A);
    }
  }
  else
  {
    int prev_upper = -1;
    int prev_lower = 0;
    for (int i = 0; i < *tottransitions; i++)
    {
      int lower_in;
      int upper_in;
      double A;
      double coll_str;
      int intforbidden;
      fscanf(transitiondata, "%d %d %lg %lg %d\n", &lower_in, &upper_in, &A, &coll_str, &intforbidden);
      const int lower = lower_in - groundstate_index_in;
      const int upper = upper_in - groundstate_index_in;
      assert(lower >= 0);
      assert(upper >= 0);

      // this entire block can be removed if we don't want to add in extra collisonal
      // transitions between levels
      if (prev_lower < nlevels_requiretransitions)
      {
        int stoplevel;
        if (lower == prev_lower && upper > prev_upper + 1)
        {
          // same lower level, but some upper levels were skipped over
          stoplevel = upper - 1;
          if (stoplevel >= nlevels_requiretransitions_upperlevels)
            stoplevel = nlevels_requiretransitions_upperlevels - 1;
        }
        else if ((lower > prev_lower) && prev_upper < (nlevels_requiretransitions_upperlevels - 1))
        {
          // we've moved onto another lower level, but the previous one was missing some required transitions
          stoplevel = nlevels_requiretransitions_upperlevels - 1;
        }
        else
        {
          stoplevel = -1;
        }

        for (int tmplevel = prev_upper + 1; tmplevel <= stoplevel; tmplevel++)
        {
          if (tmplevel == prev_lower)
            continue;
          // printout("+adding transition index %d Z=%02d ionstage %d lower %d upper %d\n", i, Z, ionstage, prev_lower, tmplevel);
          (*tottransitions)++;
          transitiontable = (transitiontable_entry *) realloc(transitiontable, *tottransitions * sizeof(transitiontable_entry));
          if (transitiontable == NULL)
          {
            printout("Could not reallocate transitiontable\n");
            abort();
          }
          assert(prev_lower >= 0);
          assert(tmplevel >= 0);
          transitiontable[i].lower = prev_lower;
          transitiontable[i].upper = tmplevel;
          transitiontable[i].A = 0.;
          transitiontable[i].coll_str = -2.;
          transitiontable[i].forbidden = true;
          i++;
        }
      }

      transitiontable[i].lower = lower;
      transitiontable[i].upper = upper;
      transitiontable[i].A = A;
      transitiontable[i].coll_str = coll_str;
      transitiontable[i].forbidden = (intforbidden == 1);
      //printout("index %d, lower %d, upper %d, A %g\n",transitionindex,lower,upper,A);
      // printout("reading transition index %d lower %d upper %d\n", i, transitiontable[i].lower, transitiontable[i].upper);
      prev_lower = lower;
      prev_upper = upper;
    }
  }

  return transitiontable;
}


static int compare_linelistentry(const void *p1, const void *p2)
/// Helper function to sort the linelist by frequency.
{
  linelist_entry *a1 = (linelist_entry *)(p1);
  linelist_entry *a2 = (linelist_entry *)(p2);
  //printf("%d %d %d %d %g\n",a1->elementindex,a1->ionindex,a1->lowerlevelindex,a1->upperlevelindex,a1->nu);
  //printf("%d %d %d %d %g\n",a2->elementindex,a2->ionindex,a2->lowerlevelindex,a2->upperlevelindex,a2->nu);
  //printf("%g\n",a2->nu - a1->nu);
  if (fabs(a2->nu - a1->nu) < (1.e-10 * a1->nu))
  {
    a2->nu = a1->nu;
    if (a1->lowerlevelindex > a2->lowerlevelindex)
    {
      return -1;
    }
    else if (a1->lowerlevelindex < a2->lowerlevelindex)
    {
      return 1;
    }
    else if (a1->upperlevelindex > a2->upperlevelindex)
    {
      return -1;
    }
    else if (a1->upperlevelindex < a2->upperlevelindex)
    {
      return 1;
    }
    else
    {
      printout("Duplicate atomic line?\n");
      printout(
        "Z=%d ionstage %d lower %d upper %d nu %g\n",
        get_element(a1->elementindex),
        get_ionstage(a1->elementindex, a1->ionindex),
        a1->lowerlevelindex,
        a1->upperlevelindex,
        a1->nu);
      printout(
        "Z=%d ionstage %d lower %d upper %d nu %g\n",
        get_element(a2->elementindex),
        get_ionstage(a2->elementindex, a2->ionindex),
        a2->lowerlevelindex,
        a2->upperlevelindex,
        a2->nu);
      return 0;
    }
  }
  else
  {
    if ((a1->nu < a2->nu) || (a1->nu == a2->nu))
      return 1;
    else if (a1->nu > a2->nu)
      return -1;
    else
      return 0;
  }
}


static int transitioncheck(const int upper, const int lower)
{
  const int index = (upper - lower) - 1;
  const int flag = transitions[upper].to[index];

  return flag;
}


static void add_transitions_to_linelist(
  const int element, const int ion, const int nlevelsmax, const int tottransitions,
  transitiontable_entry *transitiontable, int *lineindex)
{
  for (int ii = 0; ii < tottransitions; ii++)
  {
    // if (get_element(element) == 28 && get_ionstage(element, ion) == 2)
    // {
    //   printout("Disabling coll_str value of %g\n", transitiontable[ii].coll_str);
    //   if (transitiontable[ii].forbidden)
    //     transitiontable[ii].coll_str = -2.;
    //   else
    //     transitiontable[ii].coll_str = -1.;
    // }

    const int level = transitiontable[ii].upper;
    const int targetlevel = transitiontable[ii].lower;
    assert(targetlevel >= 0);
    assert(level > targetlevel);
    const double nu_trans = (epsilon(element, ion, level) - epsilon(element, ion, targetlevel)) / H;
    if (targetlevel < nlevelsmax && level < nlevelsmax && nu_trans > 0.)
    {

      //if (level == transitiontable[ii].upper && level-i-1 == transitiontable[ii].lower)
      //{
      //printout("ii %d\n",ii);
      //printout("transtable upper %d, lower %d, A %g, iii %d\n",transitiontable[ii].upper,transitiontable[ii].lower, transitiontable[ii].A,iii);
      /// Make sure that we don't allow duplicate. In that case take only the lines
      /// first occurrence
      if (transitioncheck(level, targetlevel) == -99)
      {
        transitions[level].to[level - targetlevel - 1] = *lineindex;
        const double A_ul = transitiontable[ii].A;
        const double coll_str = transitiontable[ii].coll_str;
        //globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].einstein_A = A_ul;

        const double g = stat_weight(element,ion,level) / stat_weight(element,ion,targetlevel);
        const double f_ul = g * ME * pow(CLIGHT,3) / (8 * pow(QE * nu_trans * PI, 2)) * A_ul;
        //f_ul = g * OSCSTRENGTHCONVERSION / pow(nu_trans,2) * A_ul;
        //globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].oscillator_strength = g * ME*pow(CLIGHT,3)/(8*pow(QE*nu_trans*PI,2)) * A_ul;

        //printout("lineindex %d, element %d, ion %d, lower %d, upper %d, nu %g\n",*lineindex,element,ion,level-i-1,level,nu_trans);
        globals::linelist[*lineindex].elementindex = element;
        globals::linelist[*lineindex].ionindex = ion;
        globals::linelist[*lineindex].lowerlevelindex = targetlevel;
        globals::linelist[*lineindex].upperlevelindex = level;
        globals::linelist[*lineindex].nu = nu_trans;
        globals::linelist[*lineindex].einstein_A = A_ul;
        globals::linelist[*lineindex].osc_strength = f_ul;
        globals::linelist[*lineindex].coll_str = coll_str;
        globals::linelist[*lineindex].forbidden = transitiontable[ii].forbidden;
        (*lineindex)++;
        if (*lineindex % MLINES == 0)
        {
          printout("[info] read_atomicdata: increase linelistsize from %d to %d\n", *lineindex, *lineindex + MLINES);
          if ((globals::linelist = (linelist_entry *) realloc(globals::linelist, (*lineindex + MLINES) * sizeof(linelist_entry))) == NULL)
          {
            printout("[fatal] input: not enough memory to reallocate linelist ... abort\n");
            abort();
          }
        }

        /// This is not a metastable level.
        globals::elements[element].ions[ion].levels[level].metastable = false;

        const int nupperdowntrans = get_ndowntrans(element, ion, level) + 1;
        set_ndowntrans(element, ion, level, nupperdowntrans);
        if ((globals::elements[element].ions[ion].levels[level].downtrans_lineindicies
            = (int *) realloc(globals::elements[element].ions[ion].levels[level].downtrans_lineindicies, nupperdowntrans * sizeof(int))) == NULL)
        {
          printout("[fatal] input: not enough memory to reallocate downtranslist ... abort\n");
          abort();
        }
        // the line list has not been sorted yet, so the store the negative level index for now and
        // this will be replaced with the index into the sorted line list later
        globals::elements[element].ions[ion].levels[level].downtrans_lineindicies[nupperdowntrans-1] = -targetlevel;

        const int nloweruptrans = get_nuptrans(element, ion, targetlevel) + 1;
        set_nuptrans(element, ion, targetlevel, nloweruptrans);
        if ((globals::elements[element].ions[ion].levels[targetlevel].uptrans_lineindicies
            = (int *) realloc(globals::elements[element].ions[ion].levels[targetlevel].uptrans_lineindicies, nloweruptrans * sizeof(int))) == NULL)
        {
          printout("[fatal] input: not enough memory to reallocate uptranslist ... abort\n");
          abort();
        }
        globals::elements[element].ions[ion].levels[targetlevel].uptrans_lineindicies[nloweruptrans-1] = -level;
      }
      else
      {
        // This is a new branch to deal with lines that have different types of transition. It should trip after a transition is already known.
        const int linelistindex = transitions[level].to[level - targetlevel - 1];
        const double A_ul = transitiontable[ii].A;
        const double coll_str = transitiontable[ii].coll_str;
        //globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].einstein_A = A_ul;

        const double g = stat_weight(element, ion, level) / stat_weight(element, ion, targetlevel);
        const double f_ul = g * ME * pow(CLIGHT,3) / (8 * pow(QE * nu_trans * PI, 2)) * A_ul;
        //f_ul = g * OSCSTRENGTHCONVERSION / pow(nu_trans,2) * A_ul;
        //globals::elements[element].ions[ion].levels[level].transitions[level-targetlevel-1].oscillator_strength = g * ME*pow(CLIGHT,3)/(8*pow(QE*nu_trans*PI,2)) * A_ul;

        if ((globals::linelist[linelistindex].elementindex != element) || (globals::linelist[linelistindex].ionindex != ion) || (globals::linelist[linelistindex].upperlevelindex != level) || (globals::linelist[linelistindex].lowerlevelindex != targetlevel))
        {
          printout("[input.c] Failure to identify level pair for duplicate bb-transition ... going to abort now\n");
          printout("[input.c]   element %d ion %d targetlevel %d level %d\n", element, ion, targetlevel, level);
          printout("[input.c]   transitions[level].to[level-targetlevel-1]=linelistindex %d\n", transitions[level].to[level - targetlevel - 1]);
          printout("[input.c]   A_ul %g, coll_str %g\n", A_ul, coll_str);
          printout("[input.c]   globals::linelist[linelistindex].elementindex %d, globals::linelist[linelistindex].ionindex %d, globals::linelist[linelistindex].upperlevelindex %d, globals::linelist[linelistindex].lowerlevelindex %d\n", globals::linelist[linelistindex].elementindex, globals::linelist[linelistindex].ionindex, globals::linelist[linelistindex].upperlevelindex,globals::linelist[linelistindex].lowerlevelindex);
          abort();
        }
        globals::linelist[linelistindex].einstein_A += A_ul;
        globals::linelist[linelistindex].osc_strength += f_ul;
        if (coll_str > globals::linelist[linelistindex].coll_str)
        {
          globals::linelist[linelistindex].coll_str = coll_str;
        }
      }
    }
  }
}


static int get_lineindex(const int lelement, const int lion, const int llowerlevel, const int lupperlevel)
{
  for (int lineindex = 0; lineindex < globals::nlines; lineindex++)
  {
   const int element = globals::linelist[lineindex].elementindex;
   const int ion = globals::linelist[lineindex].ionindex;
   const int lowerlevel = globals::linelist[lineindex].lowerlevelindex;
   const int upperlevel = globals::linelist[lineindex].upperlevelindex;

   if (lelement == element && lion == ion && llowerlevel == lowerlevel && lupperlevel == upperlevel)
   {
     return lineindex;
   }
  }
  assert(false);
}


static int calculate_nlevels_groundterm(int element, int ion)
{
  const int nlevels = get_nlevels(element, ion);
  if (nlevels == 1)
  {
    return 1;
  }

  int nlevels_groundterm = 1;
  // detect single-level ground term
  const double endiff10 = epsilon(element, ion, 1) - epsilon(element, ion, 0);
  const double endiff21 = epsilon(element, ion, 2) - epsilon(element, ion, 1);
  if (endiff10 > 2. * endiff21)
  {
    nlevels_groundterm = 1;
  }
  else
  {
    for (int level = 1; level < nlevels - 2; level++)
    {
      const double endiff1 = epsilon(element, ion, level) - epsilon(element, ion, level - 1);
      const double endiff2 = epsilon(element, ion, level + 1) - epsilon(element, ion, level);
      if (endiff2 > 2. * endiff1)
      {
        nlevels_groundterm = level + 1;
        break;
      }
    }
  }

  for (int level = 0; level < nlevels_groundterm; level++)
  {
    const int g = stat_weight(element, ion, level);
    for (int levelb = 0; levelb < level; levelb++)
    {
      // there should be no duplicate stat weights within the ground term
      const int g_b = stat_weight(element, ion, levelb);
      if (g == g_b)
      {
        printout("ERROR: duplicate g value in ground term for Z=%d ion_stage %d nlevels_groundterm %d g(level %d) %d g(level %d) %d\n",
                 get_element(element), get_ionstage(element, ion), nlevels_groundterm, level, g, levelb, g_b);
      }
    }
  }

  return nlevels_groundterm;
}


static void read_atomicdata_files(void)
{
  radfield::jblue_init();
  int totaluptrans = 0;
  int totaldowntrans = 0;

  ///open atomic data file
  FILE *compositiondata = fopen_required("compositiondata.txt", "r");

  FILE *adata = fopen_required("adata.txt", "r");

  /// initialize atomic data structure to number of elements
  int nelements_in;
  fscanf(compositiondata,"%d", &nelements_in);
  if (nelements_in > MELEMENTS)
  {
    printout("ERROR: nelements_in = %d > %d MELEMENTS", get_nelements(), MELEMENTS);
    abort();
  }
  set_nelements(nelements_in);
  if ((globals::elements = (elementlist_entry *) calloc(get_nelements(), sizeof(elementlist_entry))) == NULL)
  {
    printout("[fatal] input: not enough memory to initialize elementlist ... abort\n");
    abort();
  }
  //printout("elements initialized\n");

  /// Initialize the linelist
  if ((globals::linelist = (linelist_entry *) calloc(MLINES, sizeof(linelist_entry))) == NULL)
  {
    printout("[fatal] input: not enough memory to initialize linelist ... abort\n");
    abort();
  }

  /// temperature to determine relevant ionstages
  int T_preset;
  fscanf(compositiondata,"%d",&T_preset);
  int homogeneous_abundances_in;
  fscanf(compositiondata,"%d",&homogeneous_abundances_in);
  globals::homogeneous_abundances = (homogeneous_abundances_in != 0);
  if (globals::homogeneous_abundances)
    printout("[info] read_atomicdata: homogeneous abundances as defined in compositiondata.txt are active\n");

  /// open transition data file
  FILE *transitiondata = fopen_required("transitiondata.txt", "r");

  int lineindex = 0;  ///counter to determine the total number of lines, initialisation
  int uniqueionindex = -1; // index into list of all ions of all elements

  /// readin
  int nbfcheck = 0;
  int heatingcheck = 0;
  int coolingcheck = 0;
  for (int element = 0; element < get_nelements(); element++)
  {
    /// read information about the next element which should be stored to memory
    int Z;
    int nions;
    int lowermost_ionstage;
    int uppermost_ionstage;
    int nlevelsmax_readin;
    double abundance;
    double mass_amu;
    fscanf(compositiondata,"%d %d %d %d %d %lg %lg", &Z, &nions, &lowermost_ionstage, &uppermost_ionstage, &nlevelsmax_readin, &abundance, &mass_amu);
    printout("readin compositiondata: next element Z %d, nions %d, lowermost %d, uppermost %d, nlevelsmax %d\n",Z,nions,lowermost_ionstage,uppermost_ionstage,nlevelsmax_readin);
    assert(Z > 0);
    assert(nions > 0);
    assert(nions == uppermost_ionstage - lowermost_ionstage + 1);
    assert(abundance >= 0);
    assert(mass_amu >= 0);

    /// write this element's data to memory
    assert(nions <= globals::maxion);
    globals::elements[element].anumber = Z;
    globals::elements[element].nions = nions;
    globals::elements[element].abundance = abundance;       /// abundances are expected to be given by mass
    globals::elements[element].mass = mass_amu * MH;
    globals::elements_uppermost_ion[tid][element] = nions - 1;
    globals::includedions += nions;

    /// Initialize the elements ionlist
    if ((globals::elements[element].ions = (ionlist_entry *) calloc(nions, sizeof(ionlist_entry))) == NULL)
    {
        printout("[fatal] input: not enough memory to initialize ionlist ... abort\n");
        abort();
    }

    /// now read in data for all ions of the current element. before doing so initialize
    /// energy scale for the current element (all level energies are stored relative to
    /// the ground level of the neutral ion)
    double energyoffset = 0.;
    double ionpot = 0.;
    for (int ion = 0; ion < nions; ion++)
    {
      uniqueionindex++;
      int nlevelsmax = nlevelsmax_readin;
      printout("element %d ion %d\n", element, ion);
      /// calculate the current levels ground level energy
      energyoffset += ionpot;

      /// read information for the elements next ionstage
      int adata_Z_in = -1;
      int ionstage = -1;
      int nlevels = 0;
      // fscanf(adata,"%d %d %d %lg\n",&adata_Z_in,&ionstage,&nlevels,&ionpot);
      while (adata_Z_in != Z || ionstage != lowermost_ionstage + ion) // skip over this ion block
      {
        if (adata_Z_in == Z)
        {
          printout("increasing energyoffset by ionpot %g\n", ionpot);
          energyoffset += ionpot;
        }
        for (int i = 0; i < nlevels; i++)
        {
          double levelenergy;
          double statweight;
          int levelindex;
          int ntransitions;
          fscanf(adata, "%d %lg %lg %d%*[^\n]\n", &levelindex, &levelenergy, &statweight, &ntransitions);
        }

        const int fscanfadata = fscanf(adata, "%d %d %d %lg\n", &adata_Z_in, &ionstage, &nlevels, &ionpot);

        if (fscanfadata == EOF)
        {
          printout("End of file in adata not expected");
          abort();
        }
      }

      printout("adata header matched: Z %d, ionstage %d, nlevels %d\n", adata_Z_in, ionstage, nlevels);

      if (single_level_top_ion && ion == nions - 1) // limit the top ion to one level and no transitions
      {
        nlevelsmax = 1;
      }

      // if (adata_Z_in == 26 && ionstage == 1)
      // {
      //   nlevelsmax = 5;
      // }
      // else if (adata_Z_in == 26 && ionstage == 2)
      // {
      //   nlevelsmax = 5;
      // }

      if (nlevelsmax < 0)
      {
        nlevelsmax = nlevels;
      }
      else if (nlevels >= nlevelsmax)
      {
        printout("[info] read_atomicdata: reduce number of levels from %d to %d for ion %d of element %d\n", nlevels, nlevelsmax, ion, element);
      }
      else
      {
        printout("[warning] read_atomicdata: requested nlevelsmax=%d > nlevels=%d for ion %d of element %d ... reduced nlevelsmax to nlevels\n",
                 nlevelsmax, nlevels, ion, element);
        nlevelsmax = nlevels;
      }

      /// and proceed through the transitionlist till we match this ionstage (if it was not the neutral one)
      int transdata_Z_in = -1;
      int transdata_ionstage_in = -1;
      int tottransitions_in = 0;
      while (transdata_Z_in != Z || transdata_ionstage_in != ionstage)
      {
        for (int i = 0; i < tottransitions_in; i++)
        {
          int lower;
          int upper;
          double A;
          double coll_str;
          int intforbidden;
          fscanf(transitiondata, "%d %d %lg %lg %d\n", &lower, &upper, &A, &coll_str, &intforbidden);
        }
        const int readtransdata = fscanf(transitiondata,"%d %d %d", &transdata_Z_in, &transdata_ionstage_in, &tottransitions_in);
        if (readtransdata == EOF)
        {
          printout("End of file in transition data");
          abort();
        }
      }

      printout("transdata header matched: transdata_Z_in %d, transdata_ionstage_in %d, tottransitions %d\n",
               transdata_Z_in, transdata_ionstage_in, tottransitions_in);
      assert(tottransitions_in >= 0);

      int tottransitions = tottransitions_in;

      if (single_level_top_ion && ion == nions - 1) // limit the top ion to one level and no transitions
      {
        tottransitions = 0;
      }

      assert(transdata_Z_in == Z);
      assert(transdata_ionstage_in == ionstage);

      /// read in the level and transition data for this ion
      transitiontable_entry *transitiontable = (transitiontable_entry *) calloc(tottransitions, sizeof(transitiontable_entry));

      /// load transition table for the CURRENT ion to temporary memory
      if (transitiontable == NULL)
      {
        if (tottransitions > 0)
        {
          printout("[fatal] input: not enough memory to initialize transitiontable ... abort\n");
          abort();
        }
      }
      // first <nlevels_requiretransitions> levels will be collisionally
      // coupled to the first <nlevels_requiretransitions_upperlevels> levels (assumed forbidden)
      // use 0 to disable adding extra transitions
      int nlevels_requiretransitions;
      int nlevels_requiretransitions_upperlevels;
      if (((Z == 26 || Z == 28) && ionstage >= 1))
      {
        nlevels_requiretransitions = 80;
        nlevels_requiretransitions_upperlevels = nlevelsmax;
      }
      else
      {
        nlevels_requiretransitions = 0;
        nlevels_requiretransitions_upperlevels = nlevelsmax; // no effect if previous line is zero
      }
      if (nlevels_requiretransitions > nlevelsmax)
        nlevels_requiretransitions = nlevelsmax;
      if (nlevels_requiretransitions_upperlevels > nlevelsmax)
        nlevels_requiretransitions_upperlevels = nlevelsmax;

      transitiontable = read_ion_transitions(transitiondata, tottransitions_in, &tottransitions, transitiontable,
        nlevels_requiretransitions, nlevels_requiretransitions_upperlevels, Z, ionstage);

      /// store the ions data to memory and set up the ions zeta and levellist
      globals::elements[element].ions[ion].ionstage = ionstage;
      globals::elements[element].ions[ion].nlevels = nlevelsmax;
      globals::elements[element].ions[ion].ionisinglevels = 0;
      globals::elements[element].ions[ion].maxrecombininglevel = 0;
      globals::elements[element].ions[ion].ionpot = ionpot * EV;
      globals::elements[element].ions[ion].nlevels_groundterm = 0;
      globals::elements[element].ions[ion].uniqueionindex = uniqueionindex;

//           if ((globals::elements[element].ions[ion].zeta = calloc(TABLESIZE, sizeof(float))) == NULL)
//           {
//             printout("[fatal] input: not enough memory to initialize zetalist for element %d, ion %d ... abort\n",element,ion);
//             abort();
//           }
      if ((globals::elements[element].ions[ion].Alpha_sp = (float *) calloc(TABLESIZE, sizeof(float))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize Alpha_sp list for element %d, ion %d ... abort\n",element,ion);
        abort();
      }
      if ((globals::elements[element].ions[ion].levels = (levellist_entry *) calloc(nlevelsmax, sizeof(levellist_entry))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize level list of element %d, ion %d ... abort\n",element,ion);
        abort();
      }


      /// now we need to readout the data for all those levels, write them to memory
      /// and set up the list of possible transitions for each level
      if ((transitions = (transitions_t *) calloc(nlevelsmax, sizeof(transitions_t))) == NULL)
      {
        printout("[fatal] input: not enough memory to allocate transitions ... abort\n");
        abort();
      }

      read_ion_levels(adata, element, ion, nions, nlevels, nlevelsmax, energyoffset, ionpot);

      add_transitions_to_linelist(element, ion, nlevelsmax, tottransitions, transitiontable, &lineindex);

      //printf("A %g\n",globals::elements[element].ions[ion].levels[level].transitions[i].einstein_A );
      //printout("%d -> %d has A %g\n",level,level-i-1,globals::elements[element].ions[ion].levels[level].transitions[i].einstein_A );

      for (int level = 0; level < nlevelsmax; level++)
      {
        totaldowntrans += get_ndowntrans(element, ion, level);
        totaluptrans += get_nuptrans(element, ion, level);
        free(transitions[level].to);
      }
      free(transitiontable);
      free(transitions);

      /// Also the phixslist
      if (ion < nions - 1)
      {
//            globals::elements[element].ions[ion].nbfcontinua = globals::elements[element].ions[ion].ionisinglevels;//nlevelsmax;
        nbfcheck += globals::elements[element].ions[ion].ionisinglevels; //nlevelsmax;
/*            if ((globals::elements[element].ions[ion].phixslist = calloc(nlevelsmax, sizeof(ionsphixslist_t))) == NULL)
        {
          printout("[fatal] input: not enough memory to initialize phixslist for element %d, ion %d ... abort\n",element,ion);
          abort();
        }*/
      }
    }
  }
  fclose(adata);
  fclose(transitiondata);
  fclose(compositiondata);
  printout("nbfcheck %d\n",nbfcheck);
  printout("heatingcheck %d\n",heatingcheck);

  /// Save the linecounters value to the global variable containing the number of lines
  globals::nlines = lineindex;
  printout("nlines %d\n", globals::nlines);
  if (globals::nlines > 0)
  {
    /// and release empty memory from the linelist
    if ((globals::linelist = (linelist_entry *) realloc(globals::linelist, globals::nlines * sizeof(linelist_entry))) == NULL)
    {
      printout("[fatal] input: not enough memory to reallocate linelist ... abort\n");
      abort();
    }
    printout("mem_usage: linelist occupies %.1f MB\n", globals::nlines * (sizeof(globals::linelist[0]) + sizeof(&globals::linelist[0])) / 1024. / 1024);
  }

  if (T_preset > 0)
    abort();


  /// Set up the list of allowed upward transitions for each level
  printout("total uptrans %d\n", totaluptrans);
  printout("total downtrans %d\n", totaldowntrans);
  printout("coolingcheck %d\n", coolingcheck);

  printout("mem_usage: transitions occupy %.1f MB\n", (totaluptrans + totaldowntrans) * (sizeof(int)) / 1024. / 1024.);
  ///debug output
  /*
  FILE *linelist_file = fopen_required("linelist_unsorted.out", "w");
  for (i = 0; i < nlines; i++)
    fprintf(linelist_file,"element %d, ion %d, ionstage %d, upperindex %d, lowerindex %d, nu %g\n",linelist[i].elementindex, globals::linelist[i].ionindex, globals::elements[linelist[i].elementindex].ions[linelist[i].ionindex].ionstage, globals::linelist[i].upperlevelindex, globals::linelist[i].lowerlevelindex, globals::linelist[i].nu);
  fclose(linelist_file);
  //abort();
  */

  /// then sort the linelist by decreasing frequency
  qsort(globals::linelist, globals::nlines, sizeof(linelist_entry), compare_linelistentry);

  /// Save sorted linelist into a file
  // if (rank_global == 0)
  // {
  //   FILE *linelist_file = fopen_required("linelist.dat", "w");
  //   fprintf(linelist_file,"%d\n",nlines);
  //   for (int i = 0; i < nlines; i++)
  //   {
  //     fprintf(linelist_file,"%d %d %d %d %d %lg %lg %lg %lg %d\n",
  //             i, globals::linelist[i].elementindex, globals::linelist[i].ionindex,
  //             globals::linelist[i].upperlevelindex, globals::linelist[i].lowerlevelindex,
  //             globals::linelist[i].nu, globals::linelist[i].einstein_A, globals::linelist[i].osc_strength,
  //             globals::linelist[i].coll_str, globals::linelist[i].forbidden);
  //   }
  //   fclose(linelist_file);
  // }


  ///Establish connection between transitions and sorted linelist
  //printout("[debug] init line counter list\n");
  printout("establish connection between transitions and sorted linelist\n");
  for (int lineindex = 0; lineindex < globals::nlines; lineindex++)
  {
    const int element = globals::linelist[lineindex].elementindex;
    const int ion = globals::linelist[lineindex].ionindex;
    const int lowerlevel = globals::linelist[lineindex].lowerlevelindex;
    const int upperlevel = globals::linelist[lineindex].upperlevelindex;

    const int nupperdowntrans = get_ndowntrans(element, ion, upperlevel);
    for (int ii = 0; ii < nupperdowntrans; ii++)
    {
      // negative indicates a level instead of a lineindex
      if (globals::elements[element].ions[ion].levels[upperlevel].downtrans_lineindicies[ii] == -lowerlevel)
      {
        globals::elements[element].ions[ion].levels[upperlevel].downtrans_lineindicies[ii] = lineindex;
        // break; // should be safe to end here if there is max. one transition per pair of levels
      }
    }

    const int nloweruptrans = get_nuptrans(element, ion, lowerlevel);
    for (int ii = 0; ii < nloweruptrans; ii++)
    {
      // negative indicates a level instead of a lineindex
      if (globals::elements[element].ions[ion].levels[lowerlevel].uptrans_lineindicies[ii] == -upperlevel)
      {
        globals::elements[element].ions[ion].levels[lowerlevel].uptrans_lineindicies[ii] = lineindex;
        // break; // should be safe to end here if there is max. one transition per pair of levels
      }
    }
  }

  for (int element = 0; element < get_nelements(); element++)
  {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++)
    {
      if (globals::elements[element].ions[ion].nlevels_groundterm <= 0)
        globals::elements[element].ions[ion].nlevels_groundterm = calculate_nlevels_groundterm(element, ion);
    }
  }

  /// Photoionisation cross-sections
  ///======================================================
  ///finally read in photoionisation cross sections and store them to the atomic data structure
  read_phixs_data();

  int cont_index = -1;
  for (int element = 0; element < get_nelements(); element++)
  {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++)
    {
      for (int level = 0; level < get_ionisinglevels(element, ion); level++)
      {
        globals::elements[element].ions[ion].levels[level].cont_index = cont_index;
        cont_index -= get_nphixstargets(element, ion, level);
      }

      // below is just an extra warning consistency check
      const int nlevels_groundterm = globals::elements[element].ions[ion].nlevels_groundterm;

      // all levels in the ground term should be photoionisation targets from the lower ground state
      if (ion > 0 && ion < get_nions(element) - 1)
      {
        if (get_phixsupperlevel(element, ion - 1, 0, 0) == 0)
        {
          const int nphixstargets = get_nphixstargets(element, ion - 1, 0);
          const int phixstargetlevels = get_phixsupperlevel(element, ion - 1, 0, nphixstargets - 1) + 1;

          if (nlevels_groundterm != phixstargetlevels)
          {
            printout("WARNING: Z=%d ion_stage %d nlevels_groundterm %d phixstargetlevels(ion-1) %d.\n",
                     get_element(element), get_ionstage(element, ion), nlevels_groundterm, phixstargetlevels);
            // if (nlevels_groundterm < phixstargetlevels)
            // {
            //   printout("  -> setting to %d\n", phixstargetlevels);
            //   globals::elements[element].ions[ion].nlevels_groundterm = phixstargetlevels;
            // }
          }
        }
      }
    }
  }

  printout("cont_index %d\n", cont_index);
}


static int search_groundphixslist(double nu_edge, int *index_in_groundlevelcontestimator, int el, int in, int ll)
/// Return the closest ground level continuum index to the given edge
/// frequency. If the given edge frequency is redder than the reddest
/// continuum return -1.
/// NB: groundphixslist must be in ascending order.
{
  int index;

  if (nu_edge < globals::phixslist[tid].groundcont[0].nu_edge)
  {
    index = -1;
    *index_in_groundlevelcontestimator = -1;
  }
  else
  {
    int i;
    int element;
    int ion;
    for (i = 1; i < globals::nbfcontinua_ground; i++)
    {
      if (nu_edge < globals::phixslist[tid].groundcont[i].nu_edge)
        break;
    }
/*    if (i == nbfcontinua_ground)
    {
      printout("[fatal] search_groundphixslist: i %d, nu_edge %g, globals::phixslist[tid].groundcont[i-1].nu_edge %g ... abort\n",i,nu_edge,phixslist[tid].groundcont[i-1].nu_edge);
      printout("[fatal] search_groundphixslist: this is element %d, ion %d, level %d in groundphixslist at i-1\n",el,in,ll);
      //printout("[fatal] search_groundphixslist: this is element %d, ion %d, level %d in groundphixslist at i-1\n",phixslist[tid].groundcont[i-1].element,phixslist[tid].groundcont[i-1].ion,groundphixslist[i-1].level);
      abort();
    }*/
    if (i == globals::nbfcontinua_ground)
    {
      element = globals::phixslist[tid].groundcont[i - 1].element;
      ion = globals::phixslist[tid].groundcont[i - 1].ion;
      int level = globals::phixslist[tid].groundcont[i - 1].level;
      if (element == el && ion == in && level == ll)
      {
        index = i - 1;
      }
      else
      {
        printout("[fatal] search_groundphixslist: element %d, ion %d, level %d has edge_frequency %g equal to the bluest ground-level continuum\n",el,in,ll,nu_edge);
        printout("[fatal] search_groundphixslist: bluest ground level continuum is element %d, ion %d, level %d at nu_edge %g\n",element,ion,level, globals::phixslist[tid].groundcont[i-1].nu_edge);
        printout("[fatal] search_groundphixslist: i %d, nbfcontinua_ground %d\n", i, globals::nbfcontinua_ground);
        printout("[fatal] This shouldn't happen, is hoewever possible if there are multiple levels in the adata file at energy=0\n");
        for (int looplevels = 0; looplevels < get_nlevels(el,in); looplevels++)
        {
          printout("[fatal]   element %d, ion %d, level %d, energy %g\n",el,in,looplevels,epsilon(el,in,looplevels));
        }
        printout("[fatal] Abort omitted ... MAKE SURE ATOMIC DATA ARE CONSISTENT\n");
        index = i - 1;
        //abort();
      }
    }
    else
    {
      const double left_diff = nu_edge - globals::phixslist[tid].groundcont[i - 1].nu_edge;
      const double right_diff = globals::phixslist[tid].groundcont[i].nu_edge - nu_edge;
      index = (left_diff <= right_diff) ? i - 1 : i;
      element = globals::phixslist[tid].groundcont[index].element;
      ion = globals::phixslist[tid].groundcont[index].ion;
    }
    *index_in_groundlevelcontestimator = element * globals::maxion + ion;
  }

  return index;
}


static void setup_cellhistory(void)
{
  /// SET UP THE CELL HISTORY
  ///======================================================
  /// Stack which holds information about population and other cell specific data
  /// ===> move to update_packets
  if ((globals::cellhistory = (cellhistory_struct *) malloc(globals::nthreads * sizeof(cellhistory_struct))) == NULL)
  {
    printout("[fatal] input: not enough memory to initialize cellhistory of size %d... abort\n", globals::nthreads);
    abort();
  }
  #ifdef _OPENMP
    #pragma omp parallel
    {
  #endif
      long mem_usage_cellhistory = 0;
      mem_usage_cellhistory += sizeof(cellhistory_struct);;
      printout("[info] input: initializing cellhistory for thread %d ...\n", tid);

      globals::cellhistory[tid].cellnumber = -99;

      mem_usage_cellhistory += globals::ncoolingterms * sizeof(cellhistorycoolinglist_t);
      globals::cellhistory[tid].coolinglist = (cellhistorycoolinglist_t *) malloc(globals::ncoolingterms * sizeof(cellhistorycoolinglist_t));
      if (globals::cellhistory[tid].coolinglist == NULL)
      {
        printout("[fatal] input: not enough memory to initialize cellhistory's coolinglist ... abort\n");
        abort();
      }
      const long mem_usage_cellhistory_coolinglist = globals::ncoolingterms * sizeof(cellhistorycoolinglist_t);
      printout("mem_usage: coolinglist (part of cellhistory) for thread %d occupies %.1f MB\n", tid, mem_usage_cellhistory_coolinglist / 1024. / 1024.);

      mem_usage_cellhistory += get_nelements() * sizeof(chelements_struct);
      if ((globals::cellhistory[tid].chelements = (chelements_struct *) malloc(get_nelements() * sizeof(chelements_struct))) == NULL)
      {
        printout("[fatal] input: not enough memory to initialize cellhistory's elementlist ... abort\n");
        abort();
      }
      for (int element = 0; element < get_nelements(); element++)
      {
        const int nions = get_nions(element);
        mem_usage_cellhistory += nions * sizeof(chions_struct);
        if ((globals::cellhistory[tid].chelements[element].chions = (chions_struct *) malloc(nions * sizeof(chions_struct))) == NULL)
        {
          printout("[fatal] input: not enough memory to initialize cellhistory's ionlist ... abort\n");
          abort();
        }
        for (int ion = 0; ion < nions; ion++)
        {
          const int nlevels = get_nlevels(element,ion);
          mem_usage_cellhistory += nlevels * sizeof(chlevels_struct);
          if ((globals::cellhistory[tid].chelements[element].chions[ion].chlevels = (chlevels_struct *) malloc(nlevels * sizeof(chlevels_struct))) == NULL)
          {
            printout("[fatal] input: not enough memory to initialize cellhistory's levellist ... abort\n");
            abort();
          }
          for (int level = 0; level < nlevels; level++)
          {
            const int nphixstargets = get_nphixstargets(element,ion,level);
            mem_usage_cellhistory += nphixstargets * sizeof(chphixstargets_struct);
            if ((globals::cellhistory[tid].chelements[element].chions[ion].chlevels[level].chphixstargets = (chphixstargets_struct *) malloc(nphixstargets * sizeof(chphixstargets_struct))) == NULL)
            {
              printout("[fatal] input: not enough memory to initialize cellhistory's chphixstargets ... abort\n");
              abort();
            }
          }
        }
      }
      printout("mem_usage: cellhistory for thread %d occupies %.1f MB\n", tid, mem_usage_cellhistory / 1024. / 1024.);
  #ifdef _OPENMP
    }
  #endif
}


static void write_bflist_file(int includedphotoiontransitions)
{
  if ((globals::bflist = (bflist_t *) malloc(includedphotoiontransitions * sizeof(bflist_t))) == NULL)
  {
    printout("[fatal] input: not enough memory to initialize bflist ... abort\n");
    abort();
  }

  FILE *bflist_file;
  if (globals::rank_global == 0)
  {
    bflist_file = fopen_required("bflist.dat", "w");
    fprintf(bflist_file,"%d\n", includedphotoiontransitions);
  }
  int i = 0;
  for (int element = 0; element < get_nelements(); element++)
  {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++)
    {
      const int nlevels = get_ionisinglevels(element, ion);
      for (int level = 0; level < nlevels; level++)
      {
        for (int phixstargetindex = 0; phixstargetindex < get_nphixstargets(element, ion, level); phixstargetindex++)
        {
          const int upperionlevel = get_phixsupperlevel(element, ion, level, phixstargetindex);
          globals::bflist[i].elementindex = element;
          globals::bflist[i].ionindex = ion;
          globals::bflist[i].levelindex = level;
          globals::bflist[i].phixstargetindex = phixstargetindex;

          if (globals::rank_global == 0)
            fprintf(bflist_file,"%d %d %d %d %d\n", i, element, ion, level, upperionlevel);

          assert(-1 - i == get_continuumindex(element, ion, level, upperionlevel));

          assert(i != 9999999 - 1); // would cause the same packet emission type as the special value for free-free scattering
          i++;
        }
      }
    }
  }
  assert(i == includedphotoiontransitions);
  if (globals::rank_global == 0)
    fclose(bflist_file);
}


static void setup_coolinglist(void)
{
  /// SET UP THE COOLING LIST
  ///======================================================
  /// Determine number of processes which allow kpkts to convert to something else.
  /// This number is given by the collisional excitations (so far determined from the oscillator strengths
  /// by the van Regemorter formula, therefore totaluptrans), the number of free-bound emissions and collisional ionisations
  /// (as long as we only deal with ionisation to the ground level this means for both of these
  /// \sum_{elements,ions}get_nlevels(element,ion) and free-free which is \sum_{elements} get_nions(element)-1
  /*ncoolingterms = totaluptrans;
  for (element = 0; element < get_nelements(); element++)
  {
    nions = get_nions(element);
    for (ion=0; ion < nions; ion++)
    {
      if (get_ionstage(element,ion) > 1) ncoolingterms++;
      if (ion < nions - 1) ncoolingterms += 2 * get_ionisinglevels(element,ion);
    }
  }
  printout("[info] read_atomicdata: number of coolingterms %d\n",ncoolingterms);*/

  globals::ncoolingterms = 0;
  for (int element = 0; element < get_nelements(); element++)
  {
    const int nions = get_nions(element);
    for (int ion = 0; ion < nions; ion++)
    {
      int ionterms = 0;
      globals::elements[element].ions[ion].coolingoffset = globals::ncoolingterms;
      /// Ionised ions add one ff-cooling term
      if (get_ionstage(element,ion) > 1)
        ionterms++;
      /// Ionisinglevels below the closure ion add to bf and col ionisation
      /// All the levels add number of col excitations
      const int nlevels = get_nlevels(element,ion);
      for (int level = 0; level < nlevels; level++)
      {
        //if (ion < nions - 1) and (level < get_ionisinglevels(element,ion))
        if (ion < nions - 1)
          ionterms += 2 * get_nphixstargets(element,ion,level);

        ionterms += 1; // level's coll. excitation cooling (all upper levels combined)
      }
      globals::elements[element].ions[ion].ncoolingterms = ionterms;
      globals::ncoolingterms += ionterms;
    }
  }
  printout("[info] read_atomicdata: number of coolingterms %d\n", globals::ncoolingterms);
}


static int compare_phixslistentry_bynuedge(const void *p1, const void *p2)
/// Helper function to sort the phixslist by ascending threshold frequency.
{
  const fullphixslist_t *a1 = (fullphixslist_t *)(p1);
  const fullphixslist_t *a2 = (fullphixslist_t *)(p2);

  double edge_diff = a1->nu_edge - a2->nu_edge;
  if (edge_diff < 0)
    return -1;
  else if (edge_diff > 0)
    return 1;
  else
    return 0;
}


static int compare_groundphixslistentry_bynuedge(const void *p1, const void *p2)
/// Helper function to sort the groundphixslist by ascending threshold frequency.
{
  const groundphixslist_t *a1 = (groundphixslist_t *)(p1);
  const groundphixslist_t *a2 = (groundphixslist_t *)(p2);

  double edge_diff = a1->nu_edge - a2->nu_edge;
  if (edge_diff < 0)
    return -1;
  else if (edge_diff > 0)
    return 1;
  else
    return 0;
}


static void setup_phixs_list(void)
{
  /// SET UP THE PHIXSLIST
  ///======================================================
  printout("[info] read_atomicdata: number of bfcontinua %d\n", globals::nbfcontinua);
  printout("[info] read_atomicdata: number of ground-level bfcontinua %d\n", globals::nbfcontinua_ground);

  globals::phixslist = (phixslist_t *) malloc(globals::nthreads * sizeof(phixslist_t));
  if (globals::phixslist == NULL)
  {
    printout("[fatal] read_atomicdata: not enough memory to initialize phixslist... abort\n");
    abort();
  }

  /// MK: 2012-01-19
  /// To fix the OpenMP problem on BlueGene machines this parallel section was removed and replaced by
  /// a serial loop which intializes the phixslist data structure for all threads in a loop. I'm still
  /// not sure why this causes a problem at all and on BlueGene architectures in particular. However,
  /// it seems to fix the problem.
  //#ifdef _OPENMP
  //  #pragma omp parallel private(i,element,ion,level,nions,nlevels,epsilon_upper,E_threshold,nu_edge)
  //  {
  //#endif
  for (int itid = 0; itid < globals::nthreads; itid++)
  {
    /// Number of ground level bf-continua equals the total number of included ions minus the number
    /// of included elements, because the uppermost ionisation stages can't ionise.
    //printout("groundphixslist nbfcontinua_ground %d\n",nbfcontinua_ground);
    printout("initialising groundphixslist for itid %d\n", itid);
    globals::phixslist[itid].groundcont = (groundphixslist_t *) malloc(globals::nbfcontinua_ground * sizeof(groundphixslist_t));
    if (globals::phixslist[itid].groundcont == NULL)
    {
      printout("[fatal] read_atomicdata: not enough memory to initialize globals::phixslist[%d].groundcont... abort\n", itid);
      abort();
    }
    printout("mem_usage: phixslist[tid].groundcont for thread %d occupies %.1f MB\n",
             itid, globals::nbfcontinua_ground * sizeof(groundphixslist_t) / 1024. / 1024.);

    int i = 0;
    for (int element = 0; element < get_nelements(); element++)
    {
      const int nions = get_nions(element);
      for (int ion = 0; ion < nions-1; ion++)
      {
        const int nlevels_groundterm = get_nlevels_groundterm(element, ion);
        for (int level = 0; level < nlevels_groundterm; level++)
        {
          const int nphixstargets = get_nphixstargets(element, ion, level);
          for (int phixstargetindex = 0; phixstargetindex < nphixstargets; phixstargetindex++)
          {
            // const int upperlevel = get_phixsupperlevel(element, ion, level, 0);
            // const double E_threshold = epsilon(element, ion + 1, upperlevel) - epsilon(element, ion, level);
            const double E_threshold = get_phixs_threshold(element, ion, level, phixstargetindex);
            const double nu_edge = E_threshold / H;
            globals::phixslist[itid].groundcont[i].element = element;
            globals::phixslist[itid].groundcont[i].ion = ion;
            globals::phixslist[itid].groundcont[i].level = level;
            globals::phixslist[itid].groundcont[i].nu_edge = nu_edge;
            globals::phixslist[itid].groundcont[i].phixstargetindex = phixstargetindex;
            //printout("phixslist.groundcont nbfcontinua_ground %d, i %d, element %d, ion %d, level %d, nu_edge %g\n",nbfcontinua_ground,i,element,ion,level,nu_edge);
            i++;
          }
        }
      }
    }
    qsort(globals::phixslist[itid].groundcont, globals::nbfcontinua_ground, sizeof(groundphixslist_t), compare_groundphixslistentry_bynuedge);


    //if (TAKE_N_BFCONTINUA >= 0) phixslist = malloc(includedions*TAKE_N_BFCONTINUA*sizeof(phixslist_t));
    //else
    globals::phixslist[itid].allcont = (fullphixslist_t *) malloc(globals::nbfcontinua * sizeof(fullphixslist_t));
    if (globals::phixslist[itid].allcont == NULL)
    {
      printout("[fatal] read_atomicdata: not enough memory to initialize phixslist... abort\n");
      abort();
    }
    printout("mem_usage: phixslist[tid].allcont for thread %d occupies %.1f MB\n",
             itid, globals::nbfcontinua * sizeof(fullphixslist_t) / 1024. / 1024.);

    i = 0;
    for (int element = 0; element < get_nelements(); element++)
    {
      const int nions = get_nions(element);
      for (int ion = 0; ion < nions - 1; ion++)
      {
        const int nlevels = get_ionisinglevels(element, ion);
        //nlevels = get_ionisinglevels(element,ion);
        ///// The following line reduces the number of bf-continua per ion
        //if (nlevels > TAKE_N_BFCONTINUA) nlevels = TAKE_N_BFCONTINUA;
        for (int level = 0; level < nlevels; level++)
        {
          const int nphixstargets = get_nphixstargets(element, ion, level);
          for (int phixstargetindex = 0; phixstargetindex < nphixstargets; phixstargetindex++)
          {
            // const int upperlevel = get_phixsupperlevel(element, ion,level, 0);
            // const double E_threshold = epsilon(element, ion + 1, upperlevel) - epsilon(element, ion, level);
            const double E_threshold = get_phixs_threshold(element, ion, level, phixstargetindex);
            const double nu_edge = E_threshold / H;

            int index_in_groundlevelcontestimator;

            globals::phixslist[itid].allcont[i].element = element;
            globals::phixslist[itid].allcont[i].ion = ion;
            globals::phixslist[itid].allcont[i].level = level;
            globals::phixslist[itid].allcont[i].phixstargetindex = phixstargetindex;
            globals::phixslist[itid].allcont[i].nu_edge = nu_edge;
            globals::phixslist[itid].allcont[i].index_in_groundphixslist = search_groundphixslist(nu_edge, &index_in_groundlevelcontestimator, element, ion, level);
            #if (!NO_LUT_PHOTOION || !NO_LUT_BFHEATING)
              if (itid == 0)
                globals::elements[element].ions[ion].levels[level].closestgroundlevelcont = index_in_groundlevelcontestimator;
            #endif
            i++;
          }
        }
      }
    }

    //nbfcontinua = i;
    //printout("number of bf-continua reduced to %d\n",nbfcontinua);
    qsort(globals::phixslist[itid].allcont, globals::nbfcontinua, sizeof(fullphixslist_t), compare_phixslistentry_bynuedge);
  }
  //#ifdef _OPENMP
  //  }
  //#endif
}


static void read_atomicdata(void)
/// Subroutine to read in input parameters.
{
  ///new atomic data scheme by readin of adata////////////////////////////////////////////////////////////////////////
  globals::includedions = 0;

  read_atomicdata_files();
  last_phixs_nuovernuedge = (1.0 + globals::NPHIXSNUINCREMENT * (globals::NPHIXSPOINTS - 1));

  printout("included ions %d\n", globals::includedions);

  /// INITIALISE THE ABSORPTION/EMISSION COUNTERS ARRAYS
  ///======================================================
  #ifdef RECORD_LINESTAT
    if ((globals::ecounter  = (int *) malloc(globals::nlines * sizeof(int))) == NULL)
    {
      printout("[fatal] input: not enough memory to initialise ecounter array ... abort\n");
      abort();
    }
    if ((globals::acounter  = (int *) malloc(globals::nlines * sizeof(int))) == NULL)
    {
      printout("[fatal] input: not enough memory to initialise ecounter array ... abort\n");
      abort();
    }
    if ((globals::linestat_reduced  = (int *) malloc(globals::nlines * sizeof(int))) == NULL)
    {
      printout("[fatal] input: not enough memory to initialise ecounter array ... abort\n");
      abort();
    }
  #endif

  setup_coolinglist();

  setup_cellhistory();

  /// Printout some information about the read-in model atom
  ///======================================================
  //includedions = 0;
  int includedlevels = 0;
  int includedionisinglevels = 0;
  int includedphotoiontransitions = 0;
  printout("[input.c] this simulation contains\n");
  printout("----------------------------------\n");
  for (int element = 0; element < get_nelements(); element++)
  {
    printout("[input.c]   element %d (Z=%2d)\n", element, get_element(element));
    const int nions = get_nions(element);
    //includedions += nions;
    for (int ion = 0; ion < nions; ion++)
    {
      int photoiontransitions = 0;
      for (int level = 0; level < get_nlevels(element,ion); level++)
        photoiontransitions += get_nphixstargets(element,ion,level);
      printout("[input.c]     ion_stage %d with %4d levels (%d in groundterm, %4d ionising) and %6d photoionisation transitions (epsilon_ground %7.2f eV)\n",
               get_ionstage(element, ion), get_nlevels(element, ion), get_nlevels_groundterm(element, ion),
               get_ionisinglevels(element, ion), photoiontransitions, epsilon(element, ion, 0) / EV);
      includedlevels += get_nlevels(element,ion);
      includedionisinglevels += get_ionisinglevels(element,ion);
      includedphotoiontransitions += photoiontransitions;
    }
  }
  assert(includedphotoiontransitions == globals::nbfcontinua);

  printout("[input.c]   in total %d ions, %d levels (%d ionising), %d lines, %d photoionisation transitions\n",
           globals::includedions, includedlevels, includedionisinglevels, globals::nlines, globals::nbfcontinua);

  write_bflist_file(globals::nbfcontinua);

  setup_phixs_list();

  ///set-up/gather information for nlte stuff

  globals::total_nlte_levels = 0;
  globals::n_super_levels = 0;

  if (NLTE_POPS_ON)
  {
    for (int element = 0; element < get_nelements(); element++)
    {
      const int nions = get_nions(element);
      for (int ion = 0; ion < nions; ion++)
      {
        globals::elements[element].ions[ion].first_nlte = globals::total_nlte_levels;
        const int nlevels = get_nlevels(element,ion);
        int fullnlteexcitedlevelcount = 0;
        for (int level = 1; level < nlevels; level++)
        {
          if (is_nlte(element,ion,level))
          {
            fullnlteexcitedlevelcount++;
            globals::total_nlte_levels++;
          }
        }

        const bool has_superlevel = (nlevels > (fullnlteexcitedlevelcount + 1));
        if (has_superlevel)
        {
          // If there are more levels that the ground state + the number of NLTE levels then we need an extra
          // slot to store data for the "superlevel", which is a representation of all the other levels that
          // are not treated in detail.
          globals::total_nlte_levels++;
          globals::n_super_levels++;
        }

        globals::elements[element].ions[ion].nlevels_nlte = fullnlteexcitedlevelcount;

        assert(has_superlevel == ion_has_superlevel(element, ion));

        printout("[input.c]  element %2d Z=%2d ion_stage %2d has %5d NLTE excited levels%s. Starting at %d\n",
                 element, get_element(element), get_ionstage(element, ion),
                 fullnlteexcitedlevelcount,
                 has_superlevel ? " plus a superlevel" : "",
                 globals::elements[element].ions[ion].first_nlte);
      }
    }
  }

  printout("[input.c] Total NLTE levels: %d, of which %d are superlevels\n", globals::total_nlte_levels, globals::n_super_levels);
}


static void show_totmassradionuclides(void)
{
  globals::mtot = 0.;
  globals::mfeg = 0.;

  for (int iso = 0; iso < RADIONUCLIDE_COUNT; iso++)
    globals::totmassradionuclide[iso] = 0.;

  int n1 = 0;
  for (int mgi = 0; mgi < globals::npts_model; mgi++)
  {
    double cellvolume = 0.;
    if (get_model_type() == RHO_1D_READ)
    {
      const double v_inner = (mgi == 0) ? 0. : globals::vout_model[mgi - 1];
      // mass_in_shell = rho_model[mgi] * (pow(globals::vout_model[mgi], 3) - pow(v_inner, 3)) * 4 * PI * pow(t_model, 3) / 3.;
      cellvolume = (pow(globals::vout_model[mgi], 3) - pow(v_inner, 3)) * 4 * PI * pow(globals::tmin, 3) / 3.;
    }
    else if (get_model_type() == RHO_2D_READ)
    {
      cellvolume = pow(globals::tmin / globals::t_model, 3) * ((2 * n1) + 1) * PI * globals::dcoord2 * pow(globals::dcoord1, 2.);
      n1++;
      if (n1 == globals::ncoord1_model)
      {
        n1 = 0;
      }
    }
    else if (get_model_type() == RHO_3D_READ)
    {
      /// Assumes cells are cubes here - all same volume.
      cellvolume = pow((2 * globals::vmax * globals::tmin), 3.) / (globals::ncoordgrid[0] * globals::ncoordgrid[1] * globals::ncoordgrid[2]);
    }
    else
    {
      printout("Unknown model type %d in function %s\n", get_model_type(), __func__);
      abort();
    }

    const double mass_in_shell = get_rhoinit(mgi) * cellvolume;

    globals::mtot += mass_in_shell;

    for (int isoint = 0; isoint < RADIONUCLIDE_COUNT; isoint++)
    {
      const enum radionuclides iso = (enum radionuclides) isoint;
      globals::totmassradionuclide[iso] += mass_in_shell * get_modelinitradioabund(mgi, iso);
    }

    globals::mfeg += mass_in_shell * get_ffegrp(mgi);
  }


  printout("Masses / Msun:    Total: %9.3e  56Ni: %9.3e  56Co: %9.3e  52Fe: %9.3e  48Cr: %9.3e\n",
           globals::mtot / MSUN, globals::totmassradionuclide[NUCLIDE_NI56] / MSUN,
           globals::totmassradionuclide[NUCLIDE_CO56] / MSUN, globals::totmassradionuclide[NUCLIDE_FE52] / MSUN,
           globals::totmassradionuclide[NUCLIDE_CR48] / MSUN);
  printout("Masses / Msun: Fe-group: %9.3e  57Ni: %9.3e  57Co: %9.3e\n",
           globals::mfeg / MSUN, globals::totmassradionuclide[NUCLIDE_NI57] / MSUN, globals::totmassradionuclide[NUCLIDE_CO57] / MSUN);
}


static void read_2d3d_modelabundanceline(FILE * model_input, const int mgi, const bool keep)
{
  char line[1024] = "";
  if (line != fgets(line, 1024, model_input))
  {
    printout("Read failed on second line for cell %d\n", mgi);
    abort();
  }

  double f56ni_model = 0.;
  double f56co_model = 0.;
  double ffegrp_model = 0.;
  double f48cr_model = 0.;
  double f52fe_model = 0.;
  double f57ni_model = 0.;
  double f57co_model = 0.;
  const int items_read = sscanf(line, "%lg %lg %lg %lg %lg %lg %lg",
    &ffegrp_model, &f56ni_model, &f56co_model, &f52fe_model, &f48cr_model, &f57ni_model, &f57co_model);

  if (items_read == 5 || items_read == 7)
  {
    if (items_read == 10 && mgi == 0)
    {
      printout("Found Ni57 and Co57 abundance columns in model.txt\n");
    }

    // printout("mgi %d ni56 %g co56 %g fe52 %g cr48 %g ni57 %g co57 %g\n",
    //          mgi, f56ni_model, f56co_model, f52fe_model, f48cr_model, f57ni_model, f57co_model);

    if (keep)
    {
      set_modelinitradioabund(mgi, NUCLIDE_NI56, f56ni_model);
      set_modelinitradioabund(mgi, NUCLIDE_CO56, f56co_model);
      set_modelinitradioabund(mgi, NUCLIDE_NI57, f57ni_model);
      set_modelinitradioabund(mgi, NUCLIDE_CO57, f57co_model);
      set_modelinitradioabund(mgi, NUCLIDE_FE52, f52fe_model);
      set_modelinitradioabund(mgi, NUCLIDE_CR48, f48cr_model);
      set_modelinitradioabund(mgi, NUCLIDE_V48, 0.);

      set_ffegrp(mgi, ffegrp_model);
      //printout("mgi %d, control rho_init %g\n",mgi,get_rhoinit(mgi));
    }
  }
  else
  {
    printout("Unexpected number of values in model.txt. items_read = %d\n", items_read);
    printout("line: %s\n", line);
    abort();
  }
}


static void read_1d_model(void)
/// Subroutine to read in a 1-D model.
{
  FILE *model_input = fopen_required("model.txt", "r");

  // 1st read the number of data points in the table of input model.
  fscanf(model_input, "%d", &globals::npts_model);
  if (globals::npts_model > MMODELGRID)
  {
    printout("Too many points in input model. Abort.\n");
    abort();
  }
  // Now read the time (in days) at which the model is specified.
  double t_model_days;
  fscanf(model_input, "%lg\n", &t_model_days);
  globals::t_model = t_model_days * DAY;

  // Now read in the lines of the model. Each line has 5 entries: the
  // cell number (integer) the velocity at outer boundary of cell (float),
  // the mass density in the cell (float), the abundance of Ni56 by mass
  // in the cell (float) and the total abundance of all Fe-grp elements
  // in the cell (float). For now, the last number is recorded but never
  // used.

  int mgi = 0;
  while (!feof(model_input))
  {
    char line[1024] = "";
    if (line != fgets(line, 1024, model_input))
    {
      // no more lines to read in
      break;
    }

    int cellnumberin;
    double vout_kmps;
    double log_rho;
    double f56ni_model = 0.;
    double f56co_model = 0.;
    double ffegrp_model = 0.;
    double f48cr_model = 0.;
    double f52fe_model = 0.;
    double f57ni_model = 0.;
    double f57co_model = 0.;
    const int items_read = sscanf(line, "%d %lg %lg %lg %lg %lg %lg %lg %lg %lg",
                                   &cellnumberin, &vout_kmps, &log_rho, &ffegrp_model, &f56ni_model,
                                   &f56co_model, &f52fe_model, &f48cr_model, &f57ni_model, &f57co_model);

    if (items_read == 8 || items_read == 10)
    {
      assert(cellnumberin == mgi + 1);

      globals::vout_model[mgi] = vout_kmps * 1.e5;

      const double rho_tmin = pow(10., log_rho) * pow(globals::t_model / globals::tmin, 3);
      set_rhoinit(mgi, rho_tmin);
      set_rho(mgi, rho_tmin);

      if (items_read == 10 && mgi == 0)
      {
        printout("Found Ni57 and Co57 abundance columns in model.txt\n");
      }
    }
    else
    {
      printout("Unexpected number of values in model.txt. items_read = %d\n", items_read);
      printout("line: %s\n", line);
      abort();
    }

    // printout("%d %g %g %g %g %g %g %g\n",
    //          cellnumin, vout_kmps, log_rho, ffegrp_model[n], f56ni_model[n],
    //          f56co_model[n], f52fe_model[n], f48cr_model[n]);
    // printout("   %lg %lg\n", f57ni_model[n], f57co_model[n]);
    set_modelinitradioabund(mgi, NUCLIDE_NI56, f56ni_model);
    set_modelinitradioabund(mgi, NUCLIDE_CO56, f56co_model);
    set_modelinitradioabund(mgi, NUCLIDE_NI57, f57ni_model);
    set_modelinitradioabund(mgi, NUCLIDE_CO57, f57co_model);
    set_modelinitradioabund(mgi, NUCLIDE_FE52, f52fe_model);
    set_modelinitradioabund(mgi, NUCLIDE_CR48, f48cr_model);
    set_modelinitradioabund(mgi, NUCLIDE_V48, 0.);
    set_ffegrp(mgi, ffegrp_model);

    mgi += 1;
    if (mgi == globals::npts_model)
    {
      break;
    }
  }

  if (mgi != globals::npts_model)
  {
    printout("ERROR in model.txt. Found %d only cells instead of %d expected.\n", mgi - 1, globals::npts_model);
    abort();
  }

  fclose(model_input);

  globals::vmax = globals::vout_model[globals::npts_model - 1];
}


static void read_2d_model(void)
/// Subroutine to read in a 2-D model.
{
  FILE *model_input = fopen_required("model.txt", "r");

  // 1st read the number of data points in the table of input model.
  fscanf(model_input, "%d %d", &globals::ncoord1_model, &globals::ncoord2_model);  // r and z (cylindrical polar)

  globals::npts_model = globals::ncoord1_model * globals::ncoord2_model;
  if (globals::npts_model > MMODELGRID)
  {
    printout("Too many points in input model. Abort.\n");
    abort();
  }
  // Now read the time (in days) at which the model is specified.
  double t_model_days;
  fscanf(model_input, "%lg", &t_model_days);
  globals::t_model = t_model_days * DAY;

  // Now read in globals::vmax (in cm/s)
  fscanf(model_input, "%lg\n", &globals::vmax);
  globals::dcoord1 = globals::vmax * globals::t_model / globals::ncoord1_model; //dr for input model
  globals::dcoord2 = 2. * globals::vmax * globals::t_model / globals::ncoord2_model; //dz for input model

  // Now read in the model. Each point in the model has two lines of input.
  // First is an index for the cell then its r-mid point then its z-mid point
  // then its total mass density.
  // Second is the total FeG mass, initial 56Ni mass, initial 56Co mass

  int mgi = 0;
  while (!feof(model_input))
  {
    char line[1024] = "";
    if (line != fgets(line, 1024, model_input))
    {
      // no more lines to read in
      break;
    }

    int cellnumin;
    float cell_r_in;
    float cell_z_in;
    double rho_tmodel;

    int items_read = sscanf(line, "%d %g %g %lg", &cellnumin, &cell_r_in, &cell_z_in, &rho_tmodel);
    assert(items_read == 4);

    const int ncoord1 = ((cellnumin - 1) % globals::ncoord1_model);
    const double r_cylindrical = (ncoord1 + 0.5) * globals::dcoord1;
    assert(fabs(cell_r_in / r_cylindrical - 1) < 1e-3);
    const int ncoord2 = ((cellnumin - 1) / globals::ncoord1_model);
    const double z = -globals::vmax * globals::t_model + ((ncoord2 + 0.5) * globals::dcoord2);
    assert(fabs(cell_z_in / z - 1) < 1e-3);

    assert(cellnumin == mgi + 1);

    const double rho_tmin = rho_tmodel * pow(globals::t_model / globals::tmin, 3);
    set_rhoinit(mgi, rho_tmin);
    set_rho(mgi, rho_tmin);

    read_2d3d_modelabundanceline(model_input, mgi, true);

    mgi++;
  }

  if (mgi != globals::npts_model)
  {
    printout("ERROR in model.txt. Found %d only cells instead of %d expected.\n", mgi - 1, globals::npts_model);
    abort();
  }

  fclose(model_input);
}


static void read_3d_model(void)
/// Subroutine to read in a 3-D model.
{
  FILE *model_input = fopen_required("model.txt", "r");

  /// 1st read the number of data points in the table of input model.
  /// This MUST be the same number as the maximum number of points used in the grid - if not, abort.
  int npts_model_in = 0;
  fscanf(model_input, "%d", &npts_model_in);
  if (npts_model_in > MMODELGRID)
  {
    printout("Too many points in input model. Abort. (%d > %d)\n", npts_model_in, MMODELGRID);
    abort();
  }
  if (npts_model_in != globals::ngrid)
  {
    printout("3D model/grid mismatch. Abort. %d != %d\n", npts_model_in, globals::ngrid);
    abort();
  }

  /// Now read the time (in days) at which the model is specified.
  float t_model_days;
  fscanf(model_input, "%g", &t_model_days);
  globals::t_model = t_model_days * DAY;

  /// Now read in globals::vmax for the model (in cm s^-1).
  fscanf(model_input, "%lg\n", &globals::vmax);

  // double rmax_tmodel = globals::vmax * t_model;

  /// Now read in the lines of the model.
  globals::min_den = 1.e99;

  // mgi is the index to the model grid - empty cells are sent to MMODELGRID,
  // otherwise each input cell is one modelgrid cell
  int mgi = 0;
  int n = 0;
  while (!feof(model_input))
  {
    char line[1024] = "";
    if (line != fgets(line, 1024, model_input))
    {
      // no more lines to read in
      break;
    }

    int mgi_in;
    float cellpos_in[3];
    float rho_model;
    int items_read = sscanf(line, "%d %g %g %g %g", &mgi_in, &cellpos_in[2], &cellpos_in[1], &cellpos_in[0], &rho_model);
    assert(items_read == 5);
    //printout("cell %d, posz %g, posy %g, posx %g, rho %g, rho_init %g\n",dum1,dum3,dum4,dum5,rho_model,rho_model* pow( (t_model/globals::tmin), 3.));

    assert(mgi_in == n + 1);

    // cell coordinates in the 3D model.txt file are sometimes reordered by the scaling script
    // however, the cellindex always should increment X first, then Y, then Z

    // for (int axis = 0; axis < 3; axis++)
    // {
    //   const double cellpos_expected = - rmax_tmodel + (2 * get_cellcoordpointnum(n, axis) * rmax_tmodel / globals::ncoordgrid[axis]);
    //   // printout("n %d axis %d expected %g found %g rmax %g get_cellcoordpointnum(n, axis) %d globals::ncoordgrid %d\n",
    //   // n, axis, cellpos_expected, cellpos_in[axis], rmax_model, get_cellcoordpointnum(n, axis), globals::ncoordgrid);
    //   assert((fabs(cellpos_in[axis] / cellpos_expected - 1) < 1e-3) || ((cellpos_in[axis] == 0) && (cellpos_expected == 0)));
    // }

    if (rho_model < 0)
    {
      printout("negative input density %g %d\n", rho_model, n);
      abort();
    }

    if (mgi > MMODELGRID - 1)
    {
      printout("3D model wants more modelgrid cells than MMODELGRID. Abort.\n");
      abort();
    }

    const bool keepcell = (rho_model > 0);
    if (keepcell)
    {
      globals::cell[n].modelgridindex = mgi;
      const double rho_tmin = rho_model * pow((globals::t_model / globals::tmin), 3.);
      //printout("mgi %d, helper %g\n",mgi,helper);
      set_rhoinit(mgi, rho_tmin);
      //printout("mgi %d, rho_init %g\n",mgi,get_rhoinit(mgi));
      set_rho(mgi, rho_tmin);

      if (globals::min_den > rho_model)
      {
        globals::min_den = rho_model;
      }
    }
    else
    {
      globals::cell[n].modelgridindex = MMODELGRID;
    }

    read_2d3d_modelabundanceline(model_input, mgi, keepcell);
    if (keepcell)
    {
      mgi++;
    }

    n++;
  }
  if (n != npts_model_in)
  {
    printout("ERROR in model.txt. Found %d cells instead of %d expected.\n", n, npts_model_in);
    abort();
  }

  printout("min_den %g\n", globals::min_den);
  printout("Effectively used model grid cells %d\n", mgi);

  /// Now, set actual size of the modelgrid to the number of non-empty cells.
  /// Actually this doesn't reduce the memory consumption since all the related
  /// arrays are allocated statically at compile time with size MMODELGRID+1.
  /// However, it ensures that update_grid causes no idle tasks due to empty cells!
  globals::npts_model = mgi;

  fclose(model_input);
}


static void read_ejecta_model(enum model_types model_type)
{
  switch (model_type)
  {
    case RHO_UNIFORM:
    {
      assert(false); // needs to be reimplemented using spherical coordinate mode
      globals::mtot = 1.39 * MSUN;
      globals::totmassradionuclide[NUCLIDE_NI56] = 0.625 * MSUN;
      globals::vmax = 1.e9;
      // rhotot = 3 * mtot / 4 / PI / rmax /rmax /rmax; //MK
      break;
    }

    case RHO_1D_READ:
      printout("Read 1D model!\n");
      read_1d_model();
      break;

    case RHO_2D_READ:
      printout("Read 2D model!\n");

      read_2d_model();
      break;

    case RHO_3D_READ:
      printout("Read 3D model!\n");
      read_3d_model();
      break;

    default:
      printout("Unknown model. Abort.\n");
      abort();
  }
}


#ifdef VPKT_ON
static void read_parameterfile_vpkt(void)
{
  int dum1,dum8;
  float dum2,dum3,dum4,dum5,dum6,dum7,dum9,dum10,dum11;

  FILE *input_file = fopen_required("vpkt.txt", "r");

  // Nobs
  fscanf(input_file, "%d", &dum1);
  Nobs = dum1;

  assert(Nobs <= MOBS);

  // nz_obs_vpkt. Cos(theta) to the observer. A list in the case of many observers
  for (int i = 0; i < Nobs; i++)
  {
    fscanf(input_file, "%g", &dum2);
    nz_obs_vpkt[i] = dum2;

    if (fabs(nz_obs_vpkt[i]) > 1)
    {
      printout("Wrong observer direction \n");
      exit(0);
    }
    else if (nz_obs_vpkt[i] == 1)
    {
      nz_obs_vpkt[i] = 0.9999;
    }
    else if (nz_obs_vpkt[i] == -1)
    {
      nz_obs_vpkt[i] = -0.9999;
    }
  }

  // phi to the observer (degrees). A list in the case of many observers
  for (int i = 0; i < Nobs; i++)
  {
    fscanf(input_file, "%g \n", &dum3);
    phiobs[i] = dum3 * PI / 180;
  }

  // Nspectra opacity choices (i.e. Nspectra spectra for each observer)
  fscanf(input_file, "%g ", &dum4);

  if (dum4 != 1)
  {
    Nspectra = 1;
    exclude[0] = 0;
  }
  else
  {
    fscanf(input_file, "%d ", &dum1);
    Nspectra = dum1;

    assert(Nspectra <= MSPECTRA);

    for (int i = 0; i < Nspectra; i++)
    {
      fscanf(input_file, "%g ", &dum2);
      exclude[i] = dum2;

      // The first number should be equal to zero!
      assert(exclude[0] == 0); // "The first spectrum should allow for all opacities (exclude[i]=0) and is not \n"
    }
  }

  // time window. If dum4=1 it restrict vpkt to time windown (dum5,dum6)
  fscanf(input_file, "%g %g %g \n", &dum4, &dum5, &dum6);

  if (dum4 == 1)
  {
    tmin_vspec_input = dum5 * DAY;
    tmax_vspec_input = dum6 * DAY;
  }
  else
  {
    tmin_vspec_input = tmin_vspec;
    tmax_vspec_input = tmax_vspec;
  }

  printout("tmin_vspec_input %g tmax_vspec_input %g\n", tmin_vspec_input, tmax_vspec_input);

  assert(tmax_vspec_input >= tmin_vspec);
  assert(tmax_vspec_input <= tmax_vspec);

  // frequency window. dum4 restrict vpkt to a frequency range, dum5 indicates the number of ranges,
  // followed by a list of ranges (dum6,dum7)
  fscanf(input_file, "%g ", &dum4);

  if (dum4 == 1)
  {
    fscanf(input_file, "%g ", &dum5);

    Nrange = dum5;
    assert(Nrange <= MRANGE);

    for (int i = 0; i < Nrange; i++)
    {
      fscanf(input_file, "%g %g", &dum6, &dum7);

      lmin_vspec_input[i] = dum6;
      lmax_vspec_input[i] = dum7;

      numin_vspec_input[i] = CLIGHT / (lmax_vspec_input[i] * 1e-8);
      numax_vspec_input[i] = CLIGHT / (lmin_vspec_input[i] * 1e-8);
    }
  }
  else
  {
    Nrange = 1;

    numin_vspec_input[0] = numin_vspec;
    numax_vspec_input[0] = numax_vspec;
  }

  // if dum7=1, vpkt are not created when cell optical depth is larger than cell_is_optically_thick_vpkt
  fscanf(input_file, "%g %lg \n", &dum7, &cell_is_optically_thick_vpkt);

  if (dum7 != 1)
  {
    cell_is_optically_thick_vpkt = globals::cell_is_optically_thick;
  }
  printout("cell_is_optically_thick_vpkt %lg\n", cell_is_optically_thick_vpkt);

  // Maximum optical depth. If a vpkt reaches dum7 is thrown away
  fscanf(input_file, "%g \n", &dum7);
  tau_max_vpkt = dum7;

  // Produce velocity grid map if dum8=1
  fscanf(input_file, "%d \n", &dum8);
  vgrid_flag = dum8;

  if (dum8 == 1)
  {
    // Specify time range for velocity grid map
    fscanf(input_file, "%g %g \n", &dum9,&dum10);
    tmin_grid = dum9 * DAY;
    tmax_grid = dum10 * DAY;

    // Specify wavelength range: number of intervals (dum9) and limits (dum10,dum11)
    fscanf(input_file, "%g ", &dum9);
    Nrange_grid = dum9 ;

    assert(Nrange_grid <= MRANGE_GRID);

    for (int i = 0; i < Nrange_grid; i++)
    {
      fscanf(input_file, "%g %g", &dum10, &dum11);

      nu_grid_max[i] = CLIGHT / (dum10 * 1e-8);
      nu_grid_min[i] = CLIGHT / (dum11 * 1e-8);
    }
  }
  fclose(input_file);
}
#endif


void input(int rank)
/// To govern the input. For now hardwire everything.
{
  globals::homogeneous_abundances = false;
  globals::t_model = 0.0;

  /// Select grid type
  globals::grid_type = GRID_UNIFORM;
  // globals::grid_type = GRID_SPHERICAL1D;

  // this gets overwritten by the input file
  // model_type = RHO_UNIFORM;

  globals::maxion = MIONS;

  /// Set grid size for uniform xyz grid (will be overwritten for a spherical grid)
  //globals::ncoordgrid[0] = 4; //pow(MGRID,1./3.); //10;
  //globals::ncoordgrid[1] = 4; //pow(MGRID,1./3.); //10;
  //globals::ncoordgrid[2] = 4; //pow(MGRID,1./3.); //10;
  globals::ncoordgrid[0] = 50;
  globals::ncoordgrid[1] = 50;
  globals::ncoordgrid[2] = 50;
  printout("globals::ncoordgrid: %d * %d * %d\n", globals::ncoordgrid[0], globals::ncoordgrid[1], globals::ncoordgrid[2]);
  globals::ngrid = globals::ncoordgrid[0] * globals::ncoordgrid[1] * globals::ncoordgrid[2]; ///Moved to input.c
  if (globals::ngrid > MGRID)
  {
    printout("[fatal] input: Error: too many grid cells. Abort. %d>%d", globals::ngrid, MGRID);
    abort();
  }

  /// Set number of packets, outer and middle iterations
  globals::npkts = MPKTS;
  globals::n_out_it = 10;
  globals::n_middle_it = 1;
/*  #ifdef FORCE_LTE
    n_titer = 1;
  #else
    n_titer = 6;
  #endif*/
  globals::n_titer = 1;
  globals::initial_iteration = false;

  printout("[info] input: do n_titer %d iterations per timestep\n", globals::n_titer);
  if (globals::n_titer > 1)
  {
    #ifndef DO_TITER
      printout("[fatal] input: n_titer > 1, but DO_TITER not defined ... abort\n");
      abort();
    #endif
  }
  else if (globals::n_titer == 1)
  {
    #ifdef DO_TITER
      printout("[warning] input: n_titer = 1 but DO_TITER defined, remove DO_TITER to save memory\n");
    #endif
  }
  else
  {
    printout("[fatal] input: no valid value for n_titer selected\n");
    abort();
  }

  globals::nu_min_r = NU_MIN_R;   /// lower frequency boundary for UVOIR spectra and BB sampling
  globals::nu_max_r = NU_MAX_R;   /// upper frequency boundary for UVOIR spectra and BB sampling

  /// Lightcurve setting
  globals::do_r_lc = false;    /// default to no lc = gamma-ray spectrum
  globals::do_rlc_est = 0; /// ^^

  globals::nfake_gam = 1; ///# of fake gamma ray lines for syn

  /// Read in parameters from input.txt
  read_parameterfile(rank);

  /// Read in parameters from vpkt.txt
  #ifdef VPKT_ON
    read_parameterfile_vpkt();
  #endif

  read_atomicdata();

  read_ejecta_model(get_model_type());

  printout("npts_model: %d\n", globals::npts_model);
  globals::rmax = globals::vmax * globals::tmin;
  printout("globals::vmax %g\n", globals::vmax);
  printout("globals::tmin %g\n", globals::tmin);
  printout("rmax %g\n", globals::rmax);

  globals::coordmax[0] = globals::coordmax[1] = globals::coordmax[2] = globals::rmax;

  show_totmassradionuclides();


  /// Read in data for gamma ray lines and make a list of them in energy order.
  init_gamma_linelist();

  /// Now that the list exists use it to find values for spectral synthesis
  /// stuff.
  const int lindex_max = get_nul(globals::nusyn_max);
  const int lindex_min = get_nul(globals::nusyn_min);
  printout("lindex_max %d, lindex_min %d\n", lindex_max, lindex_min);

  globals::emiss_offset = lindex_min;
  globals::emiss_max = lindex_max - lindex_min + 1;
  printout("emiss_max using %d of a possible %d\n", globals::emiss_max, EMISS_MAX);

  if (globals::emiss_max > EMISS_MAX)
  {
    printout("Too many points needed for emissivities. Use smaller frequency range or increase EMISS_MAX. Abort.\n");
    abort();
  }

}


static bool lineiscommentonly(std::string &line)
{
  // if (line.length() == 0)
  //   return true;
  // if (line.find('#') != std::string::npos)
  for (unsigned int i = 0; i < line.find('#'); i++)
  {
    if (line[i] != ' ')
    {
      return false;
    }
  }
  return true;
}


static bool get_noncommentline(std::istream &input, std::string &line)
{
  while (true)
  {
    bool linefound = !(!std::getline(input, line));
    // printout("LINE: %s   commentonly: %s \n", line.c_str(), lineiscommentonly(line) ? "true" : "false");
    if (linefound && !lineiscommentonly(line))
    {
      return true;
    }
    else if (!linefound)
    {
      return false;
    }
  }
}


void read_parameterfile(int rank)
/// Subroutine to read in input parameters from input.txt.
{
  unsigned long int pre_zseed;

  std::ifstream file("input.txt");
  assert(file.is_open());

  std::string line;

  assert(get_noncommentline(file, line));

  int dum1;
  std::stringstream(line) >> dum1;

  if (dum1 > 0)
  {
    pre_zseed = dum1; // random number seed
    printout("[debug] using specified random number seed of %lu\n", pre_zseed);
  }
  else
  {
    pre_zseed = time(NULL);
    printout("[debug] randomly-generated random number seed is %lu\n", pre_zseed);
  }

  #ifdef _OPENMP
    #pragma omp parallel
    {
  #endif
      /// For MPI parallelisation, the random seed is changed based on the rank of the process
      /// For OpenMP parallelisation rng is a threadprivate variable and the seed changed according
      /// to the thread-ID tid.
      unsigned long int zseed = pre_zseed + (13 * rank) + (17 * tid); /* rnum generator seed */
      printout("rank %d: thread %d has zseed %lu\n", rank, tid, zseed);
      /// start by setting up the randon number generator
      rng = gsl_rng_alloc(gsl_rng_ran3);
      gsl_rng_set(rng, zseed);
      /// call it a few times to get it in motion.
      for (int n = 0; n < 100; n++)
      {
        gsl_rng_uniform(rng);
      }
      printout("rng is a '%s' generator\n", gsl_rng_name(rng));
  #ifdef _OPENMP
    }
  #endif

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::ntstep; // number of time steps

  assert(get_noncommentline(file, line));
  printout("line %s\n", line.c_str());
  std::stringstream(line) >> globals::itstep >> globals::ftstep; // number of start and end time step

  float tmin_days = 0.;
  float tmax_days = 0.;
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> tmin_days >> tmax_days; // start and end times
  assert(tmin_days > 0);
  assert(tmax_days > 0);
  assert(tmin_days < tmax_days);
  globals::tmin = tmin_days * DAY;
  globals::tmax = tmax_days * DAY;

  float dum2, dum3;
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum2 >> dum3;
  globals::nusyn_min = dum2 * MEV / H; // lowest frequency to synthesise
  globals::nusyn_max = dum3 * MEV / H; // highest frequency to synthesise

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::nsyn_time; // number of times for synthesis

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum2 >> dum3; // start and end times for synthesis
  for (int i = 0; i < globals::nsyn_time; i++)
  {
    globals::time_syn[i] = exp(log(dum2) + (dum3 * i)) * DAY;
  }

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum1; // model type
  if (dum1 == 1)
  {
    set_model_type(RHO_1D_READ);
  }
  else if (dum1 == 2)
  {
    set_model_type(RHO_2D_READ);
  }
  else if (dum1 == 3)
  {
    set_model_type(RHO_3D_READ);
  }

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum1; // compute the r-light curve?
  // 1: lc no estimators
  // 2: lc case with thin cells
  // 3: lc case with thick cells
  // 4: gamma-ray heating case
  globals::do_r_lc = (dum1 != 0);
  if (dum1 > 0)
  {
    globals::do_rlc_est = dum1 - 1;
  }
  assert(dum1 >= 0);
  assert(dum1 <= 4);

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::n_out_it; // number of iterations

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum2; // change speed of light?
  globals::CLIGHT_PROP = dum2 * CLIGHT;

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::gamma_grey; // use grey opacity for gammas?

  float syn_dir_in[3];
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> syn_dir_in[0] >> syn_dir_in[1] >> syn_dir_in[2]; // components of syn_dir

  const double rr = (syn_dir_in[0] * syn_dir_in[0]) + (syn_dir_in[1] * syn_dir_in[1]) + (syn_dir_in[2] * syn_dir_in[2]);
  // ensure that this vector is normalised.
  if (rr > 1.e-6)
  {
    globals::syn_dir[0] = syn_dir_in[0] / sqrt(rr);
    globals::syn_dir[1] = syn_dir_in[1] / sqrt(rr);
    globals::syn_dir[2] = syn_dir_in[2] / sqrt(rr);
  }
  else
  {
    const double z1 = 1. - (2 * gsl_rng_uniform(rng));
    const double z2 = gsl_rng_uniform(rng) * 2.0 * PI;
    globals::syn_dir[2] = z1;
    globals::syn_dir[0] = sqrt( (1. - (z1 * z1))) * cos(z2);
    globals::syn_dir[1] = sqrt( (1. - (z1 * z1))) * sin(z2);
  }

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::opacity_case; // opacity choice

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::rho_crit_para; //free parameter for calculation of rho_crit
  printout("input: rho_crit_para %g\n", globals::rho_crit_para);
  /// he calculation of rho_crit itself depends on the time, therfore it happens in grid_init and update_grid

  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::debug_packet; // activate debug output for packet
  // select a negative value to deactivate

  // Do we start a new simulation or, continue another one?
  int continue_flag;
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> continue_flag;
  globals::simulation_continued_from_saved = (continue_flag == 1);
  if (globals::simulation_continued_from_saved)
    printout("input: resuming simulation from saved point\n");
  else
    printout("input: starting a new simulation\n");

  /// Wavelength (in Angstroms) at which the parameterisation of the radiation field
  /// switches from the nebular approximation to LTE.
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum2;  // free parameter for calculation of rho_crit
  globals::nu_rfcut = CLIGHT / (dum2 * 1e-8);
  printout("input: nu_rfcut %g\n", globals::nu_rfcut);

  /// Sets the number of initial LTE timesteps for NLTE runs
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::n_lte_timesteps;
  #ifdef FORCE_LTE
    printout("input: this is a pure LTE run\n");
  #else
    printout("input: this is a NLTE run\n");
    printout("input: do the first %d timesteps in LTE\n", globals::n_lte_timesteps);
  #endif

  if (NT_ON)
  {
    if (NT_SOLVE_SPENCERFANO)
      printout("input: Non-thermal ionisation with a Spencer-Fano solution is switched on for this run.\n");
    else
      printout("input: Non-thermal ionisation with the work function approximation is switched on for this run.\n");
    #ifdef FORCE_LTE
      printout("input: Non-thermal ionisation requires the code to run in non-LTE mode. Remove macro FORCE_LTE and recompile!\n");
      abort();
    #endif
  }
  else
    printout("input: No non-thermal ionisation is used in this run.\n");

  #if (NO_LUT_PHOTOION)
    printout("Corrphotoioncoeff is calculated from the radiation field at each timestep in each modelgrid cell (no LUT).\n");
  #else
    printout("Corrphotoioncoeff is calculated from LTE lookup tables (ratecoeff.dat) and corrphotoionrenorm estimator.\n");
  #endif

  #if (NO_LUT_BFHEATING)
    printout("bfheating coefficients are calculated from the radiation field at each timestep in each modelgrid cell (no LUT).\n");
  #else
    printout("bfheating coefficients are calculated from LTE lookup tables (ratecoeff.dat) and bfheatingestimator.\n");
  #endif

  /// Set up initial grey approximation?
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::cell_is_optically_thick >> globals::n_grey_timesteps;
  printout("input: cells with Thomson optical depth > %g are treated in grey approximation for the first %d timesteps\n", globals::cell_is_optically_thick, globals::n_grey_timesteps);

  /// Limit the number of bf-continua
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::max_bf_continua;
  if (globals::max_bf_continua == -1)
  {
    printout("input: use all bf-continua\n");
    globals::max_bf_continua = 1e6;
  }
  else
  {
    printout("input: use only %d bf-continua per ion\n", globals::max_bf_continua);
  }

  /// The following parameters affect the DO_EXSPEC mode only /////////////////
  /// Read number of MPI tasks for exspec
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum1;
  #ifdef DO_EXSPEC
    nprocs_exspec = dum1;
    printout("input: DO_EXSPEC ... extract spectra for %d MPI tasks\n", nprocs_exspec);
    printout("input: DO_EXSPEC ... and %d packets per task\n", globals::npkts);
  #endif

  /// Extract line-of-sight dependent information of last emission for spectrum_res
  ///   if 1, yes
  ///   if 0, no
  /// Only affects runs with DO_EXSPEC. But then it needs huge amounts of memory!!!
  /// Thus be aware to reduce MNUBINS for this mode of operation!
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> dum1;
  #ifdef DO_EXSPEC
    do_emission_res = dum1;
    if (do_emission_res == 1) printout("input: DO_EXSPEC ... extract LOS dependent emission information\n");
  #endif

  /// To reduce the work imbalance between different MPI tasks I introduced a diffusion
  /// for kpkts, since it turned out that this work imbalance was largely dominated
  /// by continuous collisional interactions. By introducing a diffusion time for kpkts
  /// this loop is broken. The following two parameters control this approximation.
  /// Parameter one (a float) gives the relative fraction of a time step which individual
  /// kpkts live. Parameter two (an int) gives the number of time steps for which we
  /// want to use this approximation
  assert(get_noncommentline(file, line));
  std::stringstream(line) >> globals::kpktdiffusion_timescale >> globals::n_kpktdiffusion_timesteps;
  printout("input: kpkts diffuse %g of a time step's length for the first %d time steps\n", globals::kpktdiffusion_timescale, globals::n_kpktdiffusion_timesteps);

  file.close();
}



/*
int compare_linelistentry(const void *p1, const void *p2)
{
  linelist_entry *a1, *a2;
  a1 = (linelist_entry *)(p1);
  a2 = (linelist_entry *)(p2);
  //printf("%d %d %d %d %g\n",a1->elementindex,a1->ionindex,a1->lowerlevelindex,a1->upperlevelindex,a1->nu);
  //printf("%d %d %d %d %g\n",a2->elementindex,a2->ionindex,a2->lowerlevelindex,a2->upperlevelindex,a2->nu);
  //printf("%g\n",a2->nu - a1->nu);
  if (a1->nu - a2->nu < 0)
    return 1;
  else if (a1->nu - a2->nu > 0)
    return -1;
  else
    return 0;
}
*/


void update_parameterfile(int nts)
/// Subroutine to read in input parameters from input.txt.
{
  printout("Update input.txt for restart at timestep %d...", nts);

  std::ifstream file("input.txt");
  assert(file.is_open());

  std::ofstream fileout("input.txt.tmp");
  assert(fileout.is_open());

  std::string line;

  // FILE *input_file = fopen_required("input.txt", "r+");
  //setvbuf(input_file, NULL, _IOLBF, 0);

  char c_line[1024];
  int noncomment_linenum = -1;
  while (std::getline(file, line))
  {
    if (!lineiscommentonly(line))
    {
      noncomment_linenum++;  // line number starting from 0, ignoring comment and blank lines (that start with '#')

      // if (!preceeding_comment && noncomment_linenum < inputlinecommentcount - 1)
      // {
      //   fileout << '#' << inputlinecomments[noncomment_linenum] << '\n';
      // }

      // overwrite particular lines to enable restarting from the current timestep
      if (noncomment_linenum == 2)
      {
        /// Number of start and end time step
        sprintf(c_line, "%3.3d %3.3d", nts, globals::ftstep);
        // line.assign(c_line);
        line.replace(0, strlen(c_line), c_line);
      }
      else if (noncomment_linenum == 16)
      {
        /// resume from gridsave file
        sprintf(c_line, "%d", 1); /// Force continuation
        line.assign(c_line);
      }

      if (noncomment_linenum < inputlinecommentcount)
      {
        const int commentstart = 25;

        // truncate any existing comment on the line
        if (line.find("#") != std::string::npos)
        {
          line.resize(line.find("#"));
        }

        line.resize(commentstart, ' ');
        line.append("# ");
        line.append(inputlinecomments[noncomment_linenum]);
      }
    }

    fileout << line << '\n';
  }

  fileout.close();
  file.close();

  std::remove("input.txt");
  std::rename("input.txt.tmp", "input.txt");

  printout("done\n");
}



void time_init(void)
// Subroutine to define the time steps.
{
  /// t=globals::tmin is the start of the calcualtion. t=globals::tmax is the end of the calculation.
  /// globals::ntstep is the number of time steps wanted. For now the time steps
  /// are logarithmically spaced, but the code should be written such that
  /// they don't have to be.

  globals::time_step = (struct time *) calloc(globals::ntstep + 1, sizeof(struct time));

  /// Now set the individual time steps
  for (int n = 0; n < globals::ntstep; n++)
  {
    // For logarithmic steps, the logarithmic inverval will be
    const double dlogt = (log(globals::tmax) - log(globals::tmin)) / globals::ntstep;
    globals::time_step[n].start = globals::tmin * exp(n * dlogt);
    globals::time_step[n].mid = globals::tmin * exp((n + 0.5) * dlogt);
    globals::time_step[n].width = (globals::tmin * exp((n + 1) * dlogt)) - globals::time_step[n].start;

    // for constant timesteps
    // const double dt = (globals::tmax - globals::tmin) / globals::ntstep;
    // globals::time_step[n].start = globals::tmin + n * dt;
    // globals::time_step[n].width = dt;
    // globals::time_step[n].mid = globals::time_step[n].start + 0.5 * globals::time_step[n].width;
  }

  // /// Part log, part fixed timestepss
  // const double t_transition = 40. * DAY; // transition from logarithmic to fixed timesteps
  // const double maxtsdelta = 0.5 * DAY; // maximum timestep width in fixed part
  // assert(t_transition > globals::tmin);
  // assert(t_transition < globals::tmax);
  // const int nts_fixed = ceil((globals::tmax - t_transition) / maxtsdelta);
  // const double fixed_tsdelta = (globals::tmax - t_transition) / nts_fixed;
  // assert(nts_fixed >= 0);
  // assert(nts_fixed <= globals::ntstep);
  // const int nts_log = globals::ntstep - nts_fixed;
  // assert(nts_log >= 0);
  // assert(nts_log <= globals::ntstep);
  // assert((nts_log + nts_fixed) == globals::ntstep);
  // for (int n = 0; n < globals::ntstep; n++)
  // {
  //   if (n < nts_log)
  //   {
  //     // For logarithmic steps, the logarithmic inverval will be
  //     const double dlogt = (log(t_transition) - log(globals::tmin)) / nts_log;
  //     globals::time_step[n].start = globals::tmin * exp(n * dlogt);
  //     globals::time_step[n].mid = globals::tmin * exp((n + 0.5) * dlogt);
  //     globals::time_step[n].width = (globals::tmin * exp((n + 1) * dlogt)) - globals::time_step[n].start;
  //   }
  //   else
  //   {
  //     // for constant timesteps
  //     const double prev_start = n > 0 ? (globals::time_step[n - 1].start + globals::time_step[n - 1].width) : globals::tmin;
  //     globals::time_step[n].start = prev_start;
  //     globals::time_step[n].width = fixed_tsdelta;
  //     globals::time_step[n].mid = globals::time_step[n].start + 0.5 * globals::time_step[n].width;
  //   }
  // }

  // to limit the timestep durations
  // const double maxt = 0.5 * DAY;
  // for (int n = globals::ntstep - 1; n > 0; n--)
  // {
  //   if (globals::time_step[n].width > maxt)
  //   {
  //     const double boundaryshift = globals::time_step[n].width - maxt;
  //     globals::time_step[n].width -= boundaryshift;
  //     globals::time_step[n].start += boundaryshift;
  //     globals::time_step[n - 1].width += boundaryshift;
  //   }
  //   else if (n < globals::ntstep - 1 && globals::time_step[n + 1].width > maxt)
  //   {
  //     printout("TIME: Keeping logarithmic durations for timesteps <= %d\n", n);
  //   }
  // }
  // assert(globals::time_step[0].width <= maxt); // no solution is possible with these constraints!

  // check consistency of start + width = start_next
  for (int n = 1; n < globals::ntstep; n++)
  {
    assert(fabs((globals::time_step[n - 1].start + globals::time_step[n - 1].width) / globals::time_step[n].start) - 1 < 0.001);
  }
  assert(fabs((globals::time_step[globals::ntstep - 1].start + globals::time_step[globals::ntstep - 1].width) / globals::tmax) - 1 < 0.001);

  for (int n = 0; n < globals::ntstep; n++)
  {
    globals::time_step[n].pellet_decays = 0;
    globals::time_step[n].positron_dep = 0.;
    globals::time_step[n].gamma_dep = 0.0;
    globals::time_step[n].cmf_lum = 0.0;
  }

  /// and add a dummy timestep which contains the endtime
  /// of the calculation
  globals::time_step[globals::ntstep].start = globals::tmax;
  globals::time_step[globals::ntstep].mid = globals::tmax;
}


void write_timestep_file(void)
{
  FILE *timestepfile = fopen_required("timesteps.out", "w");
  fprintf(timestepfile, "#timestep tstart_days tmid_days twidth_days\n");
  for (int n = 0; n < globals::ntstep; n++)
  {
    fprintf(timestepfile, "%d %lg %lg %lg\n", n, globals::time_step[n].start / DAY, globals::time_step[n].mid / DAY, globals::time_step[n].width / DAY);
  }
  fclose(timestepfile);
}
