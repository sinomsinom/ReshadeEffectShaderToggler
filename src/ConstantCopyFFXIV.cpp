#include <cstring>
#include <MinHook.h>
#include "ConstantCopyFFXIV.h"

using namespace ConstantFeedback;

sig_ffxiv_cbload* ConstantCopyFFXIV::org_ffxiv_cbload = nullptr;

ConstantCopyFFXIV::ConstantCopyFFXIV()
{
}

ConstantCopyFFXIV::~ConstantCopyFFXIV()
{
}

bool ConstantCopyFFXIV::Init()
{
    return Hook(&org_ffxiv_cbload, detour_ffxiv_cbload, ffxiv_cbload);
}

bool ConstantCopyFFXIV::UnInit()
{
    return MH_Uninitialize() == MH_OK;
}


void __fastcall ConstantCopyFFXIV::detour_ffxiv_cbload(int64_t p1, uint16_t* p2, uint64_t p3, intptr_t** p4)
{
    ConstantHandlerFFXIV::Origin = *p4;
    ConstantHandlerFFXIV::Size = (reinterpret_cast<uint64_t>(p4[1]) & 0xffffffff) << 4;

    org_ffxiv_cbload(p1, p2, p3, p4);

    ConstantHandlerFFXIV::Origin = nullptr;
    ConstantHandlerFFXIV::Size = 0;
}