#include "sn3d.h"

/* Stuff for compton scattering. */

double
sig_comp(pkt_ptr,t_current)
     PKT *pkt_ptr;
     double t_current;
{
  double vel_vec[3];
  double doppler();
  int get_velocity();
  double xx;
  double fmax;
  double sigma_cmf,sigma_rf;
  double sigma_compton_partial();
  /* Start by working out the compton x-section in the co-moving frame.*/

  xx = H * pkt_ptr->nu_cmf / ME / CLIGHT / CLIGHT;

  /* Use this to decide whether the Thompson limit is acceptable. */

  if (xx < THOMSON_LIMIT)
    {
      sigma_cmf = SIGMA_T;
    }
  else
    {
      
      fmax = (1 + (2*xx));
      sigma_cmf = sigma_compton_partial(xx, fmax);
    }
  
  /* Now need to multiply by the electron number density. */
  
  sigma_cmf *= get_nnetot(cell[pkt_ptr->where].modelgridindex);
    
  /* Now need to convert between frames. */

  get_velocity(pkt_ptr->pos, vel_vec, t_current);
  sigma_rf = sigma_cmf * doppler(pkt_ptr->dir, vel_vec);

  return(sigma_rf);
}

/******************************************************************/

/*Routine to deal with physical Compton scattering event. */

int
com_sca(pkt_ptr,t_current)
     PKT *pkt_ptr;
     double t_current;
{
  double zrand;
  double xx, f;
  double choose_f();
  double vel_vec[3];
  double cmf_dir[3], new_dir[3], final_dir[3];
  double cos_theta;
  int scatter_dir();
  double dot();
  double prob_gamma;
  int get_velocity(), angle_ab();
  double doppler();
  double test;
  double thomson_angle();

  //  printout("Compton scattering.\n");

  xx = H * pkt_ptr->nu_cmf / ME / CLIGHT / CLIGHT;

  /* It is known that a Compton scattering event is going to take place.
     We need to do two things - (1) decide whether to convert energy 
     to electron or leave as gamma (2) decide properties of new packet.*/

  /* The probability of giving energy to electron is related to the 
     energy change of the gamma ray. This is equivalent to the choice of
     scattering angle. Probability of scattering into particular angle
     (i.e. final energy) is related to the partial cross-section.*/

  /* Choose a random number to get the eneregy. Want to find the
   factor by which the energy changes "f" such that 
   sigma_partial/sigma_tot = zrand */

  zrand = gsl_rng_uniform(rng);

  if (xx <  THOMSON_LIMIT)
    {
      f = 1.0; //no energy loss
      prob_gamma = 1.0;
    }
  else
    {
      f=choose_f(xx,zrand);
      
      /* Check that f lies between 1.0 and (2xx  + 1) */
      
      if ((f < 1) || (f > (2*xx + 1)))
	{
	  printout("Compton f out of bounds. Abort.\n");
	  exit(0);
	}
      
      /* Prob of keeping gamma ray is...*/
      
      prob_gamma = 1./ f;
      
    }

  zrand = gsl_rng_uniform(rng);
  if (zrand < prob_gamma)
    {
      /* It stays as a gamma ray. Change frequency and direction in 
	 co-moving frame then transfer back to rest frame.*/

      pkt_ptr->nu_cmf = pkt_ptr->nu_cmf / f; //reduce frequency
      

      /* The packet has stored the direction in the rest frame.
	 Use aberation of angles to get this into the co-moving frame.*/

      get_velocity(pkt_ptr->pos, vel_vec, t_current);
      angle_ab(pkt_ptr->dir, vel_vec, cmf_dir);

      /* Now change the direction through the scattering angle.*/

      if (xx <  THOMSON_LIMIT)
	{
	  cos_theta = thomson_angle();
	}
      else
	{
	  cos_theta = 1. - ((f - 1)/xx);
	}

      scatter_dir (cmf_dir,cos_theta,new_dir);

      test = dot(new_dir,new_dir);
      if (fabs(1. - test) > 1.e-8)
	{
	  printout("Not a unit vector - Compton. Abort. %g %g %g\n", f, xx, test);
	  printout("new_dir %g %g %g\n",new_dir[0],new_dir[1],new_dir[2]);
	  printout("cmf_dir %g %g %g\n",cmf_dir[0],cmf_dir[1],cmf_dir[2]);
	  printout("cos_theta %g",cos_theta);
	  exit(0);
	}

      test = dot(new_dir,cmf_dir);

      if (fabs(test - cos_theta) > 1.e-8)
	{
	  printout("Problem with angle - Compton. Abort.\n");
	  exit(0);
	}
      
      /* Now convert back again.*/

      get_velocity(pkt_ptr->pos, vel_vec, (-1.*t_current));
      angle_ab(new_dir, vel_vec, final_dir);

      pkt_ptr->dir[0] = final_dir[0];
      pkt_ptr->dir[1] = final_dir[1];
      pkt_ptr->dir[2] = final_dir[2];

      /*It now has a rest frame direction and a co-moving frequency.
	Just need to set the rest frame energy.*/
     
      get_velocity(pkt_ptr->pos, vel_vec, t_current);

      pkt_ptr->nu_rf = pkt_ptr->nu_cmf / doppler(pkt_ptr->dir, vel_vec);
      pkt_ptr->e_rf = pkt_ptr->e_cmf * pkt_ptr->nu_rf /pkt_ptr->nu_cmf; 

      pkt_ptr->last_cross = NONE; //allow it to re-cross a boundary

    }
  else
    {
      /* It's converted to an e-minus packet.*/
      pkt_ptr->type = TYPE_EMINUS;
      pkt_ptr->absorptiontype = -3;
    }

  return(0);
}
  

