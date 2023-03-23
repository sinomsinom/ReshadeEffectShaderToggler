#include "RenderingManager.h"
#include "PipelinePrivateData.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;

extern void on_begin_render_effects(effect_runtime* runtime, command_list* cmd_list, resource_view, resource_view);
extern void on_finish_render_effects(effect_runtime* runtime, command_list* cmd_list, resource_view, resource_view);

size_t RenderingManager::g_charBufferSize = CHAR_BUFFER_SIZE;
char RenderingManager::g_charBuffer[CHAR_BUFFER_SIZE];

RenderingManager::RenderingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingManager::~RenderingManager()
{

}

void RenderingManager::EnumerateTechniques(effect_runtime* runtime, std::function<void(effect_runtime*, effect_technique, string&)> func)
{
    runtime->enumerate_techniques(nullptr, [func](effect_runtime* rt, effect_technique technique) {
        g_charBufferSize = CHAR_BUFFER_SIZE;
        rt->get_technique_name(technique, g_charBuffer, &g_charBufferSize);
        string name(g_charBuffer);
        func(rt, technique, name);
        });
}

void RenderingManager::_CheckCallForCommandList(ShaderData& sData, CommandListDataContainer& commandListData, const DeviceDataContainer& deviceData)
{
    // Masks which checks to perform. Note that we will always schedule a draw call check for binding and effect updates,
    // this serves the purpose of assigning the resource_view to perform the update later on if needed.
    uint32_t match_mask = MATCH_NONE;
    uint32_t queue_mask = MATCH_NONE;

    // Shift in case of VS using data id
    const uint32_t match_effect = MATCH_EFFECT_PS * sData.id;
    const uint32_t match_binding = MATCH_BINDING_PS * sData.id;
    const uint32_t match_const = MATCH_CONST_PS * sData.id;

    if (sData.blockedShaderGroups != nullptr)
    {
        for (auto group : *sData.blockedShaderGroups)
        {
            if (group->isActive())
            {
                if (group->getExtractConstants() && !deviceData.constantsUpdated.contains(group))
                {
                    if (!sData.constantBuffersToUpdate.contains(group))
                    {
                        sData.constantBuffersToUpdate.emplace(group);
                        match_mask |= match_const;
                    }
                }

                if (group->isProvidingTextureBinding() && !deviceData.bindingsUpdated.contains(group->getTextureBindingName()))
                {
                    if (!sData.bindingsToUpdate.contains(group->getTextureBindingName()))
                    {
                        sData.bindingsToUpdate.emplace(group->getTextureBindingName(), std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                        match_mask |= match_binding;
                        queue_mask |= (match_binding << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_binding << CALL_DRAW * MATCH_DELIMITER);
                    }
                }

                if (group->getAllowAllTechniques())
                {
                    for (const auto& tech : deviceData.allEnabledTechniques)
                    {
                        if (group->getHasTechniqueExceptions() && group->preferredTechniques().contains(tech.first))
                        {
                            continue;
                        }

                        if (!tech.second)
                        {
                            if (!sData.techniquesToRender.contains(tech.first))
                            {
                                sData.techniquesToRender.emplace(tech.first, std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                                match_mask |= match_effect;
                                queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << CALL_DRAW * MATCH_DELIMITER);
                            }
                        }
                    }
                }
                else if (group->preferredTechniques().size() > 0) {
                    for (auto& techName : group->preferredTechniques())
                    {
                        if (deviceData.allEnabledTechniques.contains(techName) && !deviceData.allEnabledTechniques.at(techName))
                        {
                            if (!sData.techniquesToRender.contains(techName))
                            {
                                sData.techniquesToRender.emplace(techName, std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                                match_mask |= match_effect;
                                queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << CALL_DRAW * MATCH_DELIMITER);
                            }
                        }
                    }
                }
            }
        }
    }

    commandListData.commandCheck |= match_mask;
    commandListData.commandQueue |= queue_mask;
}

void RenderingManager::CheckCallForCommandList(reshade::api::command_list* commandList, uint32_t psShaderHash, uint32_t vsShaderHash)
{
    if (nullptr == commandList)
    {
        return;
    }

    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = commandList->get_device()->get_private_data<DeviceDataContainer>();

    std::shared_lock<std::shared_mutex> r_mutex(render_mutex);
    std::shared_lock<std::shared_mutex> b_mutex(binding_mutex);

    _CheckCallForCommandList(commandListData.ps, commandListData, deviceData);
    _CheckCallForCommandList(commandListData.vs, commandListData, deviceData);

    b_mutex.unlock();
    r_mutex.unlock();
}

const resource_view RenderingManager::GetCurrentResourceView(effect_runtime* runtime, const pair<string, tuple<const ToggleGroup*, bool, resource_view>>& matchObject, CommandListDataContainer& commandListData, int32_t descIndex)
{
    resource_view active_rtv = { 0 };
    device* device = runtime->get_device();
    const ToggleGroup* group = get<0>(matchObject.second);

    const vector<resource_view>& rtvs = commandListData.stateTracker.GetBoundRenderTargetViews();

    size_t index = group->getDescriptorIndex();
    index = std::min(index, rtvs.size() - 1);

    // Only return SRVs in case of bindings
    if(descIndex > -1 && group->getExtractResourceViews())
    { 
        uint32_t slot_size = commandListData.stateTracker.GetPushDescriptorState()->current_srv[descIndex].size();
        uint32_t slot = min(group->getSRVSlotIndex(), slot_size - 1);

        if (slot_size == 0)
            return active_rtv;

        uint32_t desc_size = commandListData.stateTracker.GetPushDescriptorState()->current_srv[descIndex][slot].size();
        uint32_t desc = min(group->getSRVDescriptorIndex(), desc_size - 1);

        if (desc_size == 0)
            return active_rtv;

        resource_view buf = commandListData.stateTracker.GetPushDescriptorState()->current_srv[descIndex][slot][desc];

        active_rtv = buf;
    }
    else if (rtvs.size() > 0 && runtime != nullptr && rtvs[index] != 0)
    {
        resource rs = device->get_resource_from_view(rtvs[index]);
        if (rs == 0)
        {
            // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
            return active_rtv;
        }

        active_rtv = rtvs[index];
    }

    return active_rtv;
}

bool RenderingManager::RenderRemainingEffects(effect_runtime* runtime)
{
    if (runtime == nullptr || runtime->get_device() == nullptr)
    {
        return false;
    }

    command_list* cmd_list = runtime->get_command_queue()->get_immediate_command_list();
    device* device = runtime->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();
    resource_view active_rtv = { 0 };
    resource_view active_rtv_srgb = { 0 };
    bool rendered = false;

    resourceManager.SetBackbufferViewHandles(runtime->get_current_back_buffer().handle, &active_rtv, &active_rtv_srgb);

    if (deviceData.current_runtime == nullptr || active_rtv == 0) {
        return false;
    }

    if (deviceData.rendered_effects)
    {
        on_begin_render_effects(deviceData.current_runtime, runtime->get_command_queue()->get_immediate_command_list(), { 0 }, { 0 });

        EnumerateTechniques(deviceData.current_runtime, [&deviceData, &commandListData, &cmd_list, &device, &active_rtv, &active_rtv_srgb, &rendered](effect_runtime* runtime, effect_technique technique, string& name) {
            if (deviceData.allEnabledTechniques.contains(name) && !deviceData.allEnabledTechniques[name])
            {
                resource res = runtime->get_device()->get_resource_from_view(active_rtv);
                resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

                runtime->render_technique(technique, cmd_list, active_rtv, active_rtv_srgb);

                deviceData.allEnabledTechniques[name] = true;
                rendered = true;
            }
            });

        on_finish_render_effects(deviceData.current_runtime, runtime->get_command_queue()->get_immediate_command_list(), { 0 }, { 0 });
    }
    else
    {
        deviceData.current_runtime->render_effects(runtime->get_command_queue()->get_immediate_command_list(),
            active_rtv, active_rtv_srgb);
    }

    return rendered;
}

bool RenderingManager::_RenderEffects(
    command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<const ToggleGroup*, uint32_t, resource_view>>& techniquesToRender,
    vector<string>& removalList,
    const unordered_set<string>& toRenderNames)
{
    bool rendered = false;
    CommandListDataContainer& cmdData = cmd_list->get_private_data<CommandListDataContainer>();

    EnumerateTechniques(deviceData.current_runtime, [&cmdData, &deviceData, &techniquesToRender, &cmd_list, &rendered, &removalList, &toRenderNames, this](effect_runtime* runtime, effect_technique technique, string& name) {

        if (toRenderNames.find(name) != toRenderNames.end())
        {
            auto tech = techniquesToRender.find(name);

            if (tech != techniquesToRender.end() && !deviceData.allEnabledTechniques.at(name))
            {
                resource_view active_rtv = std::get<2>(tech->second);
                const ToggleGroup* g = std::get<0>(tech->second);

                if (active_rtv == 0)
                {
                    return;
                }

                resource res = runtime->get_device()->get_resource_from_view(active_rtv);

                resource_view view_non_srgb = active_rtv;
                resource_view view_srgb = active_rtv;

                resourceManager.SetResourceViewHandles(res.handle, &view_non_srgb, &view_srgb);

                deviceData.rendered_effects = true;

                runtime->render_technique(technique, cmd_list, view_non_srgb, view_srgb);

                resource_desc resDesc = runtime->get_device()->get_resource_desc(res);
                uiData.cFormat = resDesc.texture.format;
                removalList.push_back(name);

                deviceData.allEnabledTechniques[name] = true;
                rendered = true;
            }
        }
        });
    return rendered;
}

void RenderingManager::RenderEffects(command_list* cmd_list, uint32_t callLocation, uint32_t invocation)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || (commandListData.ps.techniquesToRender.size() == 0 && commandListData.vs.techniquesToRender.size() == 0)) {
        return;
    }

    bool toRender = false;
    unordered_set<string> psToRenderNames;
    unordered_set<string> vsToRenderNames;

    if (invocation & MATCH_EFFECT_PS)
    {
        for (auto& tech : commandListData.ps.techniquesToRender)
        {
            // Set views during draw call since we can be sure the correct ones are bound at that point
            if (!callLocation)
            {
                resource_view active_rtv = GetCurrentResourceView(deviceData.current_runtime, tech, commandListData, -1);
                std::get<2>(tech.second) = active_rtv;
            }

            // Queue updates depending on the place their supposed to be called at
            if (std::get<2>(tech.second) != 0 && (!callLocation && !std::get<1>(tech.second) || callLocation & std::get<1>(tech.second)))
            {
                psToRenderNames.insert(tech.first);
            }
        }
    }

    if (invocation & MATCH_EFFECT_VS)
    {
        for (auto& tech : commandListData.vs.techniquesToRender)
        {
            if (!callLocation)
            {
                resource_view active_rtv = GetCurrentResourceView(deviceData.current_runtime, tech, commandListData, -1);
                std::get<2>(tech.second) = active_rtv;
            }

            if (std::get<2>(tech.second) != 0 && (!callLocation && !std::get<1>(tech.second) || callLocation & std::get<1>(tech.second)))
            {
                vsToRenderNames.insert(tech.first);
            }
        }
    }

    bool rendered = false;
    vector<string> psRemovalList;
    vector<string> vsRemovalList;

    if (psToRenderNames.size() == 0 && vsToRenderNames.size() == 0)
    {
        return;
    }

    deviceData.current_runtime->render_effects(cmd_list, static_cast<resource_view>(0), static_cast<resource_view>(0));

    on_begin_render_effects(deviceData.current_runtime, cmd_list, { 0 }, { 0 });

    std::unique_lock<shared_mutex> dev_mutex(render_mutex);
    rendered = (psToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.ps.techniquesToRender, psRemovalList, psToRenderNames) ||
        (vsToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.vs.techniquesToRender, vsRemovalList, vsToRenderNames);
    dev_mutex.unlock();

    on_finish_render_effects(deviceData.current_runtime, cmd_list, { 0 }, { 0 });

    for (auto& g : psRemovalList)
    {
        commandListData.ps.techniquesToRender.erase(g);
    }

    for (auto& g : vsRemovalList)
    {
        commandListData.vs.techniquesToRender.erase(g);
    }

    if (rendered)
    {
        //std::shared_lock<std::shared_mutex> dev_mutex(pipeline_layout_mutex);
        commandListData.stateTracker.ReApplyState(cmd_list, deviceData.transient_mask);
    }
}

