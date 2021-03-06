/* -------------------------------------------------------------------------- *
 *                                   OpenMM-AGBNP3                            *
 * -------------------------------------------------------------------------- */

#include "AGBNP3Force.h"
#include "openmm/internal/AssertionUtilities.h"
#include "openmm/Context.h"
#include "openmm/Platform.h"
#include "openmm/System.h"
#include "openmm/VerletIntegrator.h"
#include <cmath>
#include <iostream>
#include <vector>

using namespace AGBNP3Plugin;
using namespace OpenMM;
using namespace std;

extern "C" OPENMM_EXPORT void registerAGBNP3OpenCLKernelFactories();

void testForce() {
    // Create a chain of particles connected by bonds.
    const int numBonds = 10;
    const int numParticles = numBonds+1;
    System system;
    vector<Vec3> positions(numParticles);
    for (int i = 0; i < numParticles; i++) {
        system.addParticle(1.0);
        positions[i] = Vec3(0.1*i, 0.1*i, -0.1*i);
    }
    AGBNP3Force* force = new AGBNP3Force();
    system.addForce(force);
    double radius = 0.15;
    double charge = 1.0;
    double gamma = 0.1;
    double alpha = -1.0;
    int hbtype = 0;
    double hbcorr = 0.0;
    int ishydrogen = 0;
    for (int i = 0; i < numParticles; i++){
      force->addParticle(radius, charge, gamma, alpha, hbtype, hbcorr, ishydrogen);
      charge *= -1.0;
    }
    for (int i = 0; i < numBonds; i++)
        force->addParticleConnection(i, i+1);
    
    // Compute the forces and energy.

    VerletIntegrator integ(1.0);
    Platform& platform = Platform::getPlatformByName("OpenCL");
    Context context(system, integ, platform);
    context.setPositions(positions);
    State state = context.getState(State::Energy | State::Forces);
    
    double energy1 = state.getPotentialEnergy();
    std::cout << "Energy:" <<  energy1  << std::endl;

    // validate force by moving an atom
    double offset = 1.e-4;
    int pmove = 3;
    positions[pmove][0] += offset;
    context.setPositions(positions);
    double energy2 = context.getState(State::Energy).getPotentialEnergy();
    double de = -state.getForces()[pmove][0]*offset;

    std::cout << "Energy Change: " <<  energy2 - energy1  << std::endl;
    std::cout << "Energy Change from Gradient: " <<  de  << std::endl;


}

int main(int argc, char* argv[]) {
    try {
        registerAGBNP3OpenCLKernelFactories();
        if (argc > 1)
            Platform::getPlatformByName("OpenCL").setPropertyDefaultValue("OpenCLPrecision", string(argv[1]));
        testForce();
        //testChangingParameters();
    }
    catch(const std::exception& e) {
        std::cout << "exception: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Done" << std::endl;
    return 0;
}
