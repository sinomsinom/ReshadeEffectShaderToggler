#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>

#include "ResourceShim.h"
#include "GameHookT.h"

using namespace sigmatch_literals;

static const sigmatch::signature ffxiv_texture_create = "40 55 53 57 41 54 41 57 48 8D AC 24 ?? ?? ?? ?? B8 E0 21 00 00"_sig;
static const sigmatch::signature ffxiv_textures_create = "40 53 48 83 EC 60 48 83 79 ?? 00 48 8B D9 0F 84 ?? ?? ?? ??"_sig;

namespace Shim
{
    namespace Resources
    {
        constexpr uintptr_t RT_UI = 0x59e0;
        constexpr uintptr_t RT_NORMALS = 0x4ae0;
        constexpr uintptr_t RT_NORMALS_DECAL = 0x4be0;

        class ResourceShimFFXIV final : public virtual ResourceShim {
        public:
            virtual bool Init() override final;
            virtual bool UnInit() override final;

            virtual bool OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state) override final;
            virtual void OnDestroyResource(reshade::api::device* device, reshade::api::resource res) override final;
            virtual void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initData, reshade::api::resource_usage usage, reshade::api::resource handle) override final;
            virtual bool OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource, reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc) override final;

        private:
            static uintptr_t* p1;
            static uintptr_t* p2;
            static uintptr_t p1_1;

            static sig_ffxiv_texture_create* org_ffxiv_texture_create;
            static sig_ffxiv_textures_create* org_ffxiv_textures_create;

            static void __fastcall detour_ffxiv_texture_create(uintptr_t*, uintptr_t*);
            static void __fastcall detour_ffxiv_textures_create(uintptr_t);
        };
    }
}
