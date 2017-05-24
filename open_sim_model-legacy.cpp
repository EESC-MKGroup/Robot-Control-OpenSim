#include <OpenSim/OpenSim.h>
#include <OpenSim/Simulation/Model/Model.h>
#include <OpenSim/Actuators/CoordinateActuator.h>
#include <OpenSim/Simulation/InverseDynamicsSolver.h>

#include <iostream>
#include <string>

#include "robot_control_interface.h"

#include "sensors.h"
#include "data_io.h"
#include "data_logging.h"

#include "emg_optimizer_system.h"

typedef struct _ControlData
{
  OpenSim::Model* osimModel;
  SimTK::State state;
  SimTK::Integrator* integrator;
  OpenSim::Manager* manager;
  OpenSim::InverseDynamicsSolver* idSolver;
  OpenSim::Set<OpenSim::CoordinateActuator> actuatorsList;
  SimTK::Array_<char*> jointNamesList;
  SimTK::Array_<char*> axisNamesList;
  SimTK::Array_<bool> dofsChangedList;
  enum RobotState currentControlState;
  SimTK::Array_<Sensor> emgSensorsList;
  EMGOptimizerSystem* emgProcessor;
}
ControlData;


DECLARE_MODULE_INTERFACE( ROBOT_CONTROL_INTERFACE )


RobotController InitController( const char* data )
{
  DataIO.SetDataType( "JSON" );
  
  ControlData* newController = new ControlData;
  
  try 
  {
    // Create an OpenSim model from XML (.osim) file
    std::cout << "OSim: trying to load model file " << data << std::endl; newController->osimModel = new OpenSim::Model( std::string( "config/robots/" ) + std::string( data ) + ".osim" );
    newController->osimModel->printBasicInfo( std::cout );
    
    newController->osimModel->setGravity( SimTK::Vec3( 0.0, -9.80665, 0.0 ) );
    
    newController->osimModel->setUseVisualizer( true ); // not for RT
    
    // Initialize the system (make copy)
    std::cout << "OSim: initialize state" << std::endl; SimTK::State& localState = newController->osimModel->initSystem();
    std::cout << "OSim: copy state" << std::endl; newController->state = SimTK::State( localState );
    
    OpenSim::Set<OpenSim::Muscle> muscleSet = newController->osimModel->getMuscles();
    std::cout << "OSim: found " << muscleSet.getSize() << " muscles" << std::endl;
    OpenSim::Set<OpenSim::Actuator> actuatorSet = newController->osimModel->getActuators();
    for( int actuatorIndex = 0; actuatorIndex < actuatorSet.getSize(); actuatorIndex++ )
    {
      std::string actuatorName = actuatorSet[ actuatorIndex ].getName();
      if( muscleSet.contains( actuatorName ) )
      {
        //OpenSim::ActivationFiberLengthMuscle* muscle = dynamic_cast<OpenSim::ActivationFiberLengthMuscle*>(&(actuatorSet[ actuatorIndex ]));
        //if( muscle ) muscle->setDefaultActivation( 0.5 );
        std::string sensorConfigurationPath = std::string( "sensors/emg/" ) + actuatorName;
        DataHandle sensorConfiguration = DataIO.GetDataHandler()->LoadStringData( sensorConfigurationPath.c_str() );
        newController->emgSensorsList.push_back( Sensors.Init( sensorConfiguration ) );
        DataIO.GetDataHandler()->UnloadData( sensorConfiguration );
      }
	  else
      {
        OpenSim::CoordinateActuator* actuator = dynamic_cast<OpenSim::CoordinateActuator*>(&(actuatorSet[ actuatorIndex ]));
        newController->actuatorsList.adoptAndAppend( actuator );
        newController->jointNamesList.push_back( (char*) actuator->getCoordinate()->getName().c_str() );
        newController->axisNamesList.push_back( (char*) actuator->getCoordinate()->getName().c_str() );
        newController->dofsChangedList.push_back( true );
      }
    }
    std::cout << "OSim: found " << newController->actuatorsList.getSize() << " joint actuators" << std::endl;
    
    SetControlState( newController, ROBOT_PASSIVE );
    
	newController->emgProcessor = new EMGOptimizerSystem( 5 * muscleSet.getSize(), 1000, localState, *(newController->osimModel) );
	newController->emgProcessor->setParameterLimits( SimTK::Vector( 5 * muscleSet.getSize(), 0.8 ), SimTK::Vector( 5 * muscleSet.getSize(), 1.2 ) ); 

	std::cout << "OSim: generated coordinates size: " << newController->jointNamesList.size() << std::endl;
    newController->idSolver = new OpenSim::InverseDynamicsSolver( *(newController->osimModel) );
	std::cout << "OSim: ID solver created" << std::endl;
    newController->state.updTime() = 0.0; std::cout << "OSim: IK solver set up" << std::endl;
    
    // Create the integrator and manager for the simulation.
    newController->integrator = new SimTK::RungeKuttaMersonIntegrator( newController->osimModel->getMultibodySystem() );
    newController->integrator->setAccuracy( 1.0e-4 ); std::cout << "OSim: integrator created" << std::endl;
    newController->manager = new OpenSim::Manager( *(newController->osimModel), *(newController->integrator) ); 
    
    std::cout << "OSim: integration manager created" << std::endl;
  }
  catch( OpenSim::Exception ex )
  {
    std::cout << ex.getMessage() << std::endl;
    EndController( (RobotController) newController );
    return NULL;
  }
  catch( std::exception ex )
  {
    std::cout << ex.what() << std::endl;
    EndController( (RobotController) newController );
    return NULL;
  }
  catch( ... )
  {
    std::cout << "UNRECOGNIZED EXCEPTION" << std::endl;
    EndController( (RobotController) newController );
    return NULL;
  }
  
  std::cout << "OpenSim model loaded successfully ! (" << newController->osimModel->getNumCoordinates() << " coordinates)" << std::endl;
  
  return (RobotController) newController;
}

