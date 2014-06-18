#include "SimG4Core/Application/interface/RunManagerMT.h"
#include "SimG4Core/Application/interface/PrimaryTransformer.h"
#include "SimG4Core/Application/interface/SimRunInterface.h"
#include "SimG4Core/Application/interface/RunAction.h"
#include "SimG4Core/Application/interface/EventAction.h"
#include "SimG4Core/Application/interface/StackingAction.h"
#include "SimG4Core/Application/interface/TrackingAction.h"
#include "SimG4Core/Application/interface/SteppingAction.h"
#include "SimG4Core/Application/interface/SimTrackManager.h"
#include "SimG4Core/Application/interface/G4SimEvent.h"
#include "SimG4Core/Application/interface/ParametrisedEMPhysics.h"

#include "SimG4Core/Geometry/interface/DDDWorld.h"
#include "SimG4Core/Geometry/interface/G4LogicalVolumeToDDLogicalPartMap.h"
#include "SimG4Core/Geometry/interface/SensitiveDetectorCatalog.h"

#include "SimG4Core/SensitiveDetector/interface/AttachSD.h"

#include "SimG4Core/Physics/interface/PhysicsListFactory.h"
#include "SimG4Core/Watcher/interface/SimWatcherFactory.h"
#include "SimG4Core/MagneticField/interface/FieldBuilder.h"
#include "SimG4Core/MagneticField/interface/Field.h"

#include "MagneticField/Engine/interface/MagneticField.h"

#include "SimG4Core/Notification/interface/SimG4Exception.h"
#include "SimG4Core/Notification/interface/BeginOfJob.h"
#include "SimG4Core/Notification/interface/CurrentG4Track.h"

#include "DetectorDescription/Core/interface/DDCompactView.h"

#include "SimDataFormats/Forward/interface/LHCTransportLinkContainer.h"

#include "HepPDT/defs.h"
#include "HepPDT/TableBuilder.hh"
#include "HepPDT/ParticleDataTable.hh"

#include "G4GeometryManager.hh"
#include "G4StateManager.hh"
#include "G4ApplicationState.hh"
#include "G4MTRunManagerKernel.hh"
#include "G4UImanager.hh"

#include "G4EventManager.hh"
#include "G4Run.hh"
#include "G4Event.hh"
#include "G4TransportationManager.hh"
#include "G4ParticleTable.hh"
#include "G4Field.hh"
#include "G4FieldManager.hh"

#include "G4GDMLParser.hh"
#include "G4SystemOfUnits.hh"

#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>

#include "FWCore/MessageLogger/interface/MessageLogger.h"

//#include "SimG4Core/Application/interface/ExceptionHandler.h"

static
void createWatchers(const edm::ParameterSet& iP,
		    SimActivityRegistry& iReg,
		    std::vector<boost::shared_ptr<SimWatcher> >& oWatchers,
		    std::vector<boost::shared_ptr<SimProducer> >& oProds
   )
{
  using namespace std;
  using namespace edm;
  vector<ParameterSet> watchers;
  try {
    watchers = iP.getParameter<vector<ParameterSet> >("Watchers");
  } catch( edm::Exception) {
  }
  
  for(vector<ParameterSet>::iterator itWatcher = watchers.begin();
      itWatcher != watchers.end();
      ++itWatcher) {
    std::auto_ptr<SimWatcherMakerBase> maker( 
      SimWatcherFactory::get()->create
      (itWatcher->getParameter<std::string> ("type")) );
    if(maker.get()==0) {
      throw SimG4Exception("Unable to find the requested Watcher");
    }
    
    boost::shared_ptr<SimWatcher> watcherTemp;
    boost::shared_ptr<SimProducer> producerTemp;
    maker->make(*itWatcher,iReg,watcherTemp,producerTemp);
    oWatchers.push_back(watcherTemp);
    if(producerTemp) {
       oProds.push_back(producerTemp);
    }
  }
}

