#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include "ConstantCopyDefinitions.h"
#include "ConstantCopyT.h"

using namespace sigmatch_literals;

static const sigmatch::signature ffxiv_cbload = "4C 89 44 24 ?? 56 57 41 57"_sig;

struct HostBufferData {
    void* data;
    uint32_t numValues;
    uint32_t dunno;
};

struct SomeData {
    uint16_t something2;
    uint16_t something1;
    uint16_t something0;
    uint16_t dunno;
};

namespace ConstantFeedback {
    class ConstantCopyFFXIV final : public virtual ConstantCopyT<sig_ffxiv_cbload> {
    public:
        ConstantCopyFFXIV();
        ~ConstantCopyFFXIV();

        bool Init() override final;
        bool UnInit() override final;

        void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initData, reshade::api::resource_usage usage, reshade::api::resource handle) override final {};
        void OnDestroyResource(reshade::api::device* device, reshade::api::resource res) override final {};
        void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data) override final {};
        void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource) override final {};
        void GetHostConstantBuffer(std::vector<uint8_t>& dest, size_t size, uint64_t resourceHandle) override final;
    private:
        static vector<tuple<const void*, uint64_t, uint64_t>> _hostResourceBuffer;
        static sig_ffxiv_cbload* org_ffxiv_cbload;
        static int64_t __fastcall detour_ffxiv_cbload(int64_t param_1, uint16_t* param_2, SomeData param_3, HostBufferData* param_4);
        static inline void set_host_resource_data_location(void* origin, size_t len, int64_t resource_handle, uint64_t base, uint64_t offset);
    };
}