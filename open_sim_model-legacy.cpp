#include <OpenSim/OpenSim.h>
#include <Simulation/Model/Model.h>
#include <Simulation/Model/MarkerSet.h>
#include <Simulation/MarkersReference.h>
#include <Simulation/CoordinateReference.h>
#include <Simulation/InverseKinematicsSolver.h>

#include <iostream>
#include <string>

#include "robot_control/interface.h"

typedef struct _ControlData
{
  OpenSim::Model* osimModel;
  SimTK::State state;
  SimTK::Integrator* integrator;
  OpenSim::Manager* manager;
  OpenSim::InverseKinematicsSolver* ikSolver;
  OpenSim::CoordinateSet coordinatesList; 
  SimTK::Array_<OpenSim::CoordinateReference> coordinateReferences;
  SimTK::Array_<char*> jointNames;
  OpenSim::MarkerSet markers;
  OpenSim::MarkersReference markersReference;
  SimTK::Array_<size_t> markerReferenceAxes;
  SimTK::Array_<char*> axisNames;
  std::vector<bool> axesChangedList;
}
ControlData;


DECLARE_MODULE_INTERFACE( ROBOT_CONTROL_INTERFACE )


RobotController InitController( const char* data )
{
  ControlData* newModel = new ControlData;
  
  try 
  {
    // Create an OpenSim model from XML (.osim) file
    std::cout << "OSim: trying to load model file " << data << std::endl; newModel->osimModel = new OpenSim::Model( std::string( "config/robots/" ) + std::string( data ) + ".osim" );
    newModel->osimModel->printBasicInfo( std::cout );
    
    // Initialize the system (make copy)
    std::cout << "OSim: initialize state" << std::endl; SimTK::State& localState = newModel->osimModel->initSystem();
    std::cout << "OSim: copy state" << std::endl; newModel->state = SimTK::State( localState );
    
    OpenSim::JointSet& jointSet = newModel->osimModel->updJointSet();
    std::cout << "OSim: found " << jointSet.getSize() << " joints" << std::endl;
    for( int jointIndex = 0; jointIndex < jointSet.getSize(); jointIndex++ )
    {
      OpenSim::CoordinateSet& jointCoordinateSet = jointSet[ jointIndex ].upd_CoordinateSet();
      std::cout << "OSim: found " << jointCoordinateSet.getSize() << " coordinates in joint " << jointSet[ jointIndex ].getName() << std::endl;
      for( int coordinateIndex = 0; coordinateIndex < jointCoordinateSet.getSize(); coordinateIndex++ )
      {
        newModel->coordinatesList.adoptAndAppend( &(jointCoordinateSet[ coordinateIndex ]) );
        newModel->jointNames.push_back( (char*) jointCoordinateSet[ coordinateIndex ].getName().c_str() );
      }
    }
    
    OpenSim::MarkerSet& markerSet = newModel->osimModel->updMarkerSet();
    std::cout << "OSim: found " << markerSet.getSize() << " markers" << std::endl;
    for( int markerIndex = 0; markerIndex < markerSet.getSize(); markerIndex++ )
    {
      std::string markerName = markerSet[ markerIndex ].getName();
      const char* REFERENCE_AXES_NAMES[ 3 ] = { "_ref_X", "_ref_Y", "_ref_Z" };
      for( size_t referenceAxisIndex = 0; referenceAxisIndex < 3; referenceAxisIndex++ )
      {
        if( markerName.rfind( REFERENCE_AXES_NAMES[ referenceAxisIndex ] ) != std::string::npos )
        {
          std::cout << "OSim: found reference marker " << markerName << std::endl;
          newModel->markers.adoptAndAppend( &(markerSet[ markerIndex ]) );
		  newModel->markersReference._markerNames.push_back( markerName );
		  newModel->markersReference._weights.push_back( 1.0 );
		  newModel->markersReference._markerWeightSet.adoptAndAppend( new OpenSim::MarkerWeight( markerName, 1.0 ) );
          newModel->markerReferenceAxes.push_back( referenceAxisIndex );
          newModel->axisNames.push_back( (char*) markerSet[ markerIndex ].getName().c_str() );
          break;
        }
      }
    }

	newModel->axesChangedList.resize( markerSet.getSize() );
    
	std::cout << "OSim: generated markers reference size" << newModel->markersReference.updMarkerWeightSet().getSize() << std::endl;
	std::cout << "OSim: generated coordinates reference size: " << newModel->coordinateReferences.size() << std::endl;
    //newModel->ikSolver = new OpenSim::InverseKinematicsSolver( *(newModel->osimModel), newModel->markersReference, newModel->coordinateReferences );
	//std::cout << "OSim: IK solver created" << std::endl;
    //newModel->ikSolver->setAccuracy( 1.0e-4 );
    //newModel->state.updTime() = 0.0;
    //newModel->ikSolver->assemble( newModel->state ); std::cout << "OSim: IK solver assembled" << std::endl;
    
    // Create the integrator and manager for the simulation.
    newModel->integrator = new SimTK::RungeKuttaMersonIntegrator( newModel->osimModel->getMultibodySystem() );
    newModel->integrator->setAccuracy( 1.0e-4 ); std::cout << "OSim: integrator created" << std::endl;
    newModel->manager = new OpenSim::Manager( *(newModel->osimModel), *(newModel->integrator) ); 
    
    newModel->manager->setInitialTime( 0.0 );
    newModel->manager->setFinalTime( 0.005 ); std::cout << "OSim: integration manager created" << std::endl;
  }
  catch( OpenSim::Exception ex )
  {
    std::cout << ex.getMessage() << std::endl;
    EndController( (RobotController) newModel );
    return NULL;
  }
  catch( std::exception ex )
  {
    std::cout << ex.what() << std::endl;
    EndController( (RobotController) newModel );
    return NULL;
  }
  catch( ... )
  {
    std::cout << "UNRECOGNIZED EXCEPTION" << std::endl;
    EndController( (RobotController) newModel );
    return NULL;
  }
  
  std::cout << "OpenSim model loaded successfully ! (" << newModel->osimModel->getNumCoordinates() << " coordinates)" << std::endl;
  
  return (RobotController) newModel;
}