RunManagerMT::RunManagerMT(edm::ParameterSet const & p) 
  :   m_generator(0), m_nonBeam(p.getParameter<bool>("NonBeamEvent")), 
      m_primaryTransformer(0), 
      m_managerInitialized(false), 
      m_runInitialized(false), m_runTerminated(false), m_runAborted(false),
      firstRun(true),
      m_pUseMagneticField(p.getParameter<bool>("UseMagneticField")),
      m_currentRun(0), m_currentEvent(0), m_simEvent(0), 
      m_PhysicsTablesDir(p.getParameter<std::string>("PhysicsTablesDirectory")),
      m_StorePhysicsTables(p.getParameter<bool>("StorePhysicsTables")),
      m_RestorePhysicsTables(p.getParameter<bool>("RestorePhysicsTables")),
      m_EvtMgrVerbosity(p.getUntrackedParameter<int>("G4EventManagerVerbosity",0)),
      m_pField(p.getParameter<edm::ParameterSet>("MagneticField")),
      m_pGenerator(p.getParameter<edm::ParameterSet>("Generator")),
      m_pPhysics(p.getParameter<edm::ParameterSet>("Physics")),
      m_pRunAction(p.getParameter<edm::ParameterSet>("RunAction")),      
      m_pEventAction(p.getParameter<edm::ParameterSet>("EventAction")),
      m_pStackingAction(p.getParameter<edm::ParameterSet>("StackingAction")),
      m_pTrackingAction(p.getParameter<edm::ParameterSet>("TrackingAction")),
      m_pSteppingAction(p.getParameter<edm::ParameterSet>("SteppingAction")),
      m_G4Commands(p.getParameter<std::vector<std::string> >("G4Commands")),
      m_p(p), m_fieldBuilder(0),
      m_theLHCTlinkTag(p.getParameter<edm::InputTag>("theLHCTlinkTag"))
{    
  //m_HepMC = iC.consumes<edm::HepMCProduct>(p.getParameter<edm::InputTag>("HepMCProduct"));

  G4RunManagerKernel *kernel = G4MTRunManagerKernel::GetRunManagerKernel();
  if(!kernel) m_kernel = new G4MTRunManagerKernel();
  else {
    m_kernel = dynamic_cast<G4MTRunManagerKernel *>(kernel);
    assert(m_kernel);
  }

  //m_CustomExceptionHandler = new ExceptionHandler(this) ;
    
  m_check = p.getUntrackedParameter<bool>("CheckOverlap",false);
  m_WriteFile = p.getUntrackedParameter<std::string>("FileNameGDML","");
  m_FieldFile = p.getUntrackedParameter<std::string>("FileNameField","");
  if("" != m_FieldFile) { m_FieldFile += ".txt"; } 

  m_userRunAction = 0;
  m_runInterface = 0;

#ifdef MK_IGNORE
  createWatchers(m_p, m_registry, m_watchers, m_producers);
#endif

  m_InTag = m_pGenerator.getParameter<std::string>("HepMCProductLabel") ;

}

RunManagerMT::~RunManagerMT() 
{ 
  if (!m_runTerminated) { terminateRun(); }
  G4StateManager::GetStateManager()->SetNewState(G4State_Quit);
  G4GeometryManager::GetInstance()->OpenGeometry();
  //   if (m_kernel!=0) delete m_kernel; 
  delete m_runInterface;
}

