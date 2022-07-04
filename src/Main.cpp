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
#include <tuple>
#include <chrono>
#include "crc32_hash.hpp"
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include "AddonUIData.h"
#include "AddonUIDisplay.h"
#include "ExternalVariablesPipe.h"

using namespace reshade::api;
using namespace ShaderToggler;
using namespace AddonImGui;
//using namespace sigmatch_literals;

extern "C" __declspec(dllexport) const char *NAME = "Reshade Effect Shader Toggler";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Addon which allows you to define groups of shaders to render Reshade effects on.";

struct __declspec(uuid("222F7169-3C09-40DB-9BC9-EC53842CE537")) CommandListDataContainer {
    uint64_t activePixelShaderPipeline;
    uint64_t activeVertexShaderPipeline;
	vector<resource_view> active_rtv_history = vector<resource_view>(MAX_RT_HISTORY);
	atomic_bool rendered_effects = false;
	unordered_map<string, int32_t> techniquesToRender;
	unordered_map<string, int32_t> bindingsToUpdate;
	unordered_set < pair<string, int32_t>,
		decltype([](const pair<string, int32_t>& v) {
			return std::hash<std::string>{}(v.first); // Don't care about history index, we'll overwrite them in case of collisions
		}),
		decltype([](const pair<string, int32_t>& lhs, const pair<string, int32_t>& rhs) {
			return lhs.first == rhs.first;
		})> immediateActionSet;
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
	effect_runtime* current_runtime = nullptr;
	unordered_map<string, bool> allEnabledTechniques;
	unordered_map<string, tuple<resource, reshade::api::format, resource_view, resource_view>> bindingMap;
	unordered_set<string> bindingsUpdated;
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
static reshade::api::format g_prevFormat = {};
static const float clearColor[] = { 0, 0, 0, 0 };
//static ExternalVariablesPipe variablePipe;

/// <summary>
/// Calculates a crc32 hash from the passed in shader bytecode. The hash is used to identity the shader in future runs.
/// </summary>
/// <param name="shaderData"></param>
/// <returns></returns>
static uint32_t calculateShaderHash(void* shaderData)
{
	if(nullptr==shaderData)
	{
		return 0;
	}

	const auto shaderDesc = *static_cast<shader_desc *>(shaderData);
	return compute_crc32(static_cast<const uint8_t *>(shaderDesc.code), shaderDesc.code_size);
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


static void onInitCommandList(command_list *commandList)
{
	commandList->create_private_data<CommandListDataContainer>();
}


static void onDestroyCommandList(command_list *commandList)
{
	commandList->destroy_private_data<CommandListDataContainer>();
}

static void onResetCommandList(command_list *commandList)
{
	std::unique_lock lock(s_mutex);
	CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
	DeviceDataContainer& deviceData = commandList->get_device()->get_private_data<DeviceDataContainer>();

	commandListData.activePixelShaderPipeline = -1;
	commandListData.activeVertexShaderPipeline = -1;
	commandListData.rendered_effects = false;
	commandListData.active_rtv_history.clear();

	std::for_each(deviceData.allEnabledTechniques.begin(), deviceData.allEnabledTechniques.end(), [](auto& el) {
		el.second = false;
		});
	deviceData.bindingsUpdated.clear();
}


static void onPresent(command_queue* queue, swapchain* swapchain, const rect*, const rect*, uint32_t, const rect*)
{
	std::unique_lock lock(s_mutex);
	CommandListDataContainer& commandListData = queue->get_private_data<CommandListDataContainer>();
	DeviceDataContainer& deviceData = queue->get_device()->get_private_data<DeviceDataContainer>();

	commandListData.rendered_effects = false;
	commandListData.techniquesToRender.clear();
	commandListData.active_rtv_history.clear();

	std::for_each(deviceData.allEnabledTechniques.begin(), deviceData.allEnabledTechniques.end(), [](auto& el) {
		el.second = false;
		});
	deviceData.bindingsUpdated.clear();
}


static void onReshadeReloadedEffects(effect_runtime* runtime)
{
	std::unique_lock lock(s_mutex);
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
	std::unique_lock lock(s_mutex);
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


static bool CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, reshade::api::format format)
{
	DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

	uint32_t frame_width, frame_height;
	runtime->get_screenshot_width_and_height(&frame_width, &frame_height);

	runtime->get_command_queue()->wait_idle();

	if (!runtime->get_device()->create_resource(
		resource_desc(frame_width, frame_height, 1, 1, format_to_typeless(format), 1, memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::shader_resource | resource_usage::render_target),
		nullptr, resource_usage::shader_resource, res))
	{
		reshade::log_message(ERROR, "Failed to create texture binding resource!");
		return false;
	}

	if (!runtime->get_device()->create_resource_view(*res, resource_usage::shader_resource, resource_view_desc(format_to_default_typed(format, 0)), srv))
	{
		reshade::log_message(ERROR, "Failed to create texture binding resource view!");
		return false;
	}

	if (!runtime->get_device()->create_resource_view(*res, resource_usage::render_target, resource_view_desc(format_to_default_typed(format, 0)), rtv))
	{
		reshade::log_message(ERROR, "Failed to create texture binding resource view!");
		return false;
	}

	return true;
}


static void DestroyTextureBinding(effect_runtime* runtime, std::string binding)
{
	DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

	if (data.bindingMap.contains(binding))
	{
		resource res = { 0 };
		resource_view srv = { 0 };
		resource_view rtv = { 0 };

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
	}
}


static bool UpdateTextureBinding(effect_runtime* runtime, std::string binding, reshade::api::format format)
{
	DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

	if (data.bindingMap.contains(binding))
	{
		reshade::api::format oldFormat = std::get<1>(data.bindingMap[binding]);
		if (format != oldFormat)
		{
			DestroyTextureBinding(runtime, binding);

			resource res = {};
			resource_view srv = {};
			resource_view rtv = {};

			if (CreateTextureBinding(runtime, &res, &srv, &rtv, format))
			{
				stringstream ss;
				ss << "TEXTURE SEMANTIC BINDING RECREATED. NEW FORMAT=" << (uint32_t)format << " OLD FORMAT=" << (uint32_t)oldFormat << " BINDING = " << binding << endl;
				reshade::log_message(2, ss.str().c_str());

				data.bindingMap[binding] = std::make_tuple(res, format, srv, rtv);
				runtime->update_texture_bindings(binding.c_str(), srv);
				//runtime->set_uniform_value_float
			}
			else
			{
				return false;
			}
		}
	}
	else
	{
		return false;
	}

	return true;
}


static void onInitEffectRuntime(effect_runtime* runtime)
{
	DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
	data.current_runtime = runtime;

	// Initialize texture bindings with default format
	for (auto& group : g_addonUIData.GetToggleGroups())
	{
		if (group.second.isProvidingTextureBinding() && group.second.getTextureBindingName().length() > 0)
		{
			resource res = {};
			resource_view srv = {};
			resource_view rtv = {};
			
			if (CreateTextureBinding(runtime, &res, &srv, &rtv, format::r8g8b8a8_unorm))
			{
				data.bindingMap.emplace(group.second.getTextureBindingName(), std::make_tuple(res, format::r8g8b8a8_unorm, srv, rtv));
				runtime->update_texture_bindings(group.second.getTextureBindingName().c_str(), srv);
			}
		}
	}
}


static void onDestroyEffectRuntime(effect_runtime* runtime)
{
	DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
	data.current_runtime = nullptr;

	for (const auto& binding : data.bindingMap)
	{
		DestroyTextureBinding(runtime, binding.first);
	}
	data.bindingMap.clear();
}


static void onInitPipeline(device *device, pipeline_layout, uint32_t subobjectCount, const pipeline_subobject *subobjects, pipeline pipelineHandle)
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


static void onDestroyPipeline(device *device, pipeline pipelineHandle)
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

	std::unique_lock lock(s_mutex);

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
		if (tGroup->isProvidingTextureBinding())
		{
			if (commandListData.bindingsToUpdate.contains(tGroup->getTextureBindingName()))
			{
				if (tGroup->getHistoryIndex() < commandListData.bindingsToUpdate[tGroup->getTextureBindingName()])
					commandListData.bindingsToUpdate[tGroup->getTextureBindingName()] = tGroup->getHistoryIndex();
			}
			else
			{
				commandListData.bindingsToUpdate.emplace(tGroup->getTextureBindingName(), tGroup->getHistoryIndex());
			}
		}

		if (tGroup->getAllowAllTechniques())
		{
			for (const auto& tech : deviceData.allEnabledTechniques)
			{
				if (!tech.second)
				{
					if (commandListData.techniquesToRender.contains(tech.first))
					{
						if(tGroup->getHistoryIndex() < commandListData.techniquesToRender[tech.first])
							commandListData.techniquesToRender[tech.first] = tGroup->getHistoryIndex();
					}
					else
					{
						commandListData.techniquesToRender.emplace(tech.first, tGroup->getHistoryIndex());
					}
				}
			}
		}
		else if (tGroup->preferredTechniques().size() > 0) {
			for (auto& techName : tGroup->preferredTechniques())
			{
				if (deviceData.allEnabledTechniques.contains(techName) && !deviceData.allEnabledTechniques[techName])
				{
					if (commandListData.techniquesToRender.contains(techName))
					{
						if (tGroup->getHistoryIndex() < commandListData.techniquesToRender[techName])
							commandListData.techniquesToRender[techName] = tGroup->getHistoryIndex();
					}
					else
					{
						commandListData.techniquesToRender.emplace(techName, tGroup->getHistoryIndex());
					}
				}
			}
		}
	}

