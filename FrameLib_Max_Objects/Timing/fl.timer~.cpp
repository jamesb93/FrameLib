
#include "FrameLib_Timer.h"
#include "FrameLib_MaxClass.h"

extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxClass_Expand<FrameLib_Timer>::makeClass(CLASS_BOX, "fl.timer~");
}
