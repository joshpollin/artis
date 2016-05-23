#include "sn3d.h"
#include "grid_init.h"
#include "packet_init.h"
#include "vectors.h"


// private functions
void setup_packets(int pktnumberoffset);
void place_pellet(const struct grid *restrict grid_ptr, double e0, int m, int n, int pktnumberoffset);


void packet_init(int middle_iteration, int my_rank)
{
  if (!continue_simulation)
  {
    int pktnumberoffset = middle_iteration*npkts;
    setup_packets(pktnumberoffset);
    char filename[100];               /// this must be long enough to hold "packetsxx.tmp" where xx is the number of "middle" iterations
    sprintf(filename,"packets%d_%d_odd.tmp",middle_iteration,my_rank);
    FILE *packets_file;
    if ((packets_file = fopen(filename, "wb")) == NULL)
    {
      printf("[fatal]: packet_init: Cannot open packets file\n");
      exit(0);
    }
    fwrite(&pkt[0], sizeof(PKT), npkts, packets_file);
    //write_packets(packets_file);
    fclose(packets_file);

    /* Consistency check to debug read/write
    PKT testpkt[MPKTS];
    int n;
    if (i > 0)
    {
    sprintf(filename,"packets%d_%d.tmp",i-1,my_rank);
    if ((packets_file = fopen(filename, "rb")) == NULL)
    {
    printf("Cannot open packets file\n");
    exit(0);
  }
    fread(&testpkt, sizeof(PKT), npkts, packets_file);
    fclose(packets_file);

    for (n=0; n<npkts; n++)
    {
    if (testpkt[n].number-pkt[n].number != 0) printout("fatal 1!!! %d, %d\n",testpkt[n].number,pkt[n].number);
    if (testpkt[n].where-pkt[n].where != 0) printout("fatal 2!!!\n");      /// The grid cell that the packet is in.
    if (testpkt[n].type-pkt[n].type != 0) printout("fatal 3!!!\n");       /// Identifies the type of packet (k-, r-, etc.)
    if (testpkt[n].pos[0]-pkt[n].pos[0] != 0) printout("fatal 4!!!\n");  /// Position of the packet (x,y,z).
    if (testpkt[n].pos[1]-pkt[n].pos[1] != 0) printout("fatal 5!!!\n");  /// Position of the packet (x,y,z).
    if (testpkt[n].pos[2]-pkt[n].pos[2] != 0) printout("fatal 6 !!!\n");  /// Position of the packet (x,y,z).
    if (testpkt[n].dir[0]-pkt[n].dir[0] != 0) printout("fatal 7!!!\n");  /// Direction of propagation. (x,y,z). Always a unit vector.
    if (testpkt[n].dir[1]-pkt[n].dir[1] != 0) printout("fatal 8!!!\n");  /// Direction of propagation. (x,y,z). Always a unit vector.
    if (testpkt[n].dir[2]-pkt[n].dir[2] != 0) printout("fatal 9!!!\n");  /// Direction of propagation. (x,y,z). Always a unit vector.
    if (testpkt[n].last_cross-pkt[n].last_cross != 0) printout("fatal 10!!!\n"); /// To avoid rounding errors on cell crossing.
    if (testpkt[n].tdecay-pkt[n].tdecay != 0) printout("fatal 11!!!\n");  /// Time at which pellet decays.
    if (testpkt[n].e_cmf-pkt[n].e_cmf != 0) printout("fatal 12!!!\n");   /// The energy the packet carries in the co-moving frame.
    if (testpkt[n].e_rf-pkt[n].e_rf != 0) printout("fatal 13!!!\n");    /// The energy the packet carries in the rest frame.
    if (testpkt[n].nu_cmf-pkt[n].nu_cmf != 0) printout("fatal 14!!!\n");  /// The frequency in the co-moving frame.
    if (testpkt[n].nu_rf-pkt[n].nu_rf != 0) printout("fatal 15!!!\n");   /// The frequency in the rest frame.
    if (testpkt[n].escape_type-pkt[n].escape_type != 0) printout("fatal 16!!!\n"); /// Flag to tell us in which form it escaped from the grid.
    if (testpkt[n].escape_time-pkt[n].escape_time != 0) printout("fatal 17!!!\n");
    if (testpkt[n].scat_count-pkt[n].scat_count != 0) printout("fatal 18!!!\n");  /// WHAT'S THAT???
    if (testpkt[n].next_trans-pkt[n].next_trans != 0) printout("fatal 19!!!\n");
    if (testpkt[n].interactions-pkt[n].interactions != 0) printout("fatal 20!!!\n");/// debug: number of interactions the packet undergone
    if (testpkt[n].last_event-pkt[n].last_event != 0) printout("fatal 21!!!\n");  /// debug: stores information about the packets history
  }
  }
    */
  }
}


