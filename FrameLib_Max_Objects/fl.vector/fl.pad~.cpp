
#include "FrameLib_Pad.h"
#include "Framelib_Max.h"

extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxObj<FrameLib_Expand<FrameLib_Pad> >::makeClass(CLASS_BOX, "fl.pad~");
}