/**************************************************************/

/* Routine to compute the partial cross section for Compton scattering.
   xx is the photon energy (in units of electron mass) and f
   is the energy loss factor up to which we wish to integrate.*/

double
sigma_compton_partial(x, f)
     double x, f;
{
  double term1, term2, term3;
  double tot;

  term1 = ( (x*x) - (2*x) - 2 ) * log(f) / x / x;
  term2 = ( ((f*f) -1) / (f * f)) / 2;
  term3 = ( (f - 1) / x) * ( (1/x) + (2/f) + (1/(x*f)));

  tot = 3 * SIGMA_T * (term1 + term2 + term3) / (8 * x);

  return(tot);

}

/**************************************************************/

/* To choose the value of f to integrate to - idea is we want
   sigma_compton_partial(xx,f) = zrand. */

double
choose_f(xx,zrand)
     double xx, zrand;
{
  double norm;
  double fmax;
  double fmin;
  double ftry, try;
  double err;
  int count;

  fmax = 1 + (2*xx);
  fmin = 1;

  norm = zrand * sigma_compton_partial(xx, fmax);

  count = 0;
  err = 1e20;
  
  //printout("new\n");

  while ((err > 1.e-4) && (count < 1000))
    {
      ftry = (fmax + fmin)/2;
      try = sigma_compton_partial(xx, ftry);
      //printout("ftry %g %g %g %g %g\n",ftry, fmin, fmax, try, norm);
      if (try > norm)
	{
	  fmax = ftry;
	  err = (try - norm) / norm;
	}
      else
	{
	  fmin = ftry;
	  err = (norm - try) / norm;
	}
      //      printout("error %g\n",err);
      count += 1;
    }

  if (count == 1000)
    {
      printout("Compton hit 1000 tries. %g %g %g %g %g\n", fmax, fmin, ftry, try, norm);
    }

  return(ftry);

}

/******************************************************************/
double
thomson_angle()
{
  double mu;
  double B_coeff;
  double zrand;
  double t_coeff;

  /*For Thomson scattering we can get the new angle from a random number very easily. */

  zrand = gsl_rng_uniform(rng);

  B_coeff = (8. * zrand) - 4.;

  t_coeff = sqrt( (B_coeff * B_coeff) + 4);
  t_coeff = t_coeff - B_coeff;
  t_coeff = t_coeff / 2;
  t_coeff = pow(t_coeff, (1./3));

  mu = (1./t_coeff) - t_coeff;

  if (fabs(mu) > 1)
    {
      printout("Error in Thomson. Abort.\n");
      exit(0);
    }

  return(mu);
}