void setup_packets(int pktnumberoffset)
/// Subroutine that initialises the packets if we start a new simulation.
{
  CELL *grid_ptr;
  // float cont[MGRID+1];

  /// The total number of pellets that we want to start with is just
  /// npkts. The total energy of the pellets is given by etot.
  double etot = (ENICKEL + ECOBALT) * mni56 / MNI56;
  etot += (E48V + E48CR) * mcr48 / MCR48;
  etot += (E52FE + E52MN) * mfe52 / MFE52;
  printout("etot %g\n", etot);
  printout("ENICKEL, ECOBALT ECOBALT_GAMMA%g %g %g\n",ENICKEL/MEV, ECOBALT/MEV, ECOBALT_GAMMA/MEV);
  printout("E48CR, E48V %g %g\n",E48CR/MEV, E48V/MEV);
  printout("E52FE, E52MN %g %g\n",E52FE/MEV, E52MN/MEV);

  /// So energy per pellet is
  double e0 = etot / npkts / n_out_it / n_middle_it;
  printout("e0 %g\n", e0);

  /* Now place the pellets in the ejecta and decide at what time
  they will decay. */

  /* Need to get a normalisation factor. */
  float norm = 0.0;
  for (int m = 0; m < ngrid; m++)
  {
    grid_ptr = &cell[m];
    cont[m] = norm;
    //printf("%g %g %g\n", (fni(grid_ptr)*(ENICKEL + ECOBALT)/MNI56),(f52fe(grid_ptr)*(E52FE + E52MN)/MFE52),(f48cr(grid_ptr)*(E48V + E48CR)/MCR48));
    norm += get_rhoinit(grid_ptr->modelgridindex) * vol_init(grid_ptr) *
      ((fni(grid_ptr)*(ENICKEL + ECOBALT)/56.)
       +(f52fe(grid_ptr)*(E52FE + E52MN)/52.)
       +(f48cr(grid_ptr)*(E48V + E48CR)/48.));
  }
  cont[ngrid] = norm;

  if (npkts > MPKTS)
  {
    printout("Too many packets. Abort.\n");
    exit(0);
  }

  int n = 0;
  int packet_reset = 0;
  printout("starting loop over npkts\n");
  while (n < npkts)
  {
    /// Get random number.
    int mabove = ngrid;
    int mbelow = 0;
    double zrand = gsl_rng_uniform(rng);

    while (mabove != (mbelow + 1))
    {
      int m;

      if (mabove != (mbelow + 2))
        m = (mabove + mbelow) / 2;
      else
        m = mbelow + 1;

      if (cont[m] > (zrand * norm))
        mabove = m;
      else
        mbelow = m;
    }

    if (cont[mbelow] > (zrand * norm))
    {
      printout("mbelow %d cont[mbelow] %g zrand*norm %g\n", mbelow, cont[mbelow], zrand*norm);
      exit(0);
    }
    if ((cont[mabove] < (zrand * norm)) && (mabove != ngrid))
    {
      printout("mabove %d cont[mabove] %g zrand*norm %g\n", mabove, cont[mabove], zrand*norm);
      exit(0);
    }

    int m = mbelow;
    //printout("chosen cell %d (%d, %g, %g)\n", m, ngrid, zrand, norm);
    //exit(0);
    /*
    m=0;
    double runtot = 0.0;
    while (runtot < (zrand))
    {
      grid_ptr = &cell[m];
      runtot += grid_ptr->rho_init * fni(grid_ptr) * vol_init(grid_ptr) / norm;
      m++;
    }
    m = m - 1;
    */

    grid_ptr = &cell[m];
    if (m >= ngrid)
    {
      printout("Failed to place pellet. Abort.\n");
      exit(0);
    }

    /// Pellet is in cell m, pointer is grid_ptr.
    //  printout("Pellet in cell %d\n",m);
    place_pellet(grid_ptr, e0, m, n, pktnumberoffset);

    #ifdef NO_INITIAL_PACKETS
    if (pkt[n].tdecay < tmax && pkt[n].tdecay > tmin)
      n++;
    else
      packet_reset++;
    #else
    if (pkt[n].tdecay < tmax)
      n++;
    else
      packet_reset++;
    #endif
  }

  /// Some fraction of the packets we reassigned because they were not going
  /// to activate in the time of interest so need to renormalise energies
  /// to account for this.
  for (int n = 0; n < npkts; n++)
  {
    pkt[n].e_cmf = pkt[n].e_cmf * npkts / (npkts + packet_reset);
    pkt[n].interactions = 0;
  }
  printout("radioactive energy which will be freed during simulation time %g\n", etot*npkts/(npkts+packet_reset));
}