void RenderingManager::InitTextureBingings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    // Initialize texture bindings with default format
    for (auto& group : uiData.GetToggleGroups())
    {
        if (group.second.isProvidingTextureBinding() && group.second.getTextureBindingName().length() > 0)
        {
            resource res = {};
            resource_view srv = {};
            resource_view rtv = {};

            std::unique_lock<shared_mutex> lock(binding_mutex);
            data.bindingMap[group.second.getTextureBindingName()] = std::make_tuple(res, format::unknown, srv, rtv, 0, 0);
            runtime->update_texture_bindings(group.second.getTextureBindingName().c_str(), srv);
        }
    }
}

void RenderingManager::DisposeTextureBindings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    std::unique_lock<shared_mutex> lock(binding_mutex);

    for (auto& binding : data.bindingMap)
    {
        DestroyTextureBinding(runtime, binding.first);
    }

    data.bindingMap.clear();
}

bool RenderingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, const resource_desc& desc)
{
    reshade::api::format format = desc.texture.format;

    uint32_t frame_width, frame_height;
    frame_height = desc.texture.height;
    frame_width = desc.texture.width;

    runtime->get_command_queue()->wait_idle();

    if (!runtime->get_device()->create_resource(
        resource_desc(frame_width, frame_height, 1, 1, format_to_typeless(format), 1, memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::shader_resource | resource_usage::render_target),
        nullptr, resource_usage::shader_resource, res))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource!");
        return false;
    }

    if (!runtime->get_device()->create_resource_view(*res, resource_usage::shader_resource, resource_view_desc(format_to_default_typed(format, 0)), srv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    if (!runtime->get_device()->create_resource_view(*res, resource_usage::render_target, resource_view_desc(format_to_default_typed(format, 0)), rtv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    return true;
}


void RenderingManager::DestroyTextureBinding(effect_runtime* runtime, const string& binding)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    if (data.bindingMap.contains(binding))
    {
        resource res = { 0 };
        resource_view srv = { 0 };
        resource_view rtv = { 0 };
        reshade::api::format rformat = std::get<1>(data.bindingMap[binding]);

        runtime->get_command_queue()->wait_idle();

        res = std::get<0>(data.bindingMap[binding]);
        if (res != 0)
        {
            runtime->get_device()->destroy_resource(res);
        }

        srv = std::get<2>(data.bindingMap[binding]);
        if (srv != 0)
        {
            runtime->get_device()->destroy_resource_view(srv);
        }

        rtv = std::get<3>(data.bindingMap[binding]);
        if (rtv != 0)
        {
            runtime->get_device()->destroy_resource_view(rtv);
        }

        runtime->update_texture_bindings(binding.c_str(), resource_view{ 0 }, resource_view{ 0 });
        data.bindingMap[binding] = std::make_tuple(resource{ 0 }, rformat, resource_view{ 0 }, resource_view{ 0 }, 0, 0);
    }
}


uint32_t RenderingManager::UpdateTextureBinding(effect_runtime* runtime, const string& binding, const resource_desc& desc)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    if (data.bindingMap.contains(binding))
    {
        reshade::api::format oldFormat = std::get<1>(data.bindingMap[binding]);
        reshade::api::format format = desc.texture.format;
        uint32_t oldWidth = std::get<4>(data.bindingMap[binding]);
        uint32_t width = desc.texture.width;
        uint32_t oldHeight = std::get<4>(data.bindingMap[binding]);
        uint32_t height = desc.texture.height;

        if (format != oldFormat || oldWidth != width || oldHeight != height)
        {
            DestroyTextureBinding(runtime, binding);

            resource res = {};
            resource_view srv = {};
            resource_view rtv = {};

            if (CreateTextureBinding(runtime, &res, &srv, &rtv, desc))
            {
                data.bindingMap[binding] = std::make_tuple(res, format, srv, rtv, desc.texture.width, desc.texture.height);
                runtime->update_texture_bindings(binding.c_str(), srv);
            }
            else
            {
                return 0;
            }

            return 2;
        }
    }
    else
    {
        return 0;
    }

    return 1;
}

void RenderingManager::_UpdateTextureBindings(command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<const ToggleGroup*, uint32_t, resource_view>>& bindingsToUpdate,
    vector<string>& removalList,
    const unordered_set<string>& toUpdateBindings)
{
    for (auto& binding : bindingsToUpdate)
    {
        if (toUpdateBindings.contains(binding.first) && !deviceData.bindingsUpdated.contains(binding.first))
        {
            string bindingName = binding.first;
            effect_runtime* runtime = deviceData.current_runtime;

            resource_view active_rtv = std::get<2>(binding.second);

            if (active_rtv == 0)
            {
                continue;
            }

            if (deviceData.bindingMap.contains(bindingName))
            {
                resource res = runtime->get_device()->get_resource_from_view(active_rtv);

                if (res == 0)
                {
                    continue;
                }

                resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

                uint32_t retUpdate = UpdateTextureBinding(runtime, bindingName, resDesc);

                resource target_res = std::get<0>(deviceData.bindingMap[bindingName]);

                if (retUpdate && target_res != 0)
                {
                    cmd_list->copy_resource(res, target_res);
                    deviceData.bindingsUpdated.emplace(bindingName);
                    removalList.push_back(bindingName);
                }
            }
        }
    }
}

void RenderingManager::UpdateTextureBindings(command_list* cmd_list, uint32_t callLocation, uint32_t invocation)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || (commandListData.ps.bindingsToUpdate.size() == 0 && commandListData.vs.bindingsToUpdate.size() == 0)) {
        return;
    }

    unordered_set<string> psToUpdateBindings;
    unordered_set<string> vsToUpdateBindings;

    if (invocation & MATCH_BINDING_PS)
    {
        for (auto& tech : commandListData.ps.bindingsToUpdate)
        {
            // Set views during draw call since we can be sure the correct ones are bound at that point
            if (!callLocation)
            {
                resource_view active_rtv = GetCurrentResourceView(deviceData.current_runtime, tech, commandListData, 0);
                std::get<2>(tech.second) = active_rtv;
            }

            // Queue updates depending on the place their supposed to be called at
            if (std::get<2>(tech.second) != 0 && (!callLocation && !std::get<1>(tech.second) || callLocation & std::get<1>(tech.second)))
            {
                psToUpdateBindings.insert(tech.first);
            }
        }
    }

    if (invocation & MATCH_BINDING_VS)
    {
        for (auto& tech : commandListData.vs.bindingsToUpdate)
        {
            if (!callLocation)
            {
                resource_view active_rtv = GetCurrentResourceView(deviceData.current_runtime, tech, commandListData, 1);
                std::get<2>(tech.second) = active_rtv;
            }

            if (std::get<2>(tech.second) != 0 && (!callLocation && !std::get<1>(tech.second) || callLocation & std::get<1>(tech.second)))
            {
                vsToUpdateBindings.insert(tech.first);
            }
        }
    }

    if (psToUpdateBindings.size() == 0 && vsToUpdateBindings.size() == 0)
    {
        return;
    }

    vector<string> psRemovalList;
    vector<string> vsRemovalList;

    std::unique_lock<shared_mutex> mtx(binding_mutex);
    if (psToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.ps.bindingsToUpdate, psRemovalList, psToUpdateBindings);
    }
    if (vsToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.vs.bindingsToUpdate, vsRemovalList, vsToUpdateBindings);
    }
    mtx.unlock();

    for (auto& g : psRemovalList)
    {
        commandListData.ps.bindingsToUpdate.erase(g);
    }

    for (auto& g : vsRemovalList)
    {
        commandListData.vs.bindingsToUpdate.erase(g);
    }
}