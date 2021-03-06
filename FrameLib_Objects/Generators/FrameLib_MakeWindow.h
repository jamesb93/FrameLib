
#ifndef FRAMELIB_MAKEWINDOW_H
#define FRAMELIB_MAKEWINDOW_H

#include "FrameLib_DSP.h"
#include "FrameLib_WindowGenerator.h"

class FrameLib_MakeWindow final : public FrameLib_Processor
{
    // Parameter Enums and Info
    
    enum ParameterList { kMode, kLength, kUnits, kWindowType, kExponent, kCompensation, kParameters, kEndpoints };
    enum Modes { kRequestedLength, kInLength };
    enum Units { kSamples, kMS, kSeconds };

    struct ParameterInfo : public FrameLib_Parameters::Info { ParameterInfo(); };

    using Generator = FrameLib_WindowGenerator<kWindowType, kParameters, kExponent, kCompensation, kEndpoints>;
    
public:
    
    // Constructor
    
    FrameLib_MakeWindow(FrameLib_Context context, const FrameLib_Parameters::Serial *serialisedParameters, FrameLib_Proxy *proxy);
    
    // Info
    
    std::string objectInfo(bool verbose) override;
    std::string inputInfo(unsigned long idx, bool verbose) override;
    std::string outputInfo(unsigned long idx, bool verbose) override;

private:
    
    // Helpers
    
    unsigned long getLength();

    // Update
    
    void update() override { mGenerator.updateParameters(); }

    // Process
    
    void process() override;
    
    // Data
    
    Generator mGenerator;

    static ParameterInfo sParamInfo;
};

#endif
