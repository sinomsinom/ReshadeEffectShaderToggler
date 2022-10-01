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

enum PipelineBindingTypes : uint32_t
{
    unknown = 0,
    bind_render_target,
    bind_viewport,
    bind_scissor_rect,
    bind_descriptors,
    bind_pipeline_states,
    push_constants
};

struct PipelineBindingBase
{
public:
    command_list* cmd_list = nullptr;
    uint32_t callIndex = 0;
    virtual PipelineBindingTypes GetType() { return PipelineBindingTypes::unknown; }
};

template<PipelineBindingTypes T>
struct PipelineBinding : PipelineBindingBase
{
public:
    PipelineBindingTypes GetType() override { return T; }
};

struct BindRenderTargetsState : PipelineBinding<PipelineBindingTypes::bind_render_target> {
    uint32_t count;
    vector<resource_view> rtvs;
    resource_view dsv;

    void Reset()
    {
        callIndex = 0;
        cmd_list = nullptr;
        rtvs.clear();
        dsv = { 0 };
        count = 0;
    }
};

struct BindViewportsState : PipelineBinding<PipelineBindingTypes::bind_viewport> {
    uint32_t first;
    uint32_t count;
    vector<viewport> viewports;

    void Reset()
    {
        callIndex = 0;
        cmd_list = nullptr;
        first = 0;
        count = 0;
        viewports.clear();
    }
};

struct BindScissorRectsState : PipelineBinding<PipelineBindingTypes::bind_scissor_rect> {
    uint32_t first;
    uint32_t count;
    vector<rect> rects;

    void Reset()
    {
        callIndex = 0;
        cmd_list = nullptr;
        first = 0;
        count = 0;
        rects.clear();
    }
};

struct PushConstantsState : PipelineBinding<PipelineBindingTypes::push_constants> {
    uint32_t layout_param;
    uint32_t first;
    uint32_t count;
    vector<uint32_t> values;

    void Reset()
    {
        callIndex = 0;
        cmd_list = nullptr;
        layout_param = 0;
        first = 0;
        count = 0;
        values.clear();
    }
};

struct BindDescriptorsState : PipelineBinding<PipelineBindingTypes::bind_descriptors> {
    pipeline_layout current_layout[2];
    vector<descriptor_set> current_sets[2];
    unordered_map<uint64_t, vector<bool>> transient_mask;

    void Reset()
    {
        callIndex = 0;
        cmd_list = nullptr;
        current_layout[0] = { 0 };
        current_layout[1] = { 0 };
        current_sets[0].clear();
        current_sets[1].clear();
        transient_mask.clear();
    }
};


struct BindPipelineStatesState : PipelineBinding<PipelineBindingTypes::bind_pipeline_states> {
    uint32_t value;
    bool valuesSet;
    dynamic_state state;

    BindPipelineStatesState(dynamic_state s)
    {
        state = s;
        Reset();
    }

    void Reset()
    {
        callIndex = 0;
        cmd_list = nullptr;
        value = 0;
        valuesSet = false;
    }
};

struct BindPipelineStatesStates {
    BindPipelineStatesState states[2] = { BindPipelineStatesState(dynamic_state::blend_constant), BindPipelineStatesState(dynamic_state::primitive_topology) };

    void Reset()
    {
        states[0].Reset();
        states[1].Reset();
    }
};


struct __declspec(uuid("222F7169-3C09-40DB-9BC9-EC53842CE537")) CommandListDataContainer {
    uint64_t activePixelShaderPipeline;
    uint64_t activeVertexShaderPipeline;
    vector<resource_view> active_rtvs;
    atomic_bool rendered_effects = false;
    unordered_set<string> techniquesToRender;

    uint32_t callIndex;
    BindRenderTargetsState renderTargetState;
    BindDescriptorsState descriptorsState;
    BindViewportsState viewportsState;
    BindScissorRectsState scissorRectsState;
    BindPipelineStatesStates pipelineStatesState;
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
    effect_runtime* current_runtime = nullptr;
    unordered_map<string, bool> allEnabledTechniques;
    unordered_map<uint64_t, uint32_t> rootSigParamCount;
};