///***************************************************************************/
double fni(const CELL *grid_ptr)
/// Subroutine that gives the Ni56 mass fraction.
{
  if (model_type == RHO_UNIFORM)
  {
    double dcen[3];
    dcen[0] = grid_ptr->pos_init[0] + (0.5*wid_init);
    dcen[1] = grid_ptr->pos_init[1] + (0.5*wid_init);
    dcen[2] = grid_ptr->pos_init[2] + (0.5*wid_init);

    double r_on_rmax = vec_len(dcen) / rmax;
    double m_r = pow(r_on_rmax,3) * mtot / MSUN; //this is the mass enclosed up to radius r in units of the total eject mass

    if (m_r < 0.5)
      return(1.0);
    else if (m_r < 0.75)
      return(1.0 - ((m_r - 0.5)*4));
    else
      return(0.0);
  }
  else if (model_type == RHO_1D_READ || model_type == RHO_2D_READ)
  {
    /* This is a 1-D model read in. Just find value from input file and return. */
    /*
    double dcen[3];
    dcen[0] = grid_ptr->pos_init[0] + (0.5*wid_init);
    dcen[1] = grid_ptr->pos_init[1] + (0.5*wid_init);
    dcen[2] = grid_ptr->pos_init[2] + (0.5*wid_init);

    double radial_pos = vec_len(dcen);
    if (radial_pos < rmax)
    {
      fraction = fni_model[0];
      for (m = 0; m < (npts_model-1); m++)
      {
        if (radial_pos > (vout_model[m] * tmin))
        {
          fraction = fni_model[m+1];
        }
      }
    }
    else
    {
      fraction = 0.0;
    }*/
    return get_fni(grid_ptr->modelgridindex);
  }
  else if (model_type == RHO_3D_READ)
  {
    /* This is a 3-D model read in. Just return input value. */
    //fraction = grid_ptr->f_ni;
    return get_fni(grid_ptr->modelgridindex);
  }
  else
  {
    printout("Unknown model_type (packet_init). Abort.\n");
    exit(0);
  }

  return -99;
}


///***************************************************************************/
double f52fe(const CELL *grid_ptr)
/// Subroutine that gives the Fe52 mass fraction.
{
  if (model_type == RHO_UNIFORM)
  {
    return 0.0;
  }
  else if (model_type == RHO_1D_READ || model_type == RHO_2D_READ)
  {
    return get_f52fe(grid_ptr->modelgridindex);
  }
  else if (model_type == RHO_3D_READ)
  {
    /* This is a 3-D model read in. Just return input value. */
    //fraction = grid_ptr->f_ni;
    return get_f52fe(grid_ptr->modelgridindex);
  }
  else
  {
    printout("Unknown model_type (packet_init). Abort.\n");
    exit(0);
  }

  return -99;
}

///***************************************************************************/
double f48cr(const CELL *grid_ptr)
/// Subroutine that gives the Cr48 mass fraction.
{
  if (model_type == RHO_UNIFORM)
  {
    return 0.0;
  }
  else if (model_type == RHO_1D_READ || model_type == RHO_2D_READ)
  {
    return get_f48cr(grid_ptr->modelgridindex);
  }
  else if (model_type == RHO_3D_READ)
  {
    /* This is a 3-D model read in. Just return input value. */
    //fraction = grid_ptr->f_ni;
    return get_f48cr(grid_ptr->modelgridindex);
  }
  else
  {
    printout("Unknown model_type (packet_init). Abort.\n");
    exit(0);
  }

  return -99;
}