	return commandListData.techniquesToRender.size() > 0 || commandListData.bindingsToUpdate.size() > 0;
}


static void RenderDummyEffects(command_list* cmd_list, resource_view view, DeviceDataContainer& deviceData, CommandListDataContainer& commandListData)
{
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

		deviceData.current_runtime->render_effects(cmd_list, view);

		for (auto& tech : enabledTechniques)
		{
			deviceData.current_runtime->set_technique_state(tech, true);
		}

		commandListData.rendered_effects = true;
	}
}


static resource_view GetCurrentResourceView(const pair<string, int32_t>& matchObject, CommandListDataContainer& commandListData)
{
	resource_view active_rtv = { 0 };

	if (commandListData.active_rtv_history.size() > abs(matchObject.second) && matchObject.second <= 0)
	{
		active_rtv = commandListData.active_rtv_history[abs(matchObject.second)];
	}
	else if (matchObject.second < commandListData.active_rtv_history.size() - 1)
	{
		active_rtv = commandListData.active_rtv_history[commandListData.active_rtv_history.size() - 1];
	}
	else
	{
		active_rtv = commandListData.active_rtv_history[0];
	}

	return active_rtv;
}


static void UpdateTextureBindings(command_list* cmd_list, bool dec = false)
{
	if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
	{
		return;
	}

	device* device = cmd_list->get_device();
	CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
	DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

	if (deviceData.current_runtime == nullptr || commandListData.active_rtv_history.size() == 0 || commandListData.bindingsToUpdate.size() == 0) {
		return;
	}

	for (const auto& binding : commandListData.bindingsToUpdate)
	{
		if (binding.second <= 0) {
			commandListData.immediateActionSet.emplace(binding);
		}
	}

	if (commandListData.immediateActionSet.size() == 0)
	{
		if (dec)
		{
			for (auto& tech : commandListData.bindingsToUpdate)
			{
				if (commandListData.bindingsToUpdate[tech.first] > 0)
					commandListData.bindingsToUpdate[tech.first]--;
			}
		}

		return;
	}

	for (const auto& binding : commandListData.immediateActionSet)
	{
		string bindingName = binding.first;

		if (deviceData.bindingsUpdated.contains(bindingName)) {
			continue;
		}

		resource_view active_rtv = GetCurrentResourceView(binding, commandListData);

		if (active_rtv == 0)
		{
			continue;
		}
		
		effect_runtime* runtime = deviceData.current_runtime;
		if (deviceData.bindingMap.contains(bindingName))
		{
			//resource_view_desc desc = runtime->get_device()->get_resource_view_desc(active_rtv);
			resource res = runtime->get_device()->get_resource_from_view(active_rtv);
			resource_desc resDesc = runtime->get_device()->get_resource_desc(res);
			if (UpdateTextureBinding(runtime, bindingName, resDesc.texture.format))
			{
				g_addonUIData.cFormat = resDesc.texture.format;
				cmd_list->copy_resource(res, std::get<0>(deviceData.bindingMap[bindingName]));
				deviceData.bindingsUpdated.emplace(bindingName);
			}
		}
	}

	for (auto& tech : commandListData.immediateActionSet)
	{
		commandListData.bindingsToUpdate.erase(tech.first);
	}

	if (dec)
	{
		for (auto& tech : commandListData.bindingsToUpdate)
		{
			if (commandListData.bindingsToUpdate[tech.first] > 0)
				commandListData.bindingsToUpdate[tech.first]--;
		}
	}

	commandListData.immediateActionSet.clear();
}