#define CHAR_BUFFER_SIZE 256
#define MAX_EFFECT_HANDLES 128

static ShaderToggler::ShaderManager g_pixelShaderManager;
static ShaderToggler::ShaderManager g_vertexShaderManager;
static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static vector<string> allTechniques;
static AddonUIData g_addonUIData(&g_pixelShaderManager, &g_vertexShaderManager, &g_activeCollectorFrameCounter, &allTechniques);
static shared_mutex s_render_mutex;
static shared_mutex s_resource_mutex;
static char g_charBuffer[CHAR_BUFFER_SIZE];
static size_t g_charBufferSize = CHAR_BUFFER_SIZE;
static unordered_set<uint64_t> s_resources;

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
    commandListData.active_rtvs.clear();

    commandListData.callIndex = 0;
    commandListData.renderTargetState.Reset();
    commandListData.descriptorsState.Reset();
    commandListData.scissorRectsState.Reset();
    commandListData.viewportsState.Reset();
    commandListData.pipelineStatesState.Reset();

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

    shared_lock<shared_mutex> lock(s_render_mutex);

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


static bool RenderEffects(command_list* cmd_list)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return false;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr || commandListData.active_rtvs.size() == 0) {
        return false;
    }

    resource_view active_rtv = { 0 };
    for (int i = 0; i < commandListData.active_rtvs.size(); i++)
    {
        if (deviceData.current_runtime != nullptr && commandListData.active_rtvs[i] != 0)
        {
            resource rs = device->get_resource_from_view(commandListData.active_rtvs[i]);
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
                active_rtv = commandListData.active_rtvs[i];
                break;
            }
        }
    }

    if (active_rtv == 0)
        return false;

    unique_lock<shared_mutex> lock(s_render_mutex);
    if (commandListData.techniquesToRender.size() > 0) {

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

            deviceData.current_runtime->render_effects(cmd_list, active_rtv);

            for (auto& tech : enabledTechniques)
            {
                deviceData.current_runtime->set_technique_state(tech, true);
            }

            commandListData.rendered_effects = true;
        }

        enumerateTechniques(deviceData.current_runtime, [cmd_list, &deviceData, &commandListData, &active_rtv](effect_runtime* runtime, effect_technique technique, string& name) {
            if (commandListData.techniquesToRender.contains(name) && !deviceData.allEnabledTechniques.at(name))
            {
                runtime->render_technique(technique, cmd_list, active_rtv);
            }
            });

        for (auto& tech : commandListData.techniquesToRender)
        {
            deviceData.allEnabledTechniques.at(tech) = true;
        }
        commandListData.techniquesToRender.clear();

        return true;
    }

    return false;
}


