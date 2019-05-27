#ifndef EMG_OPTIMIZER_IMPL_H
#define EMG_OPTIMIZER_IMPL_H

#include "emg_optimizer.h"

class EMGOptimizerImpl : public EMGOptimizer
{
  public:
    /* Constructor class. Parameters accessed in objectiveFunc() class */
    EMGOptimizerImpl( OpenSim::Model&, ActuatorsList&, const size_t );
    ~EMGOptimizerImpl();
 
    int objectiveFunc( const SimTK::Vector&, bool, SimTK::Real& ) const;

    SimTK::Vector CalculateTorques( SimTK::State&, SimTK::Vector ) const;
    bool StoreSamples( SimTK::Vector&, SimTK::Vector&, SimTK::Vector& );
    void ResetSamplesStorage();

    SimTK::Vector GetInitialParameters();
    
  private:
    OpenSim::Model& internalModel;
    ActuatorsList& actuatorsList;
    SimTK::Array_<SimTK::Vector> emgSamplesList, positionSamplesList, torqueSamplesList;
    SimTK::Vector activationFactorsList;

    //Log optimizationLog;
};

#endif // EMG_OPTIMIZER_IMPL_H
