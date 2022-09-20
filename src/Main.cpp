///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for Reshade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
// 
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID unsigned long long // Change ImGui texture ID type to that of a 'reshade::api::resource_view' handle

#include <imgui.h>
#include <reshade.hpp>
#include <vector>
#include <unordered_map>
#include <set>
#include <functional>
#include <algorithm>
#include "crc32_hash.hpp"
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include "AddonUIData.h"
#include "AddonUIDisplay.h"

using namespace reshade::api;
using namespace ShaderToggler;
using namespace AddonImGui;

extern "C" __declspec(dllexport) const char* NAME = "Reshade Effect Shader Toggler";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Addon which allows you to define groups of shaders to render Reshade effects on.";

struct BindRenderTargetsState {
    command_list* cmd_list;
    uint32_t count;
    const resource_view* rtvs;
    resource_view dsv;
};

struct BindViewportsState {
    command_list* cmd_list;
    uint32_t first;
    uint32_t count;
    vector<viewport> viewports;
};

struct BindScissorRectsState {
    command_list* cmd_list;
    uint32_t first;
    uint32_t count;
    vector<rect> rects;
};

struct BindPipelineState {
    command_list* cmd_list;
    pipeline_stage stages;
    pipeline pipelineHandle;
};

struct BindDescriptorsState {
    pipeline_layout current_layout[2];
    pipeline_layout current_layout_bind[2];
    vector<descriptor_set> current_sets[2];
};

enum DescriptorEmissionType : uint32_t {
    unknown = 0,
    bind_only = 1,
    push_only = 2,
    bind_then_push = 4,
    push_then_bind = 8
};

struct __declspec(uuid("222F7169-3C09-40DB-9BC9-EC53842CE537")) CommandListDataContainer {
    uint64_t activePixelShaderPipeline;
    uint64_t activeVertexShaderPipeline;
    resource_view active_rtv = resource_view{ 0 };
    atomic_bool rendered_effects = false;
    unordered_set<string> techniquesToRender;

    BindRenderTargetsState renderTargetState;
    BindDescriptorsState descriptorsState;
    BindViewportsState viewportsState;
    BindScissorRectsState scissorRectsState;
    BindPipelineState pipelineState;
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
    effect_runtime* current_runtime = nullptr;
    unordered_map<string, bool> allEnabledTechniques;
};

#define CHAR_BUFFER_SIZE 256
#define MAX_EFFECT_HANDLES 128

static ShaderToggler::ShaderManager g_pixelShaderManager;
static ShaderToggler::ShaderManager g_vertexShaderManager;
static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static vector<string> allTechniques;
static AddonUIData g_addonUIData(&g_pixelShaderManager, &g_vertexShaderManager, &g_activeCollectorFrameCounter, &allTechniques);
static std::shared_mutex s_mutex;
static char g_charBuffer[CHAR_BUFFER_SIZE];
static size_t g_charBufferSize = CHAR_BUFFER_SIZE;


/// <summary>
/// Calculates a crc32 hash from the passed in shader bytecode. The hash is used to identity the shader in future runs.
/// </summary>
/// <param name="shaderData"></param>
/// <returns></returns>
static uint32_t calculateShaderHash(void* shaderData)
{
    if (nullptr == shaderData)
    {
        return 0;
    }

    const auto shaderDesc = *static_cast<shader_desc*>(shaderData);
    return compute_crc32(static_cast<const uint8_t*>(shaderDesc.code), shaderDesc.code_size);
}


static void enumerateTechniques(effect_runtime* runtime, std::function<void(effect_runtime*, effect_technique, string&)> func)
{
    runtime->enumerate_techniques(nullptr, [func](effect_runtime* rt, effect_technique technique) {
        g_charBufferSize = CHAR_BUFFER_SIZE;
        rt->get_technique_name(technique, g_charBuffer, &g_charBufferSize);
        string name(g_charBuffer);
        func(rt, technique, name);
        });
}


static void onInitDevice(device* device)
{
    device->create_private_data<DeviceDataContainer>();
}


static void onDestroyDevice(device* device)
{
    device->destroy_private_data<DeviceDataContainer>();
}


