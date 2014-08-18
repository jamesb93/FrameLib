
#ifndef FRAMELIB_BLOCK_H
#define FRAMELIB_BLOCK_H

#include "FrameLib_Attributes.h"

// FrameLib_Block

// This abstract class provides connectivity to FrameLib_DSP objects or blocks (groups of FrameLib_DSP objects).
// Standard objects inherit this in the FrameLib_DSP class.
// Objects that have asynchronous outputs can use this class to host multiple FrameLib_DSP objects and connect them correctly.

class FrameLib_Block
{
    
public:
    
    // Constructor
    
    FrameLib_Block(unsigned long numAudioIns, unsigned long numAudioOuts)
    {
        mNumAudioIns = numAudioIns;
        mNumAudioOuts = numAudioOuts;
    }
    
    // Destructor (virtual)
    
    virtual ~FrameLib_Block() = 0;
    
    // Block copying
    
     virtual FrameLib_Block *copy() = 0;
    
    // IO Utilities
    
    virtual unsigned long getNumIns() = 0;
    virtual unsigned long getNumOuts() = 0;
    
    unsigned long getNumAudioIns()
    {
        return mNumAudioIns;
    }
    
    unsigned long getNumAudioOuts()
    {
        return mNumAudioOuts;
    }
    
    // Audio Processing
    
    virtual void blockUpdate (double **ins, double **outs, unsigned long vecSize)
    {
        // Override to handle audio at the block level (objects with block-based audio must overload this)
    }
    
    virtual bool handlesAudio()
    {
        // This function allows you to tell the outside world (whatever that is) whether or not it should call the audio brocessing block
        
        return FALSE;
    }
    
    virtual void reset() = 0;
    
    // Connections
    
    virtual void deleteConnection(unsigned long inIdx) = 0;
    virtual void addConnection(class FrameLib_DSP *object, unsigned long outIdx, unsigned long inIdx) = 0;
    virtual void clearConnections() = 0;
    virtual bool isConnected(unsigned long inIdx) = 0;
    
    void addConnection(FrameLib_Block *object, unsigned long outIdx, unsigned long inIdx)
    {
        addConnection(object->getOutputObject(outIdx), outIdx, inIdx);
    }
    
protected:
        
    virtual class FrameLib_DSP *getOutputObject(unsigned long outIdx) = 0;

    FrameLib_Attributes mAttributes;
    
private:
    
    unsigned long mNumAudioIns;
    unsigned long mNumAudioOuts;
};

// Virtual Destructor

FrameLib_Block::~FrameLib_Block(){}

#endif
