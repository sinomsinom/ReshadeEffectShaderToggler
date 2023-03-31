#include <format>
#include "ResourceManager.h"

using namespace Rendering;
using namespace reshade::api;

bool ResourceManager::_IsSRGB(reshade::api::format value)
{
    switch (value)
    {
    case format::r8g8b8a8_unorm_srgb:
    case format::r8g8b8x8_unorm_srgb:
    case format::b8g8r8a8_unorm_srgb:
    case format::b8g8r8x8_unorm_srgb:
        return true;
    default:
        return false;
    }

    return false;
}


bool ResourceManager::_HasSRGB(reshade::api::format value)
{
    switch (value)
    {
    case format::r8g8b8a8_typeless:
    case format::r8g8b8a8_unorm:
    case format::r8g8b8a8_unorm_srgb:
    case format::r8g8b8x8_unorm:
    case format::r8g8b8x8_unorm_srgb:
    case format::b8g8r8a8_typeless:
    case format::b8g8r8a8_unorm:
    case format::b8g8r8a8_unorm_srgb:
    case format::b8g8r8x8_typeless:
    case format::b8g8r8x8_unorm:
    case format::b8g8r8x8_unorm_srgb:
        return true;
    default:
        return false;
    }

    return false;
}

void ResourceManager::InitBackbuffer(swapchain* runtime)
{
    // Create backbuffer resource views
    device* dev = runtime->get_device();
    uint32_t count = runtime->get_back_buffer_count();

    resource_desc desc = dev->get_resource_desc(runtime->get_back_buffer(0));
    const bool needCopyRes = dev->get_api() == reshade::api::device_api::d3d10 ||
        dev->get_api() == reshade::api::device_api::d3d11 ||
        dev->get_api() == reshade::api::device_api::d3d12;

    resource copyRes = { 0 };
    resource_view copyBackBufferView = { 0 };
    resource_view copyBackBufferViewSRGB = { 0 };
    const auto& copyResData = _swapchainToCopyResource.find(runtime);

    // Create format specific copy resource and associated views. Basically same how ReShade handles it minus multisample support
    if (needCopyRes)
    {
        reshade::api::format resFormat = format_to_typeless(desc.texture.format);
        runtime->get_device()->create_resource(
            resource_desc(desc.texture.width, desc.texture.height, 1, 1, resFormat, 1,
                memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::copy_source | resource_usage::shader_resource | resource_usage::render_target),
            nullptr, resource_usage::copy_dest, &copyRes);


        reshade::api::format viewFormat = format_to_default_typed(resFormat);
        reshade::api::format viewFormatSRGB = format_to_default_typed(resFormat, 1);

        dev->create_resource_view(copyRes, resource_usage::render_target,
            resource_view_desc(viewFormat), &copyBackBufferView);
        dev->create_resource_view(copyRes, resource_usage::render_target,
            resource_view_desc(viewFormatSRGB), &copyBackBufferViewSRGB);

        _swapchainToCopyResource.emplace(runtime, make_tuple(copyRes, copyBackBufferView, copyBackBufferViewSRGB));
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        resource backBuffer = runtime->get_back_buffer(i);

        if (needCopyRes)
        {
            s_backBufferView[backBuffer.handle] = make_tuple(backBuffer, copyRes, copyBackBufferView, copyBackBufferViewSRGB);
        }
        else
        {
            resource_view backBufferView = { 0 };
            resource_view backBufferViewSRGB = { 0 };

            reshade::api::format viewFormat = format_to_default_typed(desc.texture.format);
            reshade::api::format viewFormatSRGB = format_to_default_typed(desc.texture.format, 1);

            dev->create_resource_view(backBuffer, resource_usage::render_target,
                resource_view_desc(viewFormat), &backBufferView);
            dev->create_resource_view(backBuffer, resource_usage::render_target,
                resource_view_desc(viewFormatSRGB), &backBufferViewSRGB);

            s_backBufferView[backBuffer.handle] = make_tuple(backBuffer, resource{ 0 }, backBufferView, backBufferViewSRGB);
        }

        _swapChainToResourceHandles[runtime].push_back(backBuffer.handle);

    }
}

void ResourceManager::ClearBackbuffer(reshade::api::swapchain* runtime)
{
    device* dev = runtime->get_device();

    const bool needCopyRes = dev->get_api() == reshade::api::device_api::d3d10 ||
        dev->get_api() == reshade::api::device_api::d3d11 ||
        dev->get_api() == reshade::api::device_api::d3d12;

    if (needCopyRes)
    {
        const auto& swapBufferResources = _swapchainToCopyResource.find(runtime);
        if (swapBufferResources != _swapchainToCopyResource.end())
        {
            if (get<2>(swapBufferResources->second) != 0)
                runtime->get_device()->destroy_resource_view(get<2>(swapBufferResources->second));
            if (get<1>(swapBufferResources->second) != 0)
                runtime->get_device()->destroy_resource_view(get<1>(swapBufferResources->second));
            if (get<0>(swapBufferResources->second) != 0)
                runtime->get_device()->destroy_resource(get<0>(swapBufferResources->second));
        }

        _swapchainToCopyResource.erase(runtime);

    }

    const auto& swapBufferRTVhandles = _swapChainToResourceHandles.find(runtime);

    if (swapBufferRTVhandles == _swapChainToResourceHandles.end())
        return;

    for (const auto handle : swapBufferRTVhandles->second)
    {
        const auto& swapBufferRTVhandle = s_backBufferView.find(handle);

        if (swapBufferRTVhandle == s_backBufferView.end())
            continue;

        if (!needCopyRes)
        {
            if (get<2>(swapBufferRTVhandle->second) != 0)
                runtime->get_device()->destroy_resource_view(get<2>(swapBufferRTVhandle->second));
            if (get<3>(swapBufferRTVhandle->second) != 0)
                runtime->get_device()->destroy_resource_view(get<3>(swapBufferRTVhandle->second));
        }

        s_backBufferView.erase(handle);
    }

    _swapChainToResourceHandles.erase(runtime);
}