static void onInitCommandList(command_list* commandList)
{
    commandList->create_private_data<CommandListDataContainer>();
}


static void onDestroyCommandList(command_list* commandList)
{
    commandList->destroy_private_data<CommandListDataContainer>();
}

static void onResetCommandList(command_list* commandList)
{
    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();

    commandListData.activePixelShaderPipeline = -1;
    commandListData.activeVertexShaderPipeline = -1;
    commandListData.rendered_effects = false;
    commandListData.active_rtv = { 0 };

    commandListData.descriptorsState.current_layout[0] = { 0 };
    commandListData.descriptorsState.current_layout[1] = { 0 };
    commandListData.descriptorsState.current_layout_bind[0] = { 0 };
    commandListData.descriptorsState.current_layout_bind[1] = { 0 };
    commandListData.descriptorsState.current_sets[0].clear();
    commandListData.descriptorsState.current_sets[1].clear();

    commandListData.scissorRectsState = { nullptr, 0, 0, {} };
    commandListData.viewportsState = { nullptr, 0, 0, {} };
    commandListData.pipelineState = { nullptr, pipeline_stage::all, {0} };

    commandListData.techniquesToRender.clear();
}


static void onPresent(command_queue* queue, swapchain* swapchain, const rect*, const rect*, uint32_t, const rect*)
{
    device* device = queue->get_device();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    if (device->get_api() != device_api::d3d12 && device->get_api() != device_api::vulkan)
        onResetCommandList(queue->get_immediate_command_list());

    std::for_each(deviceData.allEnabledTechniques.begin(), deviceData.allEnabledTechniques.end(), [](auto& el) {
        el.second = false;
        });
}


static void onReshadeReloadedEffects(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
    data.allEnabledTechniques.clear();
    allTechniques.clear();

    enumerateTechniques(data.current_runtime, [&data](effect_runtime* runtime, effect_technique technique, string& name) {
        allTechniques.push_back(name);
        bool enabled = runtime->get_technique_state(technique);

        if (enabled)
        {
            data.allEnabledTechniques.emplace(name, false);
        }
        });
}


static bool onReshadeSetTechniqueState(effect_runtime* runtime, effect_technique technique, bool enabled)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
    g_charBufferSize = CHAR_BUFFER_SIZE;
    runtime->get_technique_name(technique, g_charBuffer, &g_charBufferSize);
    string techName(g_charBuffer);

    if (!enabled)
    {
        if (data.allEnabledTechniques.contains(techName))
        {
            data.allEnabledTechniques.erase(techName);
        }
    }
    else
    {
        if (data.allEnabledTechniques.find(techName) == data.allEnabledTechniques.end())
        {
            data.allEnabledTechniques.emplace(techName, false);
        }
    }

    return false;
}


static void onInitEffectRuntime(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
    data.current_runtime = runtime;
}


static void onDestroyEffectRuntime(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
    data.current_runtime = nullptr;
}


static void onInitPipeline(device* device, pipeline_layout, uint32_t subobjectCount, const pipeline_subobject* subobjects, pipeline pipelineHandle)
{
    // shader has been created, we will now create a hash and store it with the handle we got.
    for (uint32_t i = 0; i < subobjectCount; ++i)
    {
        switch (subobjects[i].type)
        {
        case pipeline_subobject_type::vertex_shader:
        {
            g_vertexShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
        }
        break;
        case pipeline_subobject_type::pixel_shader:
        {
            g_pixelShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
        }
        break;
        }
    }
}


static void onDestroyPipeline(device* device, pipeline pipelineHandle)
{
    g_pixelShaderManager.removeHandle(pipelineHandle.handle);
    g_vertexShaderManager.removeHandle(pipelineHandle.handle);
}