void RunManagerMT::initG4(const DDCompactView *pDD, const MagneticField *pMF, const HepPDT::ParticleDataTable *fPDGTable, const edm::EventSetup & es)
{
  if (m_managerInitialized) return;
  
  // DDDWorld: get the DDCV from the ES and use it to build the World
  G4LogicalVolumeToDDLogicalPartMap map_;
  SensitiveDetectorCatalog catalog_;
  m_world.reset(new DDDWorld(pDD, map_, catalog_, m_check));
  m_registry.dddWorldSignal_(m_world.get());

#ifdef MK_IGNORE
  if("" != m_WriteFile) {
    G4GDMLParser gdml;
    gdml.Write(m_WriteFile, m_world->GetWorldVolume());
  }

  if (m_pUseMagneticField)
    {
      // setup the magnetic field
      const GlobalPoint g(0.,0.,0.);

      m_fieldBuilder = new sim::FieldBuilder(pMF, m_pField);
      G4TransportationManager * tM = 
	G4TransportationManager::GetTransportationManager();
      m_fieldBuilder->build( tM->GetFieldManager(),
			     tM->GetPropagatorInField());
      if("" != m_FieldFile) { 
	DumpMagneticField(tM->GetFieldManager()->GetDetectorField()); 
      }
    }

  // we need the track manager now
  m_trackManager = std::auto_ptr<SimTrackManager>(new SimTrackManager);

  // attach sensitive detector
  m_attach = new AttachSD;
  
  std::pair< std::vector<SensitiveTkDetector*>,
    std::vector<SensitiveCaloDetector*> > sensDets = 
    m_attach->create(*m_world,*pDD,catalog_,m_p,m_trackManager.get(),
		     m_registry);
      
  m_sensTkDets.swap(sensDets.first);
  m_sensCaloDets.swap(sensDets.second);
     
  edm::LogInfo("SimG4CoreApplication") 
    << " RunManagerMT: Sensitive Detector "
    << "building finished; found " 
    << m_sensTkDets.size()
    << " Tk type Producers, and " 
    << m_sensCaloDets.size() 
    << " Calo type producers ";

  m_primaryTransformer = new PrimaryTransformer();
#endif
  std::auto_ptr<PhysicsListMakerBase> 
    physicsMaker(PhysicsListFactory::get()->create(
      m_pPhysics.getParameter<std::string> ("type")));
  if (physicsMaker.get()==0) {
    throw SimG4Exception("Unable to find the Physics list requested");
  }
  m_physicsList = 
    physicsMaker->make(map_,fPDGTable,m_fieldBuilder,m_pPhysics,m_registry);

  PhysicsList* phys = m_physicsList.get(); 
  if (phys==0) { 
    throw SimG4Exception("Physics list construction failed!"); 
  }

  // adding GFlash, Russian Roulette for eletrons and gamma, 
  // step limiters on top of any Physics Lists
  phys->RegisterPhysics(new ParametrisedEMPhysics("EMoptions",m_pPhysics));
  
  m_kernel->SetPhysics(phys);
  m_kernel->InitializePhysics();

#ifdef MK_IGNORE
  m_physicsList->ResetStoredInAscii();
  std::string tableDir = m_PhysicsTablesDir;
  if (m_RestorePhysicsTables) {
    m_physicsList->SetPhysicsTableRetrieved(tableDir);
  }
#endif
  if (m_kernel->RunInitialization()) { m_managerInitialized = true; }
  else { 
    throw SimG4Exception("G4RunManagerKernel initialization failed!"); 
  }

#ifdef MK_IGNORE
  if (m_StorePhysicsTables)
    {
      std::ostringstream dir;
      dir << tableDir << '\0';
      std::string cmd = std::string("/control/shell mkdir -p ")+tableDir;
      if (!std::ifstream(dir.str().c_str(), std::ios::in))
        G4UImanager::GetUIpointer()->ApplyCommand(cmd);
      m_physicsList->StorePhysicsTable(tableDir);
    }

  // FIXME: this will not work in the end because stuff must be
  // retrieved from EventSetup in a CMSSW tread. It works in my tests
  // because my simple test does not have any watchers
  //
  //tell all interesting parties that we are beginning the job
  BeginOfJob aBeginOfJob(&es);
  m_registry.beginOfJobSignal_(&aBeginOfJob);

  initializeUserActions();

#endif
  for (unsigned it=0; it<m_G4Commands.size(); it++) {
    edm::LogInfo("SimG4CoreApplication") << "RunManagerMT:: Requests UI: "
                                         << m_G4Commands[it];
    G4UImanager::GetUIpointer()->ApplyCommand(m_G4Commands[it]);
  }

  // If the Geant4 particle table is needed, decomment the lines below
  //
  //  G4cout << "Output of G4ParticleTable DumpTable:" << G4endl;
  //  G4ParticleTable::GetParticleTable()->DumpTable("ALL");
  
  initializeRun();
  firstRun= false;
}

void RunManagerMT::stopG4()
{
  //std::cout << "RunManagerMT::stopG4" << std::endl;
  G4StateManager::GetStateManager()->SetNewState(G4State_Quit);
  if (!m_runTerminated) { terminateRun(); }
}

void RunManagerMT::produce(edm::Event& inpevt, const edm::EventSetup & es)
{
#ifdef MK_SERIAL
  m_currentEvent = generateEvent(inpevt);
  m_simEvent = new G4SimEvent();
  m_simEvent->hepEvent(m_generator->genEvent());
  m_simEvent->weight(m_generator->eventWeight());
  if (m_generator->genVertex() !=0 ) {
    m_simEvent->collisionPoint(
      math::XYZTLorentzVectorD(m_generator->genVertex()->x()/centimeter,
			       m_generator->genVertex()->y()/centimeter,
			       m_generator->genVertex()->z()/centimeter,
			       m_generator->genVertex()->t()/second));
  }
  if (m_currentEvent->GetNumberOfPrimaryVertex()==0) {
    edm::LogError("SimG4CoreApplication") 
      << " RunManagerMT::produce event " << inpevt.id().event()
      << " with no G4PrimaryVertices \n  Aborting Run" ;
       
    abortRun(false);
  } else {
    m_kernel->GetEventManager()->ProcessOneEvent(m_currentEvent);
  }
    
  edm::LogInfo("SimG4CoreApplication") 
    << " RunManagerMT: saved : Event  " << inpevt.id().event() 
    << " of weight " << m_simEvent->weight()
    << " with " << m_simEvent->nTracks() << " tracks and " 
    << m_simEvent->nVertices()
    << " vertices, generated by " << m_simEvent->nGenParts() << " particles ";
#endif
}
 