///***************************************************************************/
void place_pellet(const struct grid *restrict grid_ptr, double e0, int m, int n, int pktnumberoffset)
/// This subroutine places pellet n with energy e0 in cell m pointed to by grid_ptr.
{
  /// First choose a position for the pellet. In the cell.
  /// n is the index of the packet. m is the index for the grid cell.
  pkt[n].where = m;
  pkt[n].number = n + pktnumberoffset;  ///record the packets number for debugging

  double zrand = gsl_rng_uniform_pos(rng);
  pkt[n].pos[0] = grid_ptr->pos_init[0] + (zrand * wid_init);
  zrand = gsl_rng_uniform_pos(rng);
  pkt[n].pos[1] = grid_ptr->pos_init[1] + (zrand * wid_init);
  zrand = gsl_rng_uniform_pos(rng);
  pkt[n].pos[2] = grid_ptr->pos_init[2] + (zrand * wid_init);

  /*first choose which of the decay chains to sample*/
  double prob_chain[3];
  prob_chain[0] = fni(grid_ptr) * (ENICKEL + ECOBALT) / MNI56;
  prob_chain[1] = f52fe(grid_ptr) * (E52FE + E52MN) / MFE52;
  prob_chain[2] = f48cr(grid_ptr) * (E48V + E48CR) / MCR48;

  double zrand3 = gsl_rng_uniform(rng) * (prob_chain[0] + prob_chain[1] + prob_chain[2]);
  zrand = gsl_rng_uniform(rng);
  if (zrand3 <= prob_chain[0])
  {
    /// Now choose whether it's going to be a nickel or cobalt pellet and
    /// mark it as such.
    if (zrand < (ENICKEL / (ENICKEL + ECOBALT)))
    {
      pkt[n].type = TYPE_NICKEL_PELLET;
    }
    else
    {
      zrand = gsl_rng_uniform(rng);

      if (zrand < ECOBALT_GAMMA / ECOBALT)
        pkt[n].type = TYPE_COBALT_PELLET;
      else
        pkt[n].type = TYPE_COBALT_POSITRON_PELLET;
    }

    /// Now choose the decay time.
    if (pkt[n].type == TYPE_NICKEL_PELLET)
    {
      zrand = gsl_rng_uniform(rng);
      pkt[n].tdecay = -TNICKEL * log(zrand);
    }
    else if (pkt[n].type == TYPE_COBALT_PELLET || pkt[n].type == TYPE_COBALT_POSITRON_PELLET)
    {
      zrand = gsl_rng_uniform(rng);
      double zrand2 = gsl_rng_uniform(rng);
      pkt[n].tdecay = (-TNICKEL * log(zrand)) + (-TCOBALT * log(zrand2));
    }
  }
  else if (zrand3 <= (prob_chain[0] + prob_chain[1]))
  {
    /// Now choose whether it's going to be a 52Fe or 52Mn pellet and
    /// mark it as such.
    if (zrand < (E52FE / (E52FE + E52MN)))
      pkt[n].type = TYPE_52FE_PELLET;
    else
      pkt[n].type = TYPE_52MN_PELLET;

    /// Now choose the decay time.
    if (pkt[n].type == TYPE_52FE_PELLET)
    {
      zrand = gsl_rng_uniform(rng);
      pkt[n].tdecay = -T52FE * log(zrand);
    }
    else if (pkt[n].type == TYPE_52MN_PELLET)
    {
      zrand = gsl_rng_uniform(rng);
      double zrand2 = gsl_rng_uniform(rng);
      pkt[n].tdecay = (-T52FE * log(zrand)) + (-T52MN * log(zrand2));
    }
  }
  else
  {
    /// Now choose whether it's going to be a 48Cr or 48V pellet and
    /// mark it as such.
    zrand = gsl_rng_uniform(rng);
    if (zrand < (E48CR / (E48CR + E48V)))
      pkt[n].type = TYPE_48CR_PELLET;
    else
      pkt[n].type = TYPE_48V_PELLET;

    /// Now choose the decay time.
    if (pkt[n].type == TYPE_48CR_PELLET)
    {
      zrand = gsl_rng_uniform(rng);
      pkt[n].tdecay = -T48CR * log(zrand);
    }
    else if (pkt[n].type == TYPE_48V_PELLET)
    {
      zrand = gsl_rng_uniform(rng);
      double zrand2 = gsl_rng_uniform(rng);
      pkt[n].tdecay = (-T48CR * log(zrand)) + (-T48V * log(zrand2));
    }
  }
  /// Now assign the energy to the pellet.
  pkt[n].e_cmf = e0;
}