/// <summary>
/// This function will return true if the command list specified has one or more shader hashes which are currently marked. Otherwise false.
/// </summary>
/// <param name="commandList"></param>
/// <returns>true if the draw call has to be blocked</returns>
bool checkDrawCallForCommandList(command_list* commandList)
{
    if (nullptr == commandList)
    {
        return false;
    }

    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = commandList->get_device()->get_private_data<DeviceDataContainer>();

    bool all_rendered = true;
    std::for_each(deviceData.allEnabledTechniques.begin(), deviceData.allEnabledTechniques.end(), [&all_rendered](const auto& el) {
        all_rendered &= el.second;
        });

    if (all_rendered) {
        return false;
    }

    uint32_t psShaderHash = g_pixelShaderManager.getShaderHash(commandListData.activePixelShaderPipeline);
    uint32_t vsShaderHash = g_vertexShaderManager.getShaderHash(commandListData.activeVertexShaderPipeline);

    vector<const ToggleGroup*> tGroups;

    if ((g_pixelShaderManager.isBlockedShader(psShaderHash) || g_vertexShaderManager.isBlockedShader(vsShaderHash)) &&
        (g_pixelShaderManager.isInHuntingMode() || g_vertexShaderManager.isInHuntingMode()))
    {
        tGroups.push_back(&g_addonUIData.GetToggleGroups()[g_addonUIData.GetToggleGroupIdShaderEditing()]);
    }

    for (auto& group : g_addonUIData.GetToggleGroups())
    {
        if (group.second.isBlockedPixelShader(psShaderHash) || group.second.isBlockedVertexShader(vsShaderHash))
        {
            tGroups.push_back(&group.second);
        }
    }

    for (auto tGroup : tGroups)
    {
        if (tGroup->preferredTechniques().size() > 0) {
            for (auto& techName : tGroup->preferredTechniques())
            {
                if (deviceData.allEnabledTechniques.contains(techName) && !deviceData.allEnabledTechniques[techName])
                {
                    commandListData.techniquesToRender.insert(techName);
                }
            }
        }
        else
        {
            for (const auto& tech : deviceData.allEnabledTechniques)
            {
                if (!tech.second)
                {
                    commandListData.techniquesToRender.insert(tech.first);
                }
            }
        }
    }

    return commandListData.techniquesToRender.size() > 0;
}


static void RenderEffects(command_list* cmd_list)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr || commandListData.active_rtv == 0) {
        return;
    }

    if (commandListData.techniquesToRender.size() > 0) {
        // dummy render call to prevent reshade from rendering effects on top if we're rendering too
        if (!commandListData.rendered_effects)
        {
            vector<effect_technique> enabledTechniques;

            enumerateTechniques(deviceData.current_runtime, [&deviceData, &enabledTechniques](effect_runtime* runtime, effect_technique technique, string& name) {
                if (deviceData.allEnabledTechniques.contains(name))
                {
                    enabledTechniques.push_back(technique);
                }
                });

            for (auto& tech : enabledTechniques)
            {
                deviceData.current_runtime->set_technique_state(tech, false);
            }

            deviceData.current_runtime->render_effects(cmd_list, commandListData.active_rtv);

            for (auto& tech : enabledTechniques)
            {
                deviceData.current_runtime->set_technique_state(tech, true);
            }

            commandListData.rendered_effects = true;
        }

        enumerateTechniques(deviceData.current_runtime, [cmd_list, &deviceData, &commandListData](effect_runtime* runtime, effect_technique technique, string& name) {
            if (commandListData.techniquesToRender.contains(name) && !deviceData.allEnabledTechniques[name])
            {
                runtime->render_technique(technique, cmd_list, commandListData.active_rtv);
            }
            });

        for (auto& tech : commandListData.techniquesToRender)
        {
            deviceData.allEnabledTechniques[tech] = true;
        }

        commandListData.techniquesToRender.clear();
    }
}