void EndController( RobotController RobotController )
{
  if( RobotController == NULL ) return;
  
  ControlData* model = (ControlData*) RobotController;
  
  delete model->integrator;
  delete model->manager;
  //delete model->ikSolver;
  delete model->osimModel;
  
  model->markers.clearAndDestroy();
  model->coordinatesList.clearAndDestroy();
  model->coordinateReferences.clear();  
  
  delete model;
}

size_t GetJointsNumber( RobotController RobotController )
{
  if( RobotController == NULL ) return 0;
  
  ControlData* model = (ControlData*) RobotController;
  
  return (size_t) model->coordinatesList.getSize();
}

const char** GetJointNamesList( RobotController RobotController )
{
  if( RobotController == NULL ) return NULL;
  
  ControlData* model = (ControlData*) RobotController;
  
  return (const char**) model->jointNames.data();
}

const bool* GetJointsChangedList( RobotController ref_controller )
{
  if( ref_controller == NULL ) return NULL;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (const bool*) controller->axesChangedList.data();
}

size_t GetAxesNumber( RobotController RobotController )
{
  if( RobotController == NULL ) return 0;
  
  ControlData* model = (ControlData*) RobotController;
  
  return (size_t) model->markers.getSize();
}

const char** GetAxisNamesList( RobotController RobotController )
{
  if( RobotController == NULL ) return NULL;
  
  ControlData* model = (ControlData*) RobotController;
  
  return (const char**) model->axisNames.data();
}

const bool* GetAxesChangedList( RobotController ref_controller )
{
  if( ref_controller == NULL ) return NULL;
  
  ControlData* controller = (ControlData*) ref_controller;
  
  return (const bool*) controller->axesChangedList.data();
}

void SetControlState( RobotController ref_controller, enum RobotState controlState )
{
  std::cout << "Setting robot control phase: " << controlState << std::endl;
}

