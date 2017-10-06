
#include "FrameLib_Route.h"

// Internal Valve Class

// Constructor

FrameLib_Route::Valve::Valve(FrameLib_Context context, FrameLib_Parameters::Serial *serialisedParameters, void *owner, long num) : FrameLib_Processor(context, owner, NULL, 1, 1)
{
    mParameters.addInt(kActiveValve, "output", 0);
    mParameters.set(serialisedParameters);
    
    mValveNumber = num;
    mActiveValve = floor(mParameters.getInt(kActiveValve));
    
    setInputMode(0, false, mValveNumber == mActiveValve, true, kFrameAny);
    setOutputMode(0, kFrameAny);
    
    addParameterInput();
}

// Update and Process

void FrameLib_Route::Valve::update()
{
    mActiveValve = floor(mParameters.getValue(kActiveValve));
    updateTrigger(0, mValveNumber == mActiveValve);
}

void FrameLib_Route::Valve::process()
{
    prepareCopyInputToOutput(0, 0);
    allocateOutputs();
    copyInputToOutput(0, 0);
}

// Main Class

// Constructor

FrameLib_Route::FrameLib_Route(FrameLib_Context context, FrameLib_Parameters::Serial *serialisedParameters, void *owner)
: FrameLib_Block(kProcessor, context, owner), mParameters(&sParamInfo)
{
    mParameters.addDouble(kNumOuts, "num_outs", 2, 0);
    mParameters.setClip(2, 32);
    mParameters.setInstantiation();
    mParameters.addInt(kActiveOut, "output", 0, 1);
    
    mParameters.set(serialisedParameters);
    
    mNumOuts = mParameters.getInt(kNumOuts);
    
    for (int i = 0; i < mNumOuts; i++)
        mValves.push_back(new Valve(context, serialisedParameters, owner, i));
    
    setIO(2, mNumOuts);
}

FrameLib_Route::~FrameLib_Route()
{
    for (std::vector<Valve *>::iterator it = mValves.begin(); it != mValves.end(); it++)
        delete (*it);
}
    
// Info

std::string FrameLib_Route::objectInfo(bool verbose)
{
    return formatInfo("Routes input frames to one of a number of outputs: The number of outputs is variable. The selected output can be changed with a parameter.",
                      "Routes input frames to one of a number of outputs.", verbose);
}

std::string FrameLib_Route::inputInfo(unsigned long idx, bool verbose)
{
    if (idx == mNumOuts)
        return formatInfo("Parameter Update - tagged input updates parameters", "Parameter Update", verbose);
    else
        return "Input Frames";
}

std::string FrameLib_Route::outputInfo(unsigned long idx, bool verbose)
{
    return formatInfo("Output #", "Output #", idx, verbose);
}

// Parameter Info

FrameLib_Route::ParameterInfo FrameLib_Route::sParamInfo;

FrameLib_Route::ParameterInfo::ParameterInfo()
{
    add("Sets the number of object outputs.");
    add("Sets the current output (or off if out of range).");
}

// Reset

void FrameLib_Route::reset(double samplingRate, unsigned long maxBlockSize)
{
    for (std::vector<Valve *>::iterator it = mValves.begin(); it != mValves.end(); it++)
        (*it)->reset(samplingRate, maxBlockSize);
}
