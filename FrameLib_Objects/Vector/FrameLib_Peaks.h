
#ifndef FRAMELIB_PEAKS_H
#define FRAMELIB_PEAKS_H

#include "FrameLib_DSP.h"

class FrameLib_Peaks : public FrameLib_Processor, private FrameLib_Info
{

public:
    
    // Constructor
    
    FrameLib_Peaks(FrameLib_Context context, FrameLib_Parameters::Serial *serialisedParameters, void *owner);
    
    // Info
    
    const char *objectInfo(bool verbose);
    const char *inputInfo(unsigned long idx, bool verbose);
    const char *outputInfo(unsigned long idx, bool verbose);

private:
    
    // Helpers
    
    double logValue(double val);
    void refinePeak(double& pos, double& amp, double posUncorrected, double vm1, double v0, double vp1);
    
    // Process
    
    void process();
};

#endif
