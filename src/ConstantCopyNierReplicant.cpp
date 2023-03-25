#include <cstring>
#include <MinHook.h>
#include "ConstantCopyNierReplicant.h"

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
    return Hook(&org_nier_replicant_cbload, detour_nier_replicant_cbload, nier_replicant_cbload);
}

bool ConstantCopyNierReplicant::UnInit()
{
    return MH_Uninitialize() == MH_OK;
}

void __fastcall ConstantCopyNierReplicant::detour_nier_replicant_cbload(intptr_t p1, intptr_t* p2, uintptr_t p3)
{
    ConstantHandlerFFXIV::Origin = reinterpret_cast<uint8_t*>((p3 & 0xffffffff) * *reinterpret_cast<uintptr_t*>(p1 + 0xd0) + *reinterpret_cast<intptr_t*>(p1 + 0x88));
    ConstantHandlerFFXIV::Size = *reinterpret_cast<uintptr_t*>(p1 + 0xd0);

    org_nier_replicant_cbload(p1, p2, p3);

    ConstantHandlerFFXIV::Origin = nullptr;
    ConstantHandlerFFXIV::Size = 0;
}