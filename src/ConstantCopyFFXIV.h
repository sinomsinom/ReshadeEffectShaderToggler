#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include "ConstantCopyDefinitions.h"
#include "ConstantHandlerFFXIV.h"
#include "ConstantCopyT.h"

using namespace sigmatch_literals;

static const sigmatch::signature ffxiv_cbload = "4C 89 44 24 ?? 56 57 41 57"_sig;

namespace ConstantFeedback {
    class ConstantCopyFFXIV final : public virtual ConstantCopyT<sig_ffxiv_cbload>{
    public:
        ConstantCopyFFXIV();
        ~ConstantCopyFFXIV();

        bool Init() override final;
        bool UnInit() override final;

    private:
        static sig_ffxiv_cbload* org_ffxiv_cbload;
        static void __fastcall detour_ffxiv_cbload(int64_t p1, uint16_t* p2, uint64_t p3, intptr_t** p4);
    };
}