G4Event * RunManagerMT::generateEvent(edm::Event & inpevt)
{                       
  if (m_currentEvent!=0) { delete m_currentEvent; }
  m_currentEvent = 0;
  if (m_simEvent!=0) { delete m_simEvent; }
  m_simEvent = 0;
  G4Event * evt = new G4Event(inpevt.id().event());
  
  edm::Handle<edm::HepMCProduct> HepMCEvt;
  
  inpevt.getByLabel( m_InTag, HepMCEvt ) ;
  
#ifdef MK_SERIAL
  m_generator->setGenEvent(HepMCEvt->GetEvent());

  // required to reset the GenParticle Id for particles transported 
  // along the beam pipe
  // to their original value for SimTrack creation
  resetGenParticleId( inpevt );

  if (!m_nonBeam) 
    {
      m_generator->HepMC2G4(HepMCEvt->GetEvent(),evt);
    }
  else 
    {
      m_generator->nonBeamEvent2G4(HepMCEvt->GetEvent(),evt);
    }
 
#endif
  return evt;
}

void RunManagerMT::abortEvent()
{
  if (m_runTerminated) { return; }
  G4Track* t =
    m_kernel->GetEventManager()->GetTrackingManager()->GetTrack();
  t->SetTrackStatus(fStopAndKill) ;
     
  // CMS-specific act
  //
  TrackingAction* uta =
    (TrackingAction*)m_kernel->GetEventManager()->GetUserTrackingAction() ;
  uta->PostUserTrackingAction(t) ;

  m_currentEvent->SetEventAborted();
    
  // do NOT call this method for now
  // because it'll set abortRequested=true (withing G4EventManager)
  // this will make Geant4, in the event *next* after the aborted one
  // NOT to get the primamry, thus there's NOTHING to trace, and it goes
  // to the end of G4Event::DoProcessing(G4Event*), where abortRequested
  // will be reset to true again
  //    
  //m_kernel->GetEventManager()->AbortCurrentEvent();
  //
  // instead, mimic what it does, except (re)setting abortRequested
  //
  m_kernel->GetEventManager()->GetStackManager()->clear() ;
  m_kernel->GetEventManager()->GetTrackingManager()->EventAborted() ;
     
  G4StateManager* stateManager = G4StateManager::GetStateManager();
  stateManager->SetNewState(G4State_GeomClosed);
}

void RunManagerMT::initializeUserActions()
{
  m_runInterface = new SimRunInterface(this, false);

  m_userRunAction = new RunAction(m_pRunAction, m_runInterface);
  Connect(m_userRunAction);

  G4EventManager * eventManager = m_kernel->GetEventManager();
  eventManager->SetVerboseLevel(m_EvtMgrVerbosity);

  if (m_generator!=0) {
    EventAction * userEventAction = 
      new EventAction(m_pEventAction, m_runInterface, m_trackManager.get());
    Connect(userEventAction);
    eventManager->SetUserAction(userEventAction);

    TrackingAction* userTrackingAction = 
      new TrackingAction(userEventAction,m_pTrackingAction);
    Connect(userTrackingAction);
    eventManager->SetUserAction(userTrackingAction);
	
    SteppingAction* userSteppingAction = 
      new SteppingAction(userEventAction,m_pSteppingAction); 
    Connect(userSteppingAction);
    eventManager->SetUserAction(userSteppingAction);

    eventManager->SetUserAction(new StackingAction(m_pStackingAction));

  } else {
    edm::LogWarning("SimG4CoreApplication") << " RunManagerMT: WARNING : "
					    << "No generator; initialized "
					    << "only RunAction!";
  }
}