void RunControlStep( RobotController RobotController, RobotVariables** jointMeasuresList, RobotVariables** axisMeasuresList, RobotVariables** jointSetpointsList, RobotVariables** axisSetpointsList, double timeDelta )
{
  if( RobotController == NULL ) return;
  
  ControlData* model = (ControlData*) RobotController;
  
  //for( int jointIndex = 0; jointIndex < model->coordinatesList.getSize(); jointIndex++ )
  //{
  //  model->coordinatesList[ jointIndex ].setValue( model->state, jointMeasuresList[ jointIndex ]->position );
  //  model->coordinatesList[ jointIndex ].setSpeedValue( model->state, jointMeasuresList[ jointIndex ]->velocity );
  //}
  
  model->manager->integrate( model->state );
  
  /*for( int jointIndex = 0; jointIndex < model->coordinatesList.getSize(); jointIndex++ )
  {
    jointMeasuresTable[ jointIndex ][ CONTROL_POSITION ] = model->coordinatesList[ jointIndex ].getValue( model->state );
    jointMeasuresTable[ jointIndex ][ CONTROL_VELOCITY ] = model->coordinatesList[ jointIndex ].getSpeedValue( model->state );
    jointMeasuresTable[ jointIndex ][ CONTROL_ACCELERATION ] = model->coordinatesList[ jointIndex ].getAccelerationValue( model->state );
  }*/
  
  SimTK::Vec3 markerPosition, markerVelocity, markerAcceleration;
  model->osimModel->getMultibodySystem().realize( model->state, SimTK::Stage::Position );
  model->osimModel->getMultibodySystem().realize( model->state, SimTK::Stage::Velocity );
  model->osimModel->getMultibodySystem().realize( model->state, SimTK::Stage::Acceleration );
  for( int markerIndex = 0; markerIndex < model->markers.getSize(); markerIndex++ )
  {
    model->osimModel->getSimbodyEngine().getPosition( model->state, model->markers[ markerIndex ].getBody(), model->markers[ markerIndex ].getOffset(), markerPosition );
    model->osimModel->getSimbodyEngine().getVelocity( model->state, model->markers[ markerIndex ].getBody(), model->markers[ markerIndex ].getOffset(), markerVelocity );
    model->osimModel->getSimbodyEngine().getAcceleration( model->state, model->markers[ markerIndex ].getBody(), model->markers[ markerIndex ].getOffset(), markerAcceleration );
    
  //  axisMeasuresList[ markerIndex ]->position = markerPosition[ model->markerReferenceAxes[ markerIndex ] ];
  //  axisMeasuresList[ markerIndex ]->velocity = markerVelocity[ model->markerReferenceAxes[ markerIndex ] ];
  //  axisMeasuresList[ markerIndex ]->acceleration = markerAcceleration[ model->markerReferenceAxes[ markerIndex ] ];
    //axisMeasuresTable[ 0 ][ CONTROL_FORCE ] = jointMeasuresTable[ 0 ][ CONTROL_FORCE ];
    
    //markerPosition[ model->markerReferenceAxes[ markerIndex ] ] = axisSetpointsList[ markerIndex ]->position;
    //model->markersReference.setValue( markerIndex, markerPosition );
	//model->markersReference._markerData->getFrame( 0 ).updMarker( 0 ) = SimTK::Vec3( 0 );
  }
  
  //model->ikSolver->track( model->state );  
  
  //for( int jointIndex = 0; jointIndex < model->coordinatesList.getSize(); jointIndex++ )
  //{
  //  jointSetpointsList[ 0 ]->position = model->coordinatesList[ jointIndex ].getValue( model->state );
  //  jointSetpointsList[ 0 ]->velocity = model->coordinatesList[ jointIndex ].getSpeedValue( model->state );
  //  jointSetpointsList[ 0 ]->acceleration = model->coordinatesList[ jointIndex ].getAccelerationValue( model->state );
    //jointSetpointsTable[ 0 ][ CONTROL_FORCE ] = axisSetpointsTable[ 0 ][ CONTROL_FORCE ];
  //}
  
  std::cout << "marker position: " << markerPosition << std::endl;
  //std::cout << "joint positions: " << model->coordinatesList[0].getValue(model->state) << ", " << model->coordinatesList[1].getValue(model->state) << std::endl;
}
