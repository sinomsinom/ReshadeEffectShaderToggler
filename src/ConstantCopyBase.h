#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include <MinHook.h>
#include "ConstantCopyDefinitions.h"
#include "ConstantHandlerBase.h"

namespace ConstantFeedback {
    class ConstantCopyBase {
    public:
        ConstantCopyBase();
        ~ConstantCopyBase();

        virtual bool Init() = 0;
        virtual bool UnInit() = 0;

        static void SetConstantHandler(ConstantHandlerBase* constantHandler);
    protected:
        static ConstantHandlerBase* _constHandler;
        static string GetExecutableName();
    };
}