void RunManagerMT::initializeRun()
{
  m_runInitialized = false;
  if (m_currentRun==0) { m_currentRun = new G4Run(); }
  G4StateManager::GetStateManager()->SetNewState(G4State_GeomClosed);
  if (m_userRunAction!=0) { m_userRunAction->BeginOfRunAction(m_currentRun); }
  m_runAborted = false;
  m_runInitialized = true;
}
 
void RunManagerMT::terminateRun()
{
  if (m_userRunAction!=0) {
    m_userRunAction->EndOfRunAction(m_currentRun);
    delete m_userRunAction; 
    m_userRunAction = 0;
  }
  /*
  if (m_currentRun!=0) { 
    delete m_currentRun; 
    m_currentRun = 0; 
  }
  */
  if (m_kernel!=0 && !m_runTerminated) {
    delete m_currentEvent;
    m_currentEvent = 0;
    delete m_simEvent;
    m_simEvent = 0;
    m_kernel->RunTermination();
    m_runInitialized = false;
    m_runTerminated = true;
  }  
}

void RunManagerMT::abortRun(bool softAbort)
{
  m_runAborted = false;
  if (!softAbort) { abortEvent(); }
  if (m_currentRun!=0) { delete m_currentRun; m_currentRun = 0; }
  m_runInitialized = false;
  m_runAborted = true;
  terminateRun();
}

void RunManagerMT::resetGenParticleId( edm::Event& inpevt ) 
{
  edm::Handle<edm::LHCTransportLinkContainer> theLHCTlink;
  inpevt.getByLabel( m_theLHCTlinkTag, theLHCTlink );
  if ( theLHCTlink.isValid() ) {
    m_trackManager->setLHCTransportLink( theLHCTlink.product() );
  }
}

SimTrackManager* RunManagerMT::GetSimTrackManager()
{
  return m_trackManager.get();
}

void  RunManagerMT::Connect(RunAction* runAction)
{
  runAction->m_beginOfRunSignal.connect(m_registry.beginOfRunSignal_);
  runAction->m_endOfRunSignal.connect(m_registry.endOfRunSignal_);
}

void  RunManagerMT::Connect(EventAction* eventAction)
{
  eventAction->m_beginOfEventSignal.connect(m_registry.beginOfEventSignal_);
  eventAction->m_endOfEventSignal.connect(m_registry.endOfEventSignal_);
}

void  RunManagerMT::Connect(TrackingAction* trackingAction)
{
  trackingAction->m_beginOfTrackSignal.connect(m_registry.beginOfTrackSignal_);
  trackingAction->m_endOfTrackSignal.connect(m_registry.endOfTrackSignal_);
}

void  RunManagerMT::Connect(SteppingAction* steppingAction)
{
  steppingAction->m_g4StepSignal.connect(m_registry.g4StepSignal_);
}

void RunManagerMT::DumpMagneticField(const G4Field* field) const
{
  std::ofstream fout(m_FieldFile.c_str(), std::ios::out);
  if(fout.fail()){
    edm::LogWarning("SimG4CoreApplication") 
      << " RunManagerMT WARNING : "
      << "error opening file <" << m_FieldFile << "> for magnetic field";
  } else {
    double rmax = 9000*mm;
    double zmax = 16000*mm;

    double dr = 5*cm;
    double dz = 20*cm;

    int nr = (int)(rmax/dr);
    int nz = 2*(int)(zmax/dz);

    double r = 0.0;
    double z0 = -zmax;
    double z;

    double phi = 0.0;
    double cosf = cos(phi);
    double sinf = sin(phi);

    double point[4] = {0.0,0.0,0.0,0.0};
    double bfield[3] = {0.0,0.0,0.0};

    fout << std::setprecision(6); 
    for(int i=0; i<=nr; ++i) {
      z = z0;
      for(int j=0; j<=nz; ++j) {
        point[0] = r*cosf;
	point[1] = r*sinf;
	point[2] = z;
        field->GetFieldValue(point, bfield); 
        fout << "R(mm)= " << r/mm << " phi(deg)= " << phi/degree 
	     << " Z(mm)= " << z/mm << "   Bz(tesla)= " << bfield[2]/tesla 
	     << " Br(tesla)= " << (bfield[0]*cosf + bfield[1]*sinf)/tesla
	     << " Bphi(tesla)= " << (bfield[0]*sinf - bfield[1]*cosf)/tesla
	     << G4endl;
	z += dz;
      }
      r += dr;
    }
    
    fout.close();
  }
}
