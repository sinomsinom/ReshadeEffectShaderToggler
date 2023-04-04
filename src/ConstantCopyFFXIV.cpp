#include <cstring>
#include <MinHook.h>
#include <intrin.h>
#include <d3d11.h>
#include "ConstantCopyFFXIV.h"

using namespace ConstantFeedback;
using namespace reshade::api;

sig_ffxiv_cbload* ConstantCopyFFXIV::org_ffxiv_cbload = nullptr;
vector<tuple<const void*, uint64_t, uint64_t>> ConstantCopyFFXIV::_hostResourceBuffer;
static D3D11_MAPPED_SUBRESOURCE* rBuffer = new D3D11_MAPPED_SUBRESOURCE();

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

void ConstantCopyFFXIV::GetHostConstantBuffer(vector<uint8_t>& dest, size_t size, uint64_t resourceHandle)
{
    for (uint32_t i = 0; i < _hostResourceBuffer.size(); i++)
    {
        auto& item = _hostResourceBuffer[i];
        if (get<1>(item) == resourceHandle)
        {
            size_t minSize = min(size, get<2>(item));
            memcpy(dest.data(), get<0>(item), minSize);
        }
    }
}

static inline int64_t exit_cbload(uint16_t* param_2, SomeData& param_3, int64_t* plVar10, uint64_t uVar9)
{
    *(char*)((param_3.something2) + 8 + (int64_t)param_2) = (char)uVar9;
    *(int64_t**)(param_2 + param_3.something2 * 4 + 0xc) = plVar10;
    *param_2 = *param_2 | (uint16_t)(1 << param_3.something2);
    return param_3.something2;
}

inline void ConstantCopyFFXIV::set_host_resource_data_location(void* origin, size_t len, int64_t resource_handle, uint64_t base, uint64_t offset)
{
    uint64_t bOffset = (offset - base) / 8;
    if (_hostResourceBuffer.size() < bOffset + 1)
        _hostResourceBuffer.resize(bOffset + 1);

    auto& entry = _hostResourceBuffer[bOffset];
    if (get<1>(entry) != resource_handle)
        get<1>(entry) = resource_handle;

    if (get<2>(entry) != len)
        get<2>(entry) = len;

    get<0>(entry) = origin;
}