static void ApplyBoundDescriptorSets(command_list* cmd_list, shader_stage stage, pipeline_layout layout, size_t param_count,
    const vector<descriptor_set>& descriptors, const vector<bool>& mask)
{
    size_t count = min(descriptors.size(), param_count);
    for (uint32_t i = 0; i < count; i++)
    {
        if (descriptors[i] == 0 || (i < mask.size() && mask[i]))
            continue;

        for (uint32_t j = i + 1; j < count + 1; j++)
        {
            if (j == count || descriptors[j] == 0 || (j < mask.size() && mask[j]))
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
    auto& devData = cmd_list->get_device()->get_private_data<DeviceDataContainer>();

    vector<PipelineBindingBase*> blah = {
        &data.descriptorsState,
        &data.renderTargetState,
        &data.scissorRectsState,
        &data.viewportsState,
        &data.pipelineStatesState.states[0],
        &data.pipelineStatesState.states[1],
    };

    std::sort(blah.begin(), blah.end(), [](const auto& lhs, const auto& rhs)
        {
            return lhs->callIndex < rhs->callIndex;
        });

    for (auto b : blah)
    {
        if (b->GetType() == PipelineBindingTypes::bind_descriptors)
        {
            if (data.descriptorsState.cmd_list != nullptr)
            {
                ApplyBoundDescriptorSets(cmd_list, shader_stage::all_graphics, data.descriptorsState.current_layout[0],
                    devData.rootSigParamCount[data.descriptorsState.current_layout[0].handle],
                    data.descriptorsState.current_sets[0], data.descriptorsState.transient_mask[data.descriptorsState.current_layout[0].handle]);
                ApplyBoundDescriptorSets(cmd_list, shader_stage::all_compute, data.descriptorsState.current_layout[1],
                    devData.rootSigParamCount[data.descriptorsState.current_layout[1].handle],
                    data.descriptorsState.current_sets[1], data.descriptorsState.transient_mask[data.descriptorsState.current_layout[1].handle]);
            }
        }

        if (b->GetType() == PipelineBindingTypes::bind_render_target)
        {
            if (data.renderTargetState.cmd_list != nullptr)
                cmd_list->bind_render_targets_and_depth_stencil(data.renderTargetState.count, data.renderTargetState.rtvs.data(), data.renderTargetState.dsv);
        }

        if (b->GetType() == PipelineBindingTypes::bind_scissor_rect)
        {
            if (data.scissorRectsState.cmd_list != nullptr)
                cmd_list->bind_scissor_rects(data.scissorRectsState.first, data.scissorRectsState.count, data.scissorRectsState.rects.data());
        }

        if (b->GetType() == PipelineBindingTypes::bind_viewport)
        {
            if (data.viewportsState.cmd_list != nullptr)
                cmd_list->bind_viewports(data.viewportsState.first, data.viewportsState.count, data.viewportsState.viewports.data());
        }

        if (b->GetType() == PipelineBindingTypes::bind_pipeline_states)
        {
            BindPipelineStatesState* ss = static_cast<BindPipelineStatesState*>(b);
            if (ss->cmd_list != nullptr)
                cmd_list->bind_pipeline_states(1, &ss->state, &ss->value);
        }
    }
}


static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
    if (nullptr != commandList && pipelineHandle.handle != 0)
    {
        CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();

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

        if (checkDrawCallForCommandList(commandList) && RenderEffects(commandList))
        {
            if (commandList->get_device()->get_api() == device_api::d3d12)
                ReApplyState(commandList);
        }
    }
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

    commandListData.renderTargetState.callIndex = commandListData.callIndex;
    commandListData.callIndex++;

    commandListData.renderTargetState.cmd_list = cmd_list;
    commandListData.renderTargetState.count = count;
    commandListData.renderTargetState.dsv = dsv;

    if (commandListData.renderTargetState.rtvs.size() != count) {
        commandListData.renderTargetState.rtvs.resize(count);
        commandListData.active_rtvs.resize(count);
    }

    for (int i = 0; i < count; i++)
    {
        commandListData.renderTargetState.rtvs[i] = rtvs[i];
        commandListData.active_rtvs[i] = rtvs[i];
    }
}


static void onBindDescriptorSets(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_set* sets)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    const int type_index = (stages == shader_stage::all_compute) ? 1 : 0;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.descriptorsState.callIndex = data.callIndex;
    data.callIndex++;

    data.descriptorsState.cmd_list = cmd_list;

    if (data.descriptorsState.current_layout[type_index] != layout)
    {
        data.descriptorsState.transient_mask[layout.handle].clear();
    }

    data.descriptorsState.current_layout[type_index] = layout;

    if (data.descriptorsState.transient_mask[layout.handle].size() < count + first)
    {
        data.descriptorsState.transient_mask[layout.handle].resize(count + first);
    }

    if (data.descriptorsState.current_sets[type_index].size() < (count + first))
    {
        data.descriptorsState.current_sets[type_index].resize(count + first);
    }

    for (size_t i = 0; i < count; ++i)
    {
        data.descriptorsState.current_sets[type_index][i + first] = sets[i];
        data.descriptorsState.transient_mask[layout.handle][i + first] = false;
    }
}


