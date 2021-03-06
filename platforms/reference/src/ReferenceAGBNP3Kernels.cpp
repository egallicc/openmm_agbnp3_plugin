/* -------------------------------------------------------------------------- *
 *                               OpenMM-AGBNP3                                *
 * -------------------------------------------------------------------------- */

#include <iostream>

#include <cstdlib>
#include "ReferenceAGBNP3Kernels.h"
#include "AGBNP3Force.h"
#include "openmm/OpenMMException.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/reference/RealVec.h"
#include "openmm/reference/ReferencePlatform.h"
#include "nblist.h"
#include "agbnp3.h"

using namespace AGBNP3Plugin;
using namespace OpenMM;
using namespace std;

static vector<RealVec>& extractPositions(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return *((vector<RealVec>*) data->positions);
}

static vector<RealVec>& extractForces(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return *((vector<RealVec>*) data->forces);
}

// Initializes AGBNP3 library
void ReferenceCalcAGBNP3ForceKernel::initialize(const System& system, const AGBNP3Force& force) {
    
    int numParticles = force.getNumParticles();
    std::vector<int> neighbors;
    double dielectric_out = 80.0;
    double dielectric_in = 1.0;

    //input lists
    x.resize(numParticles);
    y.resize(numParticles);
    z.resize(numParticles);
    radius.resize(numParticles);
    charge.resize(numParticles);
    gamma.resize(numParticles);
    sgamma.resize(numParticles);
    alpha.resize(numParticles);
    salpha.resize(numParticles);
    hbtype.resize(numParticles);
    hbcorr.resize(numParticles);
    ishydrogen.resize(numParticles);
    sp.resize(numParticles);
    br.resize(numParticles);
    surf_area.resize(numParticles);

    //initializes connection table (up 6 connections per atom)
    NeighList *conntbl;
    conntbl = (NeighList *)malloc(sizeof(NeighList));
    nblist_reset_neighbor_list(conntbl);
    nblist_reallocate_neighbor_list(conntbl, numParticles, 6*numParticles);
    
    double nm2ang = 10.0;
    double kjmol2kcalmol = 1./4.184;
    int nneigh = 0;
    for (int i = 0; i < numParticles; i++){
      force.getParticleParameters(i, radius[i], charge[i], gamma[i], alpha[i], hbtype[i],
				  hbcorr[i], ishydrogen[i], neighbors);
      radius[i] *= nm2ang;
      gamma[i] *= kjmol2kcalmol/(nm2ang*nm2ang);
      alpha[i] *= kjmol2kcalmol*nm2ang*nm2ang*nm2ang;
      hbcorr[i] *= kjmol2kcalmol;
      sgamma[i] = 0.0;
      salpha[i] = 0.0;

      // store neighbors in connection table
      conntbl->neighl[i] = &(conntbl->neighl1[nneigh]);
      conntbl->nne[i] = neighbors.size();
      //      std::cout << "N" << i << " " << hbtype[i] << " ";
      for(int j=0;j<neighbors.size();j++) {
      	conntbl->neighl1[nneigh++] = neighbors[j];
      //	std::cout << neighbors[j] << " ";
      }
      //std::cout << std::endl ;

    }

    // count and store hydrogens
    std::vector<int> ihydrogen;
    for (int i = 0; i < numParticles; i++){
      if(ishydrogen[i]) ihydrogen.push_back(i);
    }
    int nhydrogen = ihydrogen.size();

    //allocates space for derivatives
    dgbdr = (double (*)[3])malloc(numParticles*sizeof(double [3]));
    dvwdr = (double (*)[3])malloc(numParticles*sizeof(double [3]));
    decav = (double (*)[3])malloc(numParticles*sizeof(double [3]));
    dehb = (double (*)[3])malloc(numParticles*sizeof(double [3]));

    //create AGBNP3 instance
    agbnp3_initialize();
    int verbose = 0;
    agbnp3_new(&agbnp3_tag, numParticles, 
	      &x[0], &y[0], &z[0], &radius[0], 
	      &charge[0], dielectric_in, dielectric_out,
	      &gamma[0], &sgamma[0],
	      &alpha[0], &salpha[0],
	      &hbtype[0], &hbcorr[0],
	      nhydrogen, &ihydrogen[0], 
	      conntbl, verbose);

    nblist_delete_neighbor_list(conntbl);
}

static int niter = 0;
static double eold = 0.0;
static double *xold = 0, *yold = 0, *zold = 0;   

double ReferenceCalcAGBNP3ForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    vector<RealVec>& pos = extractPositions(context);
    vector<RealVec>& force = extractForces(context);
    int numParticles = radius.size();
    double energy = 0.0;
    double egb, evdw, ecorr_vdw, ecav, ecorr_cav, ehb; 
    int init = 0;
    double wgb = 1.0; //weights
    double wcav = 1.0;
    double wvdw = 1.0;
    double whb = 1.0;
      

    // Compute the interactions.
    double nm2ang = 10.0;
    for (int i = 0; i < numParticles; i++) {
      x[i] = nm2ang*pos[i][0];
      y[i] = nm2ang*pos[i][1];
      z[i] = nm2ang*pos[i][2];
    }
    agbnp3_ener(agbnp3_tag, init, &x[0], &y[0], &z[0],
		&sp[0], &br[0], &mol_volume, &surf_area[0],
		&egb, dgbdr,
		&evdw, &ecorr_vdw, dvwdr,
		&ecav, &ecorr_cav, decav,
		&ehb, dehb);
    energy = wgb*egb + wvdw*(evdw + ecorr_vdw) + wcav*(ecav + ecorr_cav) + whb*ehb;

#ifdef NOTNOW
    for(int i=0;i<numParticles;i++)
      std::cout << "Br" << i << "=" << br[i] << std::endl;

    std::cout << "Egb =" << egb << std::endl;
    std::cout << "Evdw=" << evdw + ecorr_vdw << std::endl;
    std::cout << "Ecav=" << ecav + ecorr_cav << std::endl;
    std::cout << "Ehb =" << ehb << std::endl;
    std::cout << "Eagbnp =" << energy << std::endl;
#endif    

    //returns the gradients
    double kcalang2kjmolnm = 4.184/0.1;
    for(int i = 0; i < numParticles; i++){
      force[i][0] -= kcalang2kjmolnm*(wgb*dgbdr[i][0]+wvdw*dvwdr[i][0]+wcav*decav[i][0]+whb*dehb[i][0]);
      force[i][1] -= kcalang2kjmolnm*(wgb*dgbdr[i][1]+wvdw*dvwdr[i][1]+wcav*decav[i][1]+whb*dehb[i][1]);
      force[i][2] -= kcalang2kjmolnm*(wgb*dgbdr[i][2]+wvdw*dvwdr[i][2]+wcav*decav[i][2]+whb*dehb[i][2]);
    }

    //returns energy
    double kcalmol2kjmol = 4.184;
    return kcalmol2kjmol*energy;
}

void ReferenceCalcAGBNP3ForceKernel::copyParametersToContext(ContextImpl& context, const AGBNP3Force& force) {
  std::vector<int> neighbors;
    if (force.getNumParticles() != radius.size())
        throw OpenMMException("updateParametersInContext: The number of AGBNP3 particles has changed");
    for (int i = 0; i < force.getNumParticles(); i++) {
        force.getParticleParameters(i, radius[i], charge[i], gamma[i], alpha[i], hbtype[i],
				    hbcorr[i], ishydrogen[i], neighbors);
    }
}