int64_t __fastcall ConstantCopyFFXIV::detour_ffxiv_cbload(int64_t param_1, uint16_t* param_2, SomeData param_3, HostBufferData* param_4)
{
    int64_t* plVar1;
    int64_t* plVar2;
    uint32_t uVar4;
    int32_t iVar5;
    int64_t** pplVar6;
    uint32_t uVar7;
    int64_t lVar8;
    uint64_t uVar9;
    int64_t* plVar10;
    uint32_t uVar11;
    uint64_t uVar12;

    ZeroMemory(rBuffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
    const size_t valueSize = 16;
    const uint64_t copySize = static_cast<uint64_t>(param_4->numValues) * valueSize;

    //uVar11 = (uint32_t)param_3.something1;
    uVar11 = param_3.something1;
    plVar10 = (int64_t*)param_4->data;
    uVar4 = param_4->numValues;
    if (param_4->numValues < uVar11) {
        uVar4 = uVar11;
    }
    uVar4 = uVar4 - 1;
    iVar5 = 0x1f;

    if (!_BitScanReverse((DWORD*)&iVar5, uVar4))
    {
        iVar5 = -1;
    }

    //uVar12 = (uint64_t)((uint32_t)param_3.something0 & 3);
    uVar12 = param_3.something0 & 3;
    if (*plVar10 == *(int64_t*)(param_1 + 0x10)) {
        *(uint8_t*)((param_3.something2) + 8 + (int64_t)param_2) = 4;
    }
    else {
        plVar1 = *(int64_t**)(param_1 + 8);
        plVar2 = (int64_t*)param_4->data;
        if (iVar5 + 1U < 6) {
            uVar9 = 0;
            lVar8 = (uVar12 + (uint64_t)(iVar5 + 1U) * 4) * 0x50 + param_1;
            pplVar6 = (int64_t**)(lVar8 + 0x38);
            do {
                uVar4 = (uint32_t)uVar9;
                if (*pplVar6 == plVar2) {
                    *(int32_t*)(lVar8 + 0x60) = *(int*)(lVar8 + 0x60) + 1;
                    if ((*(uint32_t*)(lVar8 + 0x58) & 7) != uVar4) {
                        *(uint32_t*)(lVar8 + 0x58) = *(uint32_t*)(lVar8 + 0x58) * 8 | uVar4;
                    }
                    plVar10 = *(int64_t**)(lVar8 + 0x18 + uVar9 * 8);
                    return exit_cbload(param_2, param_3, plVar10, uVar9);
                }
                uVar9 = (uint64_t)(uVar4 + 1);
                pplVar6 = pplVar6 + 1;
            } while (uVar4 + 1 < 4);
            *(int32_t*)(lVar8 + 100) = *(int32_t*)(lVar8 + 100) + 1;
            uVar11 = 0;
            uVar4 = 0;
            do {
                uVar7 = 1 << ((uint8_t)(*(uint32_t*)(lVar8 + 0x58) >> ((uint8_t)uVar11 & 0x1f)) & 3) | uVar4;
                if (uVar7 == 0xf) break;
                uVar11 = uVar11 + 3;
                uVar4 = uVar7;
            } while (uVar11 < 0x1e);
            uVar11 = *(int32_t*)(lVar8 + 0x5c) + 1U & 3;
            uVar4 = ~((uVar4 << 4 | uVar4) >> (int8_t)uVar11);

            if (!_BitScanForward((DWORD*)&iVar5, uVar4))
            {
                iVar5 = 0;
            }

            uVar4 = iVar5 + uVar11 & 3;
            uVar9 = (uint64_t)uVar4;
            *(int64_t**)(lVar8 + 0x38 + uVar9 * 8) = plVar2;
            *(uint32_t*)(lVar8 + 0x5c) = uVar11;
            if ((*(uint32_t*)(lVar8 + 0x58) & 7) != uVar4) {
                *(uint32_t*)(lVar8 + 0x58) = *(uint32_t*)(lVar8 + 0x58) * 8 | uVar4;
            }
            plVar10 = *(int64_t**)(lVar8 + 0x18 + uVar9 * 8);

            set_host_resource_data_location(plVar2, copySize, (int64_t)plVar10, param_1, lVar8 + 0x18 + uVar9 * 8);

            ((ID3D11DeviceContext*)(plVar1))->Map((ID3D11Resource*)plVar10, 0, (D3D11_MAP)4, 0, rBuffer);
            memcpy(rBuffer->pData, plVar2, copySize);
            ((ID3D11DeviceContext*)(plVar1))->Unmap((ID3D11Resource*)plVar10, 0);

            *(uint8_t*)(param_3.something2 + 8 + (int64_t)param_2) = (uint8_t)uVar9;
        }
        else {
            lVar8 = uVar12 * 0x10 + param_1;
            plVar10 = *(int64_t**)(lVar8 + 0x798);
            if (*(int64_t**)(lVar8 + 0x7a0) != plVar2) {
                set_host_resource_data_location(plVar2, copySize, (int64_t)plVar10, param_1, lVar8 + 0x798);

                ((ID3D11DeviceContext*)(plVar1))->Map((ID3D11Resource*)plVar10, 0, (D3D11_MAP)4, 0, rBuffer);
                memcpy(rBuffer->pData, plVar2, copySize);
                ((ID3D11DeviceContext*)(plVar1))->Unmap((ID3D11Resource*)plVar10, 0);

                *(int64_t**)(lVar8 + 0x7a0) = plVar2;
            }
            *(uint8_t*)(param_3.something2 + 8 + (int64_t)param_2) = 4;
        }
    }
    *(int64_t**)(param_2 + param_3.something2 * 4 + 0xc) = plVar10;
    *param_2 = *param_2 | (uint16_t)(1 << param_3.something2);
    return param_3.something2;
}