//
//  fl_argmin~.cpp
//
//  Created by Owen Green on 22/01/2018.
//

#include "FrameLib_Vector_Objects.h"
#include "FrameLib_MaxClass.h"

extern "C" int C74_EXPORT main(void)
{
    FrameLib_MaxClass_Expand<FrameLib_VectorArgMin>::makeClass(CLASS_BOX, "fl.argmin~");
}