void EndController( RobotController ref_controller )
{
  if( ref_controller == NULL ) return;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  delete controller->integrator;
  delete controller->manager;
  delete controller->idSolver;
  delete controller->osimModel;
  
  controller->jointNamesList.clear();
  controller->axisNamesList.clear();
  controller->dofsChangedList.clear();
  
  for( size_t muscleIndex = 0; muscleIndex < controller->emgSensorsList.size(); muscleIndex++ )
    Sensors.End( controller->emgSensorsList[ muscleIndex ] );
  controller->emgSensorsList.clear();
  
  delete controller;
}

size_t GetJointsNumber( RobotController ref_controller )
{
  if( ref_controller == NULL ) return 0;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (size_t) controller->jointNamesList.size();
}

const char** GetJointNamesList( RobotController ref_controller )
{
  if( ref_controller == NULL ) return NULL;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (const char**) controller->jointNamesList.data();
}

const bool* GetJointsChangedList( RobotController ref_controller )
{
  if( ref_controller == NULL ) return NULL;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (const bool*) controller->dofsChangedList.data();
}

size_t GetAxesNumber( RobotController ref_controller )
{
  if( ref_controller == NULL ) return 0;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (size_t) controller->axisNamesList.size();
}

const char** GetAxisNamesList( RobotController ref_controller )
{
  if( ref_controller == NULL ) return NULL;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (const char**) controller->axisNamesList.data();
}

const bool* GetAxesChangedList( RobotController ref_controller )
{
  if( ref_controller == NULL ) return NULL;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (const bool*) controller->dofsChangedList.data();
}

void SetControlState( RobotController ref_controller, enum RobotState newControlState )
{
  if( ref_controller == NULL ) return;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  std::cout << "setting new control state: " << newControlState;
  
  enum SignalProcessingPhase signalProcessingPhase = SIGNAL_PROCESSING_PHASE_MEASUREMENT;

  OpenSim::ForceSet &forceSet = controller->osimModel->updForceSet();
  for( int forceIndex = 0; forceIndex < forceSet.getSize(); forceIndex++ )
	forceSet[ forceIndex ].setDisabled( controller->state, true ); // setAppliesForce -> false

  if( newControlState == ROBOT_OFFSET )
  {
	std::cout << "starting offset phase" << std::endl;
	signalProcessingPhase = SIGNAL_PROCESSING_PHASE_OFFSET;
  }
  else if( newControlState == ROBOT_CALIBRATION )
  {
	std::cout << "starting calibration phase" << std::endl;
	signalProcessingPhase = SIGNAL_PROCESSING_PHASE_CALIBRATION;
  }
  else if( newControlState == ROBOT_PREPROCESSING )
  {
	std::cout << "reseting sampling count" << std::endl;
	controller->emgProcessor->ResetSamplesStorage();
  }
  else 
  {
	if( newControlState == ROBOT_OPERATION )
	{
	  if( controller->currentControlState == ROBOT_PREPROCESSING )
	  {
	    std::cout << "starting optimization" << std::endl;
		SimTK::Vector controls( 5 * controller->osimModel->getMuscles().getSize(), 1.0 );
		SimTK::Optimizer optimizer( *(controller->emgProcessor), SimTK::LBFGSB );
		optimizer.setConvergenceTolerance( 0.05 );
		optimizer.useNumericalGradient( true );
		optimizer.setMaxIterations( 1000 );
		optimizer.setLimitedMemoryHistory( 500 );
		SimTK::Real remainingError = optimizer.optimize( controls );

		OpenSim::ForceSet &forceSet = controller->osimModel->updForceSet();
		for( int forceIndex = 0; forceIndex < forceSet.getSize(); forceIndex++ )
		  forceSet[ forceIndex ].setDisabled( controller->state, false ); // setAppliesForce -> true
	  }
	}
  }
  
  for( size_t muscleIndex = 0; muscleIndex < controller->emgSensorsList.size(); muscleIndex++ )
	Sensors.SetState( controller->emgSensorsList[ muscleIndex ], signalProcessingPhase );

  controller->currentControlState = newControlState;
}