bool ResourceManager::OnCreateSwapchain(reshade::api::swapchain_desc& desc, void* hwnd)
{
    return false;
}

void ResourceManager::OnInitSwapchain(reshade::api::swapchain* swapchain)
{
    InitBackbuffer(swapchain);
}

void ResourceManager::OnDestroySwapchain(reshade::api::swapchain* swapchain)
{
    ClearBackbuffer(swapchain);
}

bool ResourceManager::OnCreateResource(device* device, resource_desc& desc, subresource_data* initial_data, resource_usage initial_state)
{
    if (static_cast<uint32_t>(desc.usage & resource_usage::render_target) && desc.type == resource_type::texture_2d)
    {
        if (_HasSRGB(desc.texture.format)) {
            std::unique_lock<shared_mutex> lock(resource_mutex);
        
            s_resourceFormatTransient.emplace(&desc, desc.texture.format);
        
            desc.texture.format = format_to_typeless(desc.texture.format);
    
            return true;
        }
    }
    
    return false;
}

void ResourceManager::OnInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    auto& data = device->get_private_data<DeviceDataContainer>();

    std::unique_lock<shared_mutex> lock(resource_mutex);

    if (static_cast<uint32_t>(desc.usage & resource_usage::render_target) && desc.type == resource_type::texture_2d)
    {
        if (s_resourceFormatTransient.contains(&desc))
        {
            reshade::api::format orgFormat = s_resourceFormatTransient.at(&desc);
            s_resourceFormat.emplace(handle.handle, orgFormat);
            s_resourceFormatTransient.erase(&desc);

            resource_view view_non_srgb = { 0 };
            resource_view view_srgb = { 0 };

            reshade::api::format format_non_srgb = format_to_default_typed(desc.texture.format);
            reshade::api::format format_srgb = format_to_default_typed(desc.texture.format, 1);

            device->create_resource_view(handle, resource_usage::render_target,
                resource_view_desc(format_non_srgb), &view_non_srgb);

            device->create_resource_view(handle, resource_usage::render_target,
                resource_view_desc(format_srgb), &view_srgb);

            s_sRGBResourceViews.emplace(handle.handle, make_pair(view_non_srgb, view_srgb));
        }
    }
}


void ResourceManager::OnDestroyResource(device* device, resource res)
{
    std::unique_lock<shared_mutex> lock(resource_mutex);

    s_resourceFormat.erase(res.handle);

    if (s_sRGBResourceViews.contains(res.handle))
    {
        auto& views = s_sRGBResourceViews.at(res.handle);

        if (views.first != 0)
            device->destroy_resource_view(views.first);
        if (views.second != 0)
            device->destroy_resource_view(views.second);
    }

    s_sRGBResourceViews.erase(res.handle);
}


bool ResourceManager::OnCreateResourceView(device* device, resource resource, resource_usage usage_type, resource_view_desc& desc)
{
    const resource_desc texture_desc = device->get_resource_desc(resource);
    if (!static_cast<uint32_t>(texture_desc.usage & resource_usage::render_target) || texture_desc.type != resource_type::texture_2d)
        return false;

    std::shared_lock<shared_mutex> lock(resource_mutex);
    if (s_resourceFormat.contains(resource.handle))
    {
        // Set original resource format in case the game uses that as a basis for creating it's views
        if (desc.format == format_to_typeless(desc.format) && format_to_typeless(desc.format) != format_to_default_typed(desc.format) ||
            desc.format == reshade::api::format::unknown) {

            reshade::api::format rFormat = s_resourceFormat.at(resource.handle);

            // The game may try to re-use the format setting of a previous resource that we had already set to typeless. Try default format in that case.
            desc.format = format_to_typeless(rFormat) == rFormat ? format_to_default_typed(rFormat) : rFormat;

            if (desc.type == resource_view_type::unknown)
            {
                desc.type = texture_desc.texture.depth_or_layers > 1 ? resource_view_type::texture_2d_array : resource_view_type::texture_2d;
                desc.texture.first_level = 0;
                desc.texture.level_count = (usage_type == resource_usage::shader_resource) ? UINT32_MAX : 1;
                desc.texture.first_layer = 0;
                desc.texture.layer_count = (usage_type == resource_usage::shader_resource) ? UINT32_MAX : 1;
            }

            return true;
        }
    }

    return false;
}

bool ResourceManager::IsBackbufferHandle(uint64_t handler)
{
    return s_backBufferView.contains(handler);
}

void ResourceManager::SetResourceViewHandles(uint64_t handle, reshade::api::resource_view* non_srgb_view, reshade::api::resource_view* srgb_view)
{
    const auto& it = s_sRGBResourceViews.find(handle);
    if (it != s_sRGBResourceViews.end())
    {
        *non_srgb_view = it->second.first;
        *srgb_view = it->second.second;
        return;
    }
}

const std::tuple<reshade::api::resource, reshade::api::resource, reshade::api::resource_view, reshade::api::resource_view>* ResourceManager::GetBackbufferViewData(uint64_t handle)
{
    const auto& it = s_backBufferView.find(handle);
    if (it != s_backBufferView.end())
    {
        return &(it->second);
    }

    return nullptr;
}