static void UpdateEffectVariables(effect_runtime* runtime, string& effect)
{
	//const auto variables = variablePipe.GetVariableValues();
	//
	//for (const auto& kV : variables)
	//{
	//	effect_uniform_variable var = runtime->find_uniform_variable(effect.c_str(), kV.first.c_str());
	//	if (var != 0)
	//	{
	//		runtime->set_uniform_value_float(var, &kV.second, 1, 0);
	//	}
	//}
}


static void RenderEffects(command_list* cmd_list, bool inc = false)
{
	if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
	{
		return;
	}

	device* device = cmd_list->get_device();
	CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
	DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

	if (deviceData.current_runtime == nullptr || commandListData.active_rtv_history.size() == 0 || commandListData.techniquesToRender.size() == 0) {
		return;
	}

	for (const auto& tech : commandListData.techniquesToRender)
	{
		if (tech.second <= 0) {
			commandListData.immediateActionSet.emplace(tech);
		}
	}

	if (commandListData.immediateActionSet.size() == 0)
	{
		if (inc)
		{
			for (auto& tech : commandListData.techniquesToRender)
			{
				if (commandListData.techniquesToRender[tech.first] > 0)
					commandListData.techniquesToRender[tech.first]--;
			}
		}

		return;
	}

	std::unique_lock lock(s_mutex);
	enumerateTechniques(deviceData.current_runtime, [&deviceData, &commandListData, &cmd_list](effect_runtime* runtime, effect_technique technique, string& name) {
		auto historic_rtv = commandListData.immediateActionSet.find(pair<string, int32_t>(name, 0));

		if (historic_rtv != commandListData.immediateActionSet.end() && historic_rtv->second <= 0 && !deviceData.allEnabledTechniques[name])
		{
			resource_view active_rtv = GetCurrentResourceView(*historic_rtv, commandListData);

			if (active_rtv == 0)
			{
				return;
			}

			resource res = runtime->get_device()->get_resource_from_view(active_rtv);
			resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

			g_addonUIData.cFormat = resDesc.texture.format;
			if (g_prevFormat != resDesc.texture.format)
			{
				stringstream ss;
				ss << "FORMAT CHANGED, BACKBUFFER RECREATED. NEW FORMAT=" << (uint32_t)resDesc.texture.format << " OLD FORMAT=" << (uint32_t)g_prevFormat << " TECHNIQUE=" << name << endl;
				g_prevFormat = resDesc.texture.format;
				reshade::log_message(2, ss.str().c_str());
			}

			if (!commandListData.rendered_effects)
			{
				RenderDummyEffects(cmd_list, active_rtv, deviceData, commandListData);
			}

			UpdateEffectVariables(runtime, name);
			
			runtime->render_technique(technique, cmd_list, active_rtv);
			deviceData.allEnabledTechniques[name] = true;
		}
		});

	for (auto& tech : commandListData.immediateActionSet)
	{
		commandListData.techniquesToRender.erase(tech.first);
	}
	if (inc)
	{
		for (auto& tech : commandListData.techniquesToRender)
		{

			if (commandListData.techniquesToRender[tech.first] > 0)
				commandListData.techniquesToRender[tech.first]--;
		}
	}

	commandListData.immediateActionSet.clear();
}