void RunControlStep( RobotController RobotController, RobotVariables** jointMeasuresList, RobotVariables** axisMeasuresList, RobotVariables** jointSetpointsList, RobotVariables** axisSetpointsList, double timeDelta )
{
  if( RobotController == NULL ) return;
  
  ControlData* controller = (ControlData*) RobotController;

  SimTK::Vector emgInputs( controller->emgSensorsList.size() );
  for( size_t muscleIndex = 0; muscleIndex < controller->emgSensorsList.size(); muscleIndex++ )
	  emgInputs[ muscleIndex ] = Sensors.Update( controller->emgSensorsList[ muscleIndex ], NULL );
  SimTK::Vector positionInputs( EMGOptimizerSystem::POSITION_VARIABLES_NUMBER * controller->actuatorsList.getSize() );
  SimTK::Vector accelerationsList( controller->actuatorsList.getSize(), 0.0 );
  for( int jointIndex = 0; jointIndex < controller->actuatorsList.getSize(); jointIndex++ )
  {
    OpenSim::Coordinate* jointCoordinate = controller->actuatorsList[ jointIndex ].getCoordinate();
    jointCoordinate->setValue( controller->state, jointMeasuresList[ jointIndex ]->position );
    jointCoordinate->setSpeedValue( controller->state, jointMeasuresList[ jointIndex ]->velocity );
    accelerationsList[ jointIndex ] = jointMeasuresList[ jointIndex ]->acceleration;
    controller->actuatorsList[ jointIndex ].setForce( controller->state, jointMeasuresList[ jointIndex ]->force );
  }
  
  controller->manager->setInitialTime( 0.0 );
  controller->manager->setFinalTime( timeDelta );
  controller->manager->integrate( controller->state );
  
  controller->osimModel->getMultibodySystem().realize( controller->state, SimTK::Stage::Position );
  controller->osimModel->getMultibodySystem().realize( controller->state, SimTK::Stage::Velocity );
  controller->osimModel->getMultibodySystem().realize( controller->state, SimTK::Stage::Acceleration );
  
  SimTK::Vector idForcesList = controller->idSolver->solve( controller->state, accelerationsList );
  
  SimTK::Vector forceOutputs( EMGOptimizerSystem::FORCE_VARIABLES_NUMBER * controller->actuatorsList.getSize() );
  for( int jointIndex = 0; jointIndex < controller->actuatorsList.getSize(); jointIndex++ )
  {
	  forceOutputs[ jointIndex * EMGOptimizerSystem::FORCE_VARIABLES_NUMBER ] = idForcesList[ jointIndex ];
	  double positionError = ( jointSetpointsList[ jointIndex ]->position - jointMeasuresList[ jointIndex ]->position ) / idForcesList[ jointIndex ];
	  forceOutputs[ jointIndex * EMGOptimizerSystem::FORCE_VARIABLES_NUMBER + 1 ] = positionError / idForcesList[ jointIndex ];
  }

  if( controller->currentControlState == ROBOT_PREPROCESSING )
	  controller->emgProcessor->StoreSamples( emgInputs, positionInputs, forceOutputs );
  else if( controller->currentControlState == ROBOT_OPERATION )
	  controller->emgProcessor->CalculateTorques( controller->state, emgInputs, positionInputs, forceOutputs );
  
  for( int axisIndex = 0; axisIndex < controller->actuatorsList.getSize(); axisIndex++ )
  {
    OpenSim::Coordinate* axisCoordinate = controller->actuatorsList[ axisIndex ].getCoordinate();
    axisMeasuresList[ axisIndex ]->position = axisCoordinate->getValue( controller->state );
    axisMeasuresList[ axisIndex ]->velocity = axisCoordinate->getSpeedValue( controller->state );
    axisMeasuresList[ axisIndex ]->acceleration = axisCoordinate->getAccelerationValue( controller->state );
    axisMeasuresList[ axisIndex ]->force = forceOutputs[ axisIndex * EMGOptimizerSystem::FORCE_VARIABLES_NUMBER ];
	axisMeasuresList[ axisIndex ]->stiffness = forceOutputs[ axisIndex * EMGOptimizerSystem::FORCE_VARIABLES_NUMBER + 1 ];
  }
  
  for( int jointIndex = 0; jointIndex < controller->actuatorsList.getSize(); jointIndex++ )
  {
    jointSetpointsList[ jointIndex ]->position = axisSetpointsList[ jointIndex ]->position;
    jointSetpointsList[ jointIndex ]->velocity = axisSetpointsList[ jointIndex ]->velocity;
    jointSetpointsList[ jointIndex ]->acceleration = axisSetpointsList[ jointIndex ]->acceleration;
    jointSetpointsList[ jointIndex ]->force = axisSetpointsList[ jointIndex ]->force;
  }  
  
  //std::cout << "marker position: " << markerPosition << std::endl;
  std::cout << "joint positions: " << controller->actuatorsList[ 0 ].getCoordinate()->getValue( controller->state ) << ", " << controller->actuatorsList[ 1 ].getCoordinate()->getValue( controller->state ) << std::endl;
}
