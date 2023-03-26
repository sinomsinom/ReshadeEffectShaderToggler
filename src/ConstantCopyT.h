#pragma once
#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include <MinHook.h>

#pragma warning(push)
#pragma warning(disable : 4005)
#include <sigmatch.hpp>
#pragma warning(pop)

#include "ConstantCopyDefinitions.h"
#include "ToggleGroup.h"
#include "ConstantCopyBase.h"

namespace ConstantFeedback {
    template<typename T>
    class ConstantCopyT : public virtual ConstantCopyBase {
    public:
        ConstantCopyT();
        ~ConstantCopyT();

        virtual bool Init() = 0;
        virtual bool UnInit() = 0;

        virtual bool Hook(T** original, T* detour, const sigmatch::signature& sig);
        virtual bool Unhook();
    protected:
        static T* InstallHook(void* target, T* callback);
        static T* InstallApiHook(LPCWSTR pszModule, LPCSTR pszProcName, T* callback);
    };
}
