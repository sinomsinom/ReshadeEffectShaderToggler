#pragma once

#include <reshade.hpp>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include "PipelinePrivateData.h"

namespace Rendering
{
    class __declspec(novtable) ResourceManager final
    {
    public:
        void InitBackbuffer(reshade::api::effect_runtime* runtime);
        void ClearBackbuffer(reshade::api::effect_runtime* runtime);

        bool OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state);
        void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initData, reshade::api::resource_usage usage, reshade::api::resource handle);
        void OnDestroyResource(reshade::api::device* device, reshade::api::resource res);
        bool OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource, reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc);

        void SetResourceViewHandles(uint64_t handle, reshade::api::resource_view* non_srgb_view, reshade::api::resource_view* srgb_view);
        void SetBackbufferViewHandles(uint64_t handle, reshade::api::resource_view* non_srgb_view, reshade::api::resource_view* srgb_view);
    private:
        bool _IsSRGB(reshade::api::format value);
        bool _HasSRGB(reshade::api::format value);

        std::unordered_map<uint64_t, std::pair<reshade::api::resource_view, reshade::api::resource_view>> s_backBufferView;
        std::unordered_map<uint64_t, std::pair<reshade::api::resource_view, reshade::api::resource_view>> s_sRGBResourceViews;
        std::unordered_map<const reshade::api::resource_desc*, reshade::api::format> s_resourceFormatTransient;
        std::unordered_map<uint64_t, reshade::api::format> s_resourceFormat;

        std::shared_mutex resource_mutex;
    };
}