static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
    if (nullptr != commandList && pipelineHandle.handle != 0)
    {
        CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
        commandListData.pipelineState.cmd_list = commandList;
        commandListData.pipelineState.pipelineHandle = pipelineHandle;
        commandListData.pipelineState.stages = stages;

        const bool handleHasPixelShaderAttached = g_pixelShaderManager.isKnownHandle(pipelineHandle.handle);
        const bool handleHasVertexShaderAttached = g_vertexShaderManager.isKnownHandle(pipelineHandle.handle);
        if (!handleHasPixelShaderAttached && !handleHasVertexShaderAttached)
        {
            // draw call with unknown handle, don't collect it
            return;
        }
        switch (stages)
        {
        case pipeline_stage::all:
            if (g_activeCollectorFrameCounter > 0)
            {
                // in collection mode
                if (handleHasPixelShaderAttached)
                {
                    g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
                }
                if (handleHasVertexShaderAttached)
                {
                    g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
                }
            }
            else
            {
                commandListData.activePixelShaderPipeline = handleHasPixelShaderAttached ? pipelineHandle.handle : -1;
                commandListData.activeVertexShaderPipeline = handleHasVertexShaderAttached ? pipelineHandle.handle : -1;
            }
            break;
        case pipeline_stage::pixel_shader:
            if (handleHasPixelShaderAttached)
            {
                if (g_activeCollectorFrameCounter > 0)
                {
                    // in collection mode
                    g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
                }
                commandListData.activePixelShaderPipeline = pipelineHandle.handle;
            }
            break;
        case pipeline_stage::vertex_shader:
            if (handleHasVertexShaderAttached)
            {
                if (g_activeCollectorFrameCounter > 0)
                {
                    // in collection mode
                    g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
                }
                commandListData.activeVertexShaderPipeline = pipelineHandle.handle;
            }
            break;
        }
    }
}


static void ApplyBoundDescriptorSets(command_list* cmd_list, shader_stage stage, pipeline_layout layout, const vector<descriptor_set>& descriptors)
{
    for (uint32_t i = 0; i < descriptors.size(); i++)
    {
        if (descriptors[i] == 0)
            continue;

        for (uint32_t j = i + 1; j < descriptors.size() + 1; j++)
        {
            if (j == descriptors.size() || descriptors[j] == 0)
            {
                cmd_list->bind_descriptor_sets(stage, layout, i, j - i, &descriptors.data()[i]);
                i = j;
                break;
            }
        }
    }
}


static void ReApplyState(command_list* cmd_list)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    if (data.pipelineState.cmd_list != nullptr)
        cmd_list->bind_pipeline(data.pipelineState.stages, data.pipelineState.pipelineHandle);

    if (data.descriptorsState.current_layout[0] == data.descriptorsState.current_layout_bind[0])
        ApplyBoundDescriptorSets(cmd_list, shader_stage::all_graphics, data.descriptorsState.current_layout[0], data.descriptorsState.current_sets[0]);
    if (data.descriptorsState.current_layout[1] == data.descriptorsState.current_layout_bind[1])
        ApplyBoundDescriptorSets(cmd_list, shader_stage::all_compute, data.descriptorsState.current_layout[1], data.descriptorsState.current_sets[1]);

    if (data.viewportsState.cmd_list != nullptr)
        cmd_list->bind_viewports(data.viewportsState.first, data.viewportsState.count, data.viewportsState.viewports.data());
    if (data.scissorRectsState.cmd_list != nullptr)
        cmd_list->bind_scissor_rects(data.scissorRectsState.first, data.scissorRectsState.count, data.scissorRectsState.rects.data());

    cmd_list->bind_render_targets_and_depth_stencil(data.renderTargetState.count, data.renderTargetState.rtvs, data.renderTargetState.dsv);
}


static void onBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    resource_view new_view = { 0 };
    commandListData.renderTargetState = { cmd_list, count, rtvs, dsv };

    for (int i = 0; i < count; i++)
    {
        if (deviceData.current_runtime != nullptr && rtvs[i] != 0)
        {
            resource rs = device->get_resource_from_view(rtvs[i]);
            if (rs == 0)
            {
                // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
                continue;
            }

            const resource_desc texture_desc = device->get_resource_desc(rs);

            uint32_t frame_width, frame_height;
            deviceData.current_runtime->get_screenshot_width_and_height(&frame_width, &frame_height);

            if (texture_desc.texture.height == frame_height && texture_desc.texture.width == frame_width)
            {
                new_view = rtvs[i];
                break;
            }
        }
    }

    if (new_view != commandListData.active_rtv)
    {
        commandListData.active_rtv = new_view;
    }

    if (checkDrawCallForCommandList(cmd_list))
    {
        RenderEffects(cmd_list);
        ReApplyState(cmd_list);
    }
}