static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
	if (nullptr != commandList && pipelineHandle.handle != 0)
	{
		const bool handleHasPixelShaderAttached = g_pixelShaderManager.isKnownHandle(pipelineHandle.handle);
		const bool handleHasVertexShaderAttached = g_vertexShaderManager.isKnownHandle(pipelineHandle.handle);
		if (!handleHasPixelShaderAttached && !handleHasVertexShaderAttached)
		{
			// draw call with unknown handle, don't collect it
			return;
		}
		CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
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

		//if (stages == pipeline_stage::pixel_shader || stages == pipeline_stage::vertex_shader)
		//{
			(void)checkDrawCallForCommandList(commandList);

			UpdateTextureBindings(commandList);
			RenderEffects(commandList);
		//}
	}
}


static void onBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
	if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
	{
		return;
	}

	UpdateTextureBindings(cmd_list, true);
	RenderEffects(cmd_list, true);

	device* device = cmd_list->get_device();
	CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
	DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

	resource_view new_view = { 0 };

	for (int i = 0; i < count; i++)
	{
		if (deviceData.current_runtime != nullptr && rtvs[i] != 0)
		{
			resource rs = device->get_resource_from_view(rtvs[i]);
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
	
	if (new_view != 0)
	{
		if (commandListData.active_rtv_history.size() >= MAX_RT_HISTORY)
		{
			commandListData.active_rtv_history.pop_back();
		}

		commandListData.active_rtv_history.insert(commandListData.active_rtv_history.begin(), new_view);
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
