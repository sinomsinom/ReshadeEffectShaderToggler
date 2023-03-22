#include <cstring>
#include <MinHook.h>
#include <reshade.hpp>
#include "ConstantCopyNierReplicant.h"
#include "PipelinePrivateData.h"

using namespace ConstantFeedback;

sig_nier_replicant_cbload* ConstantCopyNierReplicant::org_nier_replicant_cbload = nullptr;

ConstantCopyNierReplicant::ConstantCopyNierReplicant()
{
}

ConstantCopyNierReplicant::~ConstantCopyNierReplicant()
{
}

bool ConstantCopyNierReplicant::Init()
{
    return Hook(&org_nier_replicant_cbload, detour_nier_replicant_cbload);
}

bool ConstantCopyNierReplicant::UnInit()
{
    return MH_Uninitialize() == MH_OK;
}

bool ConstantCopyNierReplicant::Hook(sig_nier_replicant_cbload** original, sig_nier_replicant_cbload* detour)
{
    string exe = GetExecutableName();
    if (exe.length() == 0)
        return false;

    sigmatch::this_process_target target;
    sigmatch::search_result result = target.in_module(exe).search(nier_replicant_cbload);

    for (const std::byte* address : result.matches()) {
        void* adr = static_cast<void*>(const_cast<std::byte*>(address));
        *original = InstallHook<sig_nier_replicant_cbload>(adr, detour);
    }

    if (original != nullptr)
        return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;

    return false;
}

bool ConstantCopyNierReplicant::Unhook()
{
    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK)
    {
        return false;
    }

    return true;
}

void __fastcall ConstantCopyNierReplicant::detour_nier_replicant_cbload(intptr_t p1, intptr_t* p2, uintptr_t p3)
{
    ConstantHandlerFFXIV::Origin = reinterpret_cast<uint8_t*>((p3 & 0xffffffff) * *reinterpret_cast<uintptr_t*>(p1 + 0xd0) + *reinterpret_cast<intptr_t*>(p1 + 0x88));
    ConstantHandlerFFXIV::Size = *reinterpret_cast<uintptr_t*>(p1 + 0xd0);

    org_nier_replicant_cbload(p1, p2, p3);

    ConstantHandlerFFXIV::Origin = nullptr;
    ConstantHandlerFFXIV::Size = 0;
}