static void onBindDescriptorSets(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_set* sets)
{
    const int type_index = (stages == shader_stage::all_compute) ? 1 : 0;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    if (data.descriptorsState.current_layout_bind[type_index] != layout)
    {
        data.descriptorsState.current_layout[type_index] = layout;
        data.descriptorsState.current_layout_bind[type_index] = layout;

        data.descriptorsState.current_sets[type_index].clear();
    }

    if (data.descriptorsState.current_sets[type_index].size() < (count + first))
    {
        data.descriptorsState.current_sets[type_index].resize(count + first);
    }

    for (size_t i = 0; i < count; ++i)
    {
        data.descriptorsState.current_sets[type_index][i + first] = sets[i];
    }
}


static void onBindLayout(command_list* cmd_list, shader_stage stages, pipeline_layout layout)
{
    const int type_index = (stages == shader_stage::all_compute) ? 1 : 0;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    data.descriptorsState.current_layout[type_index] = layout;
}


static void onBindViewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    data.viewportsState.cmd_list = cmd_list;
    data.viewportsState.first = first;
    data.viewportsState.count = count;

    if (data.viewportsState.viewports.size() != count)
        data.viewportsState.viewports.resize(count);

    for (uint32_t i = first; i < count; i++)
    {
        data.viewportsState.viewports[i] = viewports[i];
    }
}


static void onBindScissorRects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    data.scissorRectsState.cmd_list = cmd_list;
    data.scissorRectsState.first = first;
    data.scissorRectsState.count = count;

    if (data.scissorRectsState.rects.size() != count)
        data.scissorRectsState.rects.resize(count);

    for (uint32_t i = first; i < count; i++)
    {
        data.scissorRectsState.rects[i] = rects[i];
    }
}


static void onReshadeOverlay(effect_runtime* runtime)
{
    DisplayOverlay(g_addonUIData);
}


static void onReshadePresent(effect_runtime* runtime)
{
    CheckHotkeys(g_addonUIData, runtime);
}


static void displaySettings(effect_runtime* runtime)
{
    DisplaySettings(g_addonUIData, runtime);
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule))
        {
            return FALSE;
        }
        reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
        reshade::register_event<reshade::addon_event::bind_viewports>(onBindViewports);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(onBindScissorRects);
        reshade::register_event<reshade::addon_event::bind_descriptor_sets>(onBindDescriptorSets);
        reshade::register_event<reshade::addon_event::bind_layout>(onBindLayout);
        reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
        reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
        reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
        reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
        reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
        reshade::register_event<reshade::addon_event::reshade_present>(onReshadePresent);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(onReshadeReloadedEffects);
        reshade::register_event<reshade::addon_event::reshade_set_technique_state>(onReshadeSetTechniqueState);
        reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
        reshade::register_event<reshade::addon_event::init_device>(onInitDevice);
        reshade::register_event<reshade::addon_event::destroy_device>(onDestroyDevice);
        reshade::register_event<reshade::addon_event::present>(onPresent);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
        reshade::register_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
        reshade::register_overlay(nullptr, &displaySettings);
        g_addonUIData.LoadShaderTogglerIniFile();
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
        reshade::unregister_event<reshade::addon_event::bind_viewports>(onBindViewports);
        reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(onBindScissorRects);
        reshade::unregister_event<reshade::addon_event::bind_descriptor_sets>(onBindDescriptorSets);
        reshade::unregister_event<reshade::addon_event::bind_layout>(onBindLayout);
        reshade::unregister_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
        reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
        reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
        reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(onReshadeReloadedEffects);
        reshade::unregister_event<reshade::addon_event::reshade_set_technique_state>(onReshadeSetTechniqueState);
        reshade::unregister_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
        reshade::unregister_event<reshade::addon_event::init_command_list>(onInitCommandList);
        reshade::unregister_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
        reshade::unregister_event<reshade::addon_event::reset_command_list>(onResetCommandList);
        reshade::unregister_event<reshade::addon_event::init_device>(onInitDevice);
        reshade::unregister_event<reshade::addon_event::destroy_device>(onDestroyDevice);
        reshade::unregister_event<reshade::addon_event::present>(onPresent);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
        reshade::unregister_overlay(nullptr, &displaySettings);
        reshade::unregister_addon(hModule);
        break;
    }

    return TRUE;
}