///***************************************************************************/
void write_packets(FILE *packets_file)
{
  for (int i = 0; i < npkts; i++)
  {
   fprintf(packets_file,"%d ",pkt[i].number);
   fprintf(packets_file,"%d ",pkt[i].where);
   fprintf(packets_file,"%d ",pkt[i].type);
   fprintf(packets_file,"%lg %lg %lg ",pkt[i].pos[0],pkt[i].pos[1],pkt[i].pos[2]);
   fprintf(packets_file,"%lg %lg %lg ",pkt[i].dir[0],pkt[i].dir[1],pkt[i].dir[2]);
   fprintf(packets_file,"%d ",pkt[i].last_cross);
   fprintf(packets_file,"%g ",pkt[i].tdecay);
   fprintf(packets_file,"%g ",pkt[i].e_cmf);
   fprintf(packets_file,"%g ",pkt[i].e_rf);
   fprintf(packets_file,"%g ",pkt[i].nu_cmf);
   fprintf(packets_file,"%g ",pkt[i].nu_rf);
   fprintf(packets_file,"%d ",pkt[i].escape_type);
   fprintf(packets_file,"%d ",pkt[i].escape_time);
   fprintf(packets_file,"%d ",pkt[i].scat_count);
   fprintf(packets_file,"%d ",pkt[i].next_trans);
   fprintf(packets_file,"%d ",pkt[i].interactions);
   fprintf(packets_file,"%d ",pkt[i].last_event);
   fprintf(packets_file,"%d ",pkt[i].emissiontype);
   fprintf(packets_file,"%lg %lg %lg ",pkt[i].em_pos[0],pkt[i].em_pos[1],pkt[i].em_pos[2]);
   fprintf(packets_file,"%d ",pkt[i].absorptiontype);
   fprintf(packets_file,"%lg ",pkt[i].absorptionfreq);
   fprintf(packets_file,"%d ",pkt[i].nscatterings);
   fprintf(packets_file,"%d ",pkt[i].em_time);
   fprintf(packets_file,"%lg %lg %lg ",pkt[i].absorptiondir[0],pkt[i].absorptiondir[1],pkt[i].absorptiondir[2]);
   fprintf(packets_file,"%lg %lg ",pkt[i].stokes_qu[0], pkt[i].stokes_qu[1]);
   fprintf(packets_file,"%lg %lg %lg ",pkt[i].pol_dir[0], pkt[i].pol_dir[1], pkt[i].pol_dir[2]);
   fprintf(packets_file,"\n");
  }
}


///***************************************************************************/
void read_packets(FILE *packets_file)
{
  for (int i = 0; i < npkts; i++)
  {
   fscanf(packets_file,"%d ",&pkt[i].number);
   fscanf(packets_file,"%d ",&pkt[i].where);
   fscanf(packets_file,"%d ",&pkt[i].type);
   fscanf(packets_file,"%lg %lg %lg ",&pkt[i].pos[0],&pkt[i].pos[1],&pkt[i].pos[2]);
   fscanf(packets_file,"%lg %lg %lg ",&pkt[i].dir[0],&pkt[i].dir[1],&pkt[i].dir[2]);
   fscanf(packets_file,"%d ",&pkt[i].last_cross);
   fscanf(packets_file,"%lg ",&pkt[i].tdecay);
   fscanf(packets_file,"%lg ",&pkt[i].e_cmf);
   fscanf(packets_file,"%lg ",&pkt[i].e_rf);
   fscanf(packets_file,"%lg ",&pkt[i].nu_cmf);
   fscanf(packets_file,"%lg ",&pkt[i].nu_rf);
   fscanf(packets_file,"%d ",&pkt[i].escape_type);
   fscanf(packets_file,"%d ",&pkt[i].escape_time);
   fscanf(packets_file,"%d ",&pkt[i].scat_count);
   fscanf(packets_file,"%d ",&pkt[i].next_trans);
   fscanf(packets_file,"%d ",&pkt[i].interactions);
   fscanf(packets_file,"%d ",&pkt[i].last_event);
   fscanf(packets_file,"%d ",&pkt[i].emissiontype);
   fscanf(packets_file,"%lg %lg %lg ",&pkt[i].em_pos[0],&pkt[i].em_pos[1],&pkt[i].em_pos[2]);
   fscanf(packets_file,"%d ",&pkt[i].absorptiontype);
   fscanf(packets_file,"%lg ",&pkt[i].absorptionfreq);
   fscanf(packets_file,"%d ",&pkt[i].nscatterings);
   fscanf(packets_file,"%d ",&pkt[i].em_time);
   fscanf(packets_file,"%lg %lg %lg ",&pkt[i].absorptiondir[0],&pkt[i].absorptiondir[1],&pkt[i].absorptiondir[2]);
   fscanf(packets_file,"%lg %lg ",&pkt[i].stokes_qu[0], &pkt[i].stokes_qu[1]);
   fscanf(packets_file,"%lg %lg %lg ",&pkt[i].pol_dir[0], &pkt[i].pol_dir[1], &pkt[i].pol_dir[2]);
   fscanf(packets_file,"\n");
  }
}