static void onPushConstants(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, uint32_t first, uint32_t count, const uint32_t* values)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    const int type_index = (stages == shader_stage::all_compute) ? 1 : 0;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    if (data.descriptorsState.transient_mask[layout.handle].size() < layout_param + 1)
    {
        data.descriptorsState.transient_mask[layout.handle].resize(layout_param + 1, false);
    }

    data.descriptorsState.transient_mask[layout.handle][layout_param] = true;
}


static void onBindViewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.viewportsState.callIndex = data.callIndex;
    data.callIndex++;

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
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.scissorRectsState.callIndex = data.callIndex;
    data.callIndex++;

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


static void on_bind_pipeline_states(command_list* cmd_list, uint32_t count, const dynamic_state* states, const uint32_t* values)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    auto& data = cmd_list->get_private_data<CommandListDataContainer>();

    for (uint32_t i = 0; i < count; i++)
    {
        if (states[i] == dynamic_state::primitive_topology)
        {
            data.pipelineStatesState.states[1].cmd_list = cmd_list;
            data.pipelineStatesState.states[1].callIndex = data.callIndex;
            data.pipelineStatesState.states[1].value = values[i];
            data.pipelineStatesState.states[1].valuesSet = true;
            data.callIndex++;
        }
        else if (states[i] == dynamic_state::blend_constant)
        {
            data.pipelineStatesState.states[0].cmd_list = cmd_list;
            data.pipelineStatesState.states[0].callIndex = data.callIndex;
            data.pipelineStatesState.states[0].value = values[i];
            data.pipelineStatesState.states[0].valuesSet = true;
            data.callIndex++;
        }
    }
}


static void onInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    std::unique_lock<shared_mutex> lock(s_resource_mutex);
    s_resources.emplace(handle.handle);
    lock.unlock();
}


static void onDestroyResource(device* device, resource res)
{
    std::unique_lock<shared_mutex> lock(s_resource_mutex);
    s_resources.erase(res.handle);
    lock.unlock();
}


static void onInitPipelineLayout(device* device, uint32_t param_count, const pipeline_layout_param* params, pipeline_layout layout)
{
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();
    deviceData.rootSigParamCount[layout.handle] = param_count;
}


static void onDestroyPipelineLayout(device* device, pipeline_layout layout)
{
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();
    deviceData.rootSigParamCount.erase(layout.handle);
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
        reshade::register_event<reshade::addon_event::init_resource>(onInitResource);
        reshade::register_event<reshade::addon_event::destroy_resource>(onDestroyResource);
        reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
        reshade::register_event<reshade::addon_event::bind_viewports>(onBindViewports);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(onBindScissorRects);
        reshade::register_event<reshade::addon_event::bind_descriptor_sets>(onBindDescriptorSets);
        reshade::register_event<reshade::addon_event::push_constants>(onPushConstants);
        reshade::register_event<reshade::addon_event::init_pipeline_layout>(onInitPipelineLayout);
        reshade::register_event<reshade::addon_event::destroy_pipeline_layout>(onDestroyPipelineLayout);
        reshade::register_event<reshade::addon_event::bind_pipeline_states>(on_bind_pipeline_states);
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
        reshade::unregister_event<reshade::addon_event::init_resource>(onInitResource);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(onDestroyResource);
        reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
        reshade::unregister_event<reshade::addon_event::bind_viewports>(onBindViewports);
        reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(onBindScissorRects);
        reshade::unregister_event<reshade::addon_event::bind_descriptor_sets>(onBindDescriptorSets);
        reshade::unregister_event<reshade::addon_event::push_constants>(onPushConstants);
        reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(onInitPipelineLayout);
        reshade::unregister_event<reshade::addon_event::destroy_pipeline_layout>(onDestroyPipelineLayout);
        reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(on_bind_pipeline_states);
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
