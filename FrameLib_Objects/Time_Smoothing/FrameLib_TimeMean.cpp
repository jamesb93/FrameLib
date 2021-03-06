
#include "FrameLib_TimeMean.h"

// Constructor

FrameLib_TimeMean::FrameLib_TimeMean(FrameLib_Context context, const FrameLib_Parameters::Serial *serialisedParameters, FrameLib_Proxy *proxy)
: FrameLib_TimeBuffer<FrameLib_TimeMean>(context, serialisedParameters, proxy)
{}

// Update size

void FrameLib_TimeMean::resetSize(unsigned long maxFrames, unsigned long size)
{
    mSum = allocAutoArray<NeumaierSum>(size);
    
    for (unsigned long i = 0; i < size; i++)
        mSum[i].clear();
}

// Process

void FrameLib_TimeMean::add(const double *newFrame, unsigned long size)
{
    for (unsigned long i = 0; i < size; i++)
        mSum[i].sum(newFrame[i]);
}

void FrameLib_TimeMean::remove(const double *oldFrame, unsigned long size)
{
    for (unsigned long i = 0; i < size; i++)
        mSum[i].sum(-oldFrame[i]);
}

void FrameLib_TimeMean::result(double *output, unsigned long size, Padded pad, unsigned long padSize)
{
    const double recip = 1.0 / getNumFrames();
    
    for (unsigned long i = 0; i < size; i++)
        output[i] = (pad[i] * padSize + mSum[i].value()) * recip;
}
