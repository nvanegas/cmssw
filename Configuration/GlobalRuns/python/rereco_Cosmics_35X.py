# Auto generated configuration file
# using: 
# Revision: 1.123 
# Source: /cvs_server/repositories/CMSSW/CMSSW/Configuration/PyReleaseValidation/python/ConfigBuilder.py,v 
# with command line options: promptReco -s RAW2DIGI,RECO,DQM --datatier RECO --eventcontent RECO --conditions FrontierConditions_GlobalTag,GR09_31X_V2P::All -n -1 --no_exec --data --magField AutoFromDBCurrent --scenario cosmics
import FWCore.ParameterSet.Config as cms

process = cms.Process('RECO')

# import of standard configurations
process.load('Configuration/StandardSequences/Services_cff')
process.load('FWCore/MessageService/MessageLogger_cfi')
#process.load('Configuration/StandardSequences/MixingNoPileUp_cff')
process.load('Configuration/StandardSequences/GeometryIdeal_cff')
process.load('Configuration/StandardSequences/MagneticField_AutoFromDBCurrent_cff')
process.load('Configuration/StandardSequences/RawToDigi_Data_cff')
process.load('Configuration/StandardSequences/ReconstructionCosmics_cff')
process.load('DQMOffline/Configuration/DQMOfflineCosmics_cff')
process.load('Configuration/StandardSequences/AlCaRecoStreams_cff')
process.load('Configuration/StandardSequences/EndOfProcess_cff')
process.load('Configuration/StandardSequences/FrontierConditions_GlobalTag_cff')
process.load('Configuration/EventContent/EventContentCosmics_cff')

process.configurationMetadata = cms.untracked.PSet(
    version = cms.untracked.string('$Revision: 1.17 $'),
    annotation = cms.untracked.string('promptReco nevts:-1'),
    name = cms.untracked.string('PyReleaseValidation')
)
process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(-1)
)
process.options = cms.untracked.PSet(
    Rethrow = cms.untracked.vstring('ProductNotFound'),
    wantSummary = cms.untracked.bool(True)
)
# Input source
process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(     '/store/data/BeamCommissioning09/Cosmics/RAW/v1/000/123/065/186A3A47-89DD-DE11-B613-001D09F2516D.root')
)

# Output definition
process.FEVT = cms.OutputModule("PoolOutputModule",
    outputCommands = process.RECOEventContent.outputCommands,
    fileName = cms.untracked.string('promptCosmic.root'),
    dataset = cms.untracked.PSet(
        dataTier = cms.untracked.string('RECO'),
        filterName = cms.untracked.string('')
    )
)

# Additional output definition

# Other statements
process.GlobalTag.globaltag = 'GR09_R_35_V2A::All'

## preshower baseline substraction is done already in data.
process.ecalPreshowerRecHit.ESBaseline = cms.int32(0) 
process.ecalPreshowerRecHit.ESRecoAlgo = cms.untracked.int32(1)


# Path and EndPath definitions

process.raw2digi_step = cms.Path(process.RawToDigi)
process.reconstruction_step = cms.Path(process.reconstructionCosmics)
process.dqmoffline_step = cms.Path(process.DQMOfflineCosmics)
process.endjob_step = cms.Path(process.endOfProcess)
process.out_step = cms.EndPath(process.FEVT)


# Schedule definition
process.schedule = cms.Schedule(process.raw2digi_step,process.reconstruction_step,process.dqmoffline_step,process.endjob_step,process.out_step)
