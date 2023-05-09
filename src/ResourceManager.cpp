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

    for (uint32_t i = 0; i < count; ++i)
    {
        resource backBuffer = runtime->get_back_buffer(i);

        resource_view backBufferView = { 0 };
        resource_view backBufferViewSRGB = { 0 };

        reshade::api::format viewFormat = format_to_default_typed(desc.texture.format, 0);
        reshade::api::format viewFormatSRGB = format_to_default_typed(desc.texture.format, 1);

        dev->create_resource_view(backBuffer, resource_usage::render_target,
            resource_view_desc(viewFormat), &backBufferView);
        dev->create_resource_view(backBuffer, resource_usage::render_target,
            resource_view_desc(viewFormatSRGB), &backBufferViewSRGB);

        s_sRGBResourceViews.emplace(backBuffer.handle, make_pair(backBufferView, backBufferViewSRGB));
    }
}

void ResourceManager::ClearBackbuffer(reshade::api::swapchain* runtime)
{
    device* dev = runtime->get_device();

    uint32_t count = runtime->get_back_buffer_count();

    for (uint32_t i = 0; i < count; ++i)
    {
        resource backBuffer = runtime->get_back_buffer(i);

        const auto& entry = s_sRGBResourceViews.find(backBuffer.handle);

        // Back buffer resource got probably resized, clear old views and reinitialize
        if (entry != s_sRGBResourceViews.end())
        {
            resource_view oldbackBufferView = entry->second.first;
            resource_view oldbackBufferViewSRGB = entry->second.second;

            if (oldbackBufferView != 0)
            {
                runtime->get_device()->destroy_resource_view(oldbackBufferView);
            }

            if (oldbackBufferViewSRGB != 0)
            {
                runtime->get_device()->destroy_resource_view(oldbackBufferViewSRGB);
            }
        }

        s_sRGBResourceViews.erase(backBuffer.handle);
    }
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
        }

        resource_view view_non_srgb = { 0 };
        resource_view view_srgb = { 0 };

        reshade::api::format format_non_srgb = format_to_default_typed(desc.texture.format, 0);
        reshade::api::format format_srgb = format_to_default_typed(desc.texture.format, 1);

        device->create_resource_view(handle, resource_usage::render_target,
            resource_view_desc(format_non_srgb), &view_non_srgb);

        device->create_resource_view(handle, resource_usage::render_target,
            resource_view_desc(format_srgb), &view_srgb);

        s_sRGBResourceViews.emplace(handle.handle, make_pair(view_non_srgb, view_srgb));
    }
}


void ResourceManager::OnDestroyResource(device* device, resource res)
{
    if (!resource_mutex.try_lock())
        return;

    s_resourceFormat.erase(res.handle);

    const auto& it = s_sRGBResourceViews.find(res.handle);

    if (it != s_sRGBResourceViews.end())
    {
        auto& views = it->second;

        if (views.first != 0)
            device->destroy_resource_view(views.first);
        if (views.second != 0)
            device->destroy_resource_view(views.second);

        s_sRGBResourceViews.erase(it);
    }

    resource_mutex.unlock();
}

void ResourceManager::OnDestroyDevice(device* device)
{
    std::unique_lock<shared_mutex> lock(resource_mutex);

    for (auto it = s_sRGBResourceViews.begin(); it != s_sRGBResourceViews.end();)
    {
        auto& views = it->second;

        if (views.first != 0)
            device->destroy_resource_view(views.first);
        if (views.second != 0)
            device->destroy_resource_view(views.second);

        it = s_sRGBResourceViews.erase(it);
    }
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