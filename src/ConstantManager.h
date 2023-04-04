#pragma once

#include "AddonUIData.h"
#include "ConstantHandlerBase.h"
#include "ConstantCopyBase.h"

namespace ConstantFeedback
{
    enum ConstantCopyType
    {
        Copy_None,
        Copy_MemcpySingular,
        Copy_MemcpyNested,
        Copy_FFXIV, //actually only works with ConstantHandlerType::FFXIV, but shhhhh
        Copy_NierReplicant
    };

    enum ConstantHandlerType
    {
        Handler_Default,
    };

    class ConstantManager
    {
    public:
        static bool Init(AddonImGui::AddonUIData& data, ConstantFeedback::ConstantCopyBase**, ConstantFeedback::ConstantHandlerBase**);
        static bool UnInit();

    private:
        static ConstantCopyType ResolveConstantCopyType(const string&);
        static ConstantHandlerType ResolveConstantHandlerType(const string&);
    };
}
