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
#include "crc32_hash.hpp"
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include <vector>

using namespace reshade::api;
using namespace ShaderToggler;

extern "C" __declspec(dllexport) const char *NAME = "Reshade Effect Shader Toggler";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Add - on which allows you to define groups of shaders to render Reshade effects on with one key press.";

struct __declspec(uuid("038B03AA-4C75-443B-A695-752D80797037")) CommandListDataContainer {
    uint64_t activePixelShaderPipeline;
    uint64_t activeVertexShaderPipeline;
	resource_view active_rtv = resource_view{ 0 };
	atomic_bool rendered_effects = false;
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
	std::map < resource_view, resource, decltype([](const resource_view& lhs, const resource_view& rhs)
		{
			return lhs.handle < rhs.handle;
		}) > allValidRenderTargets;
	effect_runtime* current_runtime = nullptr;
};

#define FRAMECOUNT_COLLECTION_PHASE_DEFAULT 250;
#define HASH_FILE_NAME	"ReshadeEffectShaderToggler.ini"

static ShaderToggler::ShaderManager g_pixelShaderManager;
static ShaderToggler::ShaderManager g_vertexShaderManager;
static KeyData g_keyCollector;
static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static std::vector<ToggleGroup> g_toggleGroups;
static atomic_int g_toggleGroupIdKeyBindingEditing = -1;
static atomic_int g_toggleGroupIdShaderEditing = -1;
static float g_overlayOpacity = 1.0f;
static int g_startValueFramecountCollectionPhase = FRAMECOUNT_COLLECTION_PHASE_DEFAULT;


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


/// <summary>
/// Adds a default group with VK_CAPITAL as toggle key. Only used if there aren't any groups defined in the ini file.
/// </summary>
void addDefaultGroup()
{
	ToggleGroup toAdd("Default", ToggleGroup::getNewGroupId());
	toAdd.setToggleKey(VK_CAPITAL, false, false, false);
	g_toggleGroups.push_back(toAdd);
}


/// <summary>
/// Loads the defined hashes and groups from the shaderToggler.ini file.
/// </summary>
void loadShaderTogglerIniFile()
{
	// Will assume it's started at the start of the application and therefore no groups are present.

	CDataFile iniFile;
	if(!iniFile.Load(HASH_FILE_NAME))
	{
		// not there
		return;
	}
	int groupCounter = 0;
	const int numberOfGroups = iniFile.GetInt("AmountGroups", "General");
	if(numberOfGroups==INT_MIN)
	{
		// old format file?
		addDefaultGroup();
		groupCounter=-1;	// enforce old format read for pre 1.0 ini file.
	}
	else
	{
		for(int i=0;i<numberOfGroups;i++)
		{
			g_toggleGroups.push_back(ToggleGroup("", ToggleGroup::getNewGroupId()));
		}
	}
	for(auto& group: g_toggleGroups)
	{
		group.loadState(iniFile, groupCounter);		// groupCounter is normally 0 or greater. For when the old format is detected, it's -1 (and there's 1 group).
		groupCounter++;
	}
}


/// <summary>
/// Saves the currently known toggle groups with their shader hashes to the shadertoggler.ini file
/// </summary>
void saveShaderTogglerIniFile()
{
	// format: first section with # of groups, then per group a section with pixel and vertex shaders, as well as their name and key value.
	// groups are stored with "Group" + group counter, starting with 0.
	CDataFile iniFile;
	iniFile.SetInt("AmountGroups", g_toggleGroups.size(), "",  "General");

	int groupCounter = 0;
	for(const auto& group: g_toggleGroups)
	{
		group.saveState(iniFile, groupCounter);
		groupCounter++;
	}
	iniFile.SetFileName(HASH_FILE_NAME);
	iniFile.Save();
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
	CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
	commandListData.activePixelShaderPipeline = -1;
	commandListData.activeVertexShaderPipeline = -1;
	commandListData.rendered_effects = false;
	commandListData.active_rtv = { 0 };
}


static void onPresent(command_queue* queue, swapchain* swapchain, const rect*, const rect*, uint32_t, const rect*)
{
	CommandListDataContainer& commandListData = queue->get_private_data<CommandListDataContainer>();
	commandListData.active_rtv = { 0 };
	commandListData.rendered_effects = false;
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


static void onInitResourceView(device* device, resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view view)
{
	DeviceDataContainer& data = device->get_private_data<DeviceDataContainer>();

	const resource_desc texture_desc = device->get_resource_desc(resource);


	uint32_t frame_width, frame_height;
	data.current_runtime->get_screenshot_width_and_height(&frame_width, &frame_height);

	if (texture_desc.texture.samples > 1 ||
		texture_desc.texture.height != frame_height ||
		texture_desc.texture.width != frame_width ||
		texture_desc.type != resource_type::texture_2d)
	{
		return;
	}

	data.allValidRenderTargets.insert(make_pair(view, resource));
}


static void onDestroyResourceView(device* device, resource_view view)
{
	if (device == nullptr) {
		return;
	}

	DeviceDataContainer& data = device->get_private_data<DeviceDataContainer>();

	(void)std::erase_if(data.allValidRenderTargets, [&view](const auto& item) {
		auto const& [key, value] = item;
		return key.handle == view.handle;
		});
}


static void onDestroyResource(device* device, resource res)
{

	if (device == nullptr) {
		return;
	}

	DeviceDataContainer& data = device->get_private_data<DeviceDataContainer>();

	(void)std::erase_if(data.allValidRenderTargets, [&res](const auto& item) {
		auto const& [key, value] = item;
		return value.handle == res.handle;
		});
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


static void displayIsPartOfToggleGroup()
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
	ImGui::SameLine();
	ImGui::Text(" Shader is part of this toggle group.");
	ImGui::PopStyleColor();
}


static void onReshadeOverlay(reshade::api::effect_runtime *runtime)
{
	if(g_toggleGroupIdShaderEditing>=0)
	{
		ImGui::SetNextWindowBgAlpha(g_overlayOpacity);
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		if (!ImGui::Begin("ReshadeEffectShaderTogglerInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | 
														ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}
		string editingGroupName = "";
		for(auto& group:g_toggleGroups)
		{
			if(group.getId()==g_toggleGroupIdShaderEditing)
			{
				editingGroupName = group.getName();
				break;
			}
		}
		
		ImGui::Text("# of pipelines with vertex shaders: %d. # of different vertex shaders gathered: %d.", g_vertexShaderManager.getPipelineCount(), g_vertexShaderManager.getShaderCount());
		ImGui::Text("# of pipelines with pixel shaders: %d. # of different pixel shaders gathered: %d.", g_pixelShaderManager.getPipelineCount(), g_pixelShaderManager.getShaderCount());
		if(g_activeCollectorFrameCounter > 0)
		{
			const uint32_t counterValue = g_activeCollectorFrameCounter;
			ImGui::Text("Collecting active shaders... frames to go: %d", counterValue);
		}
		else
		{
			if(g_vertexShaderManager.isInHuntingMode() || g_pixelShaderManager.isInHuntingMode())
			{
				ImGui::Text("Editing the shaders for group: %s", editingGroupName.c_str());
			}
			if(g_vertexShaderManager.isInHuntingMode())
			{
				ImGui::Text("# of vertex shaders active: %d. # of vertex shaders in group: %d", g_vertexShaderManager.getAmountShaderHashesCollected(), g_vertexShaderManager.getMarkedShaderCount());
				ImGui::Text("Current selected vertex shader: %d / %d.", g_vertexShaderManager.getActiveHuntedShaderIndex(), g_vertexShaderManager.getAmountShaderHashesCollected());
				if(g_vertexShaderManager.isHuntedShaderMarked())
				{
					displayIsPartOfToggleGroup();
				}
			}
			if(g_pixelShaderManager.isInHuntingMode())
			{
				ImGui::Text("# of pixel shaders active: %d. # of pixel shaders in group: %d", g_pixelShaderManager.getAmountShaderHashesCollected(), g_pixelShaderManager.getMarkedShaderCount());
				ImGui::Text("Current selected pixel shader: %d / %d", g_pixelShaderManager.getActiveHuntedShaderIndex(), g_pixelShaderManager.getAmountShaderHashesCollected());
				if(g_pixelShaderManager.isHuntedShaderMarked())
				{
					displayIsPartOfToggleGroup();
				}
			}
		}
		ImGui::End();
	}
}


static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
	if(nullptr!=commandList && pipelineHandle.handle!=0)
	{
		const bool handleHasPixelShaderAttached = g_pixelShaderManager.isKnownHandle(pipelineHandle.handle);
		const bool handleHasVertexShaderAttached = g_vertexShaderManager.isKnownHandle(pipelineHandle.handle);
		if(!handleHasPixelShaderAttached && !handleHasVertexShaderAttached)
		{
			// draw call with unknown handle, don't collect it
			return;
		}
		CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
		switch(stages)
		{
			case pipeline_stage::all:
				if(g_activeCollectorFrameCounter>0)
				{
					// in collection mode
					if(handleHasPixelShaderAttached)
					{
						g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
					}
					if(handleHasVertexShaderAttached)
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
				if(handleHasPixelShaderAttached)
				{
					if(g_activeCollectorFrameCounter>0)
					{
						// in collection mode
						g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
					}
					commandListData.activePixelShaderPipeline = pipelineHandle.handle;
				}
				break;
			case pipeline_stage::vertex_shader:
				if(handleHasVertexShaderAttached)
				{
					if(g_activeCollectorFrameCounter>0)
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


/// <summary>
/// This function will return true if the command list specified has one or more shader hashes which are currently marked to be hidden. Otherwise false.
/// </summary>
/// <param name="commandList"></param>
/// <returns>true if the draw call has to be blocked</returns>
bool blockDrawCallForCommandList(command_list* commandList)
{
	if(nullptr==commandList)
	{
		return false;
	}

	const CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
	uint32_t shaderHash = g_pixelShaderManager.getShaderHash(commandListData.activePixelShaderPipeline);
	bool blockCall = g_pixelShaderManager.isBlockedShader(shaderHash);
	for(auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedPixelShader(shaderHash);
	}
	shaderHash = g_vertexShaderManager.getShaderHash(commandListData.activeVertexShaderPipeline);
	blockCall |= g_vertexShaderManager.isBlockedShader(shaderHash);
	for(auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedVertexShader(shaderHash);
	}
	return blockCall;
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

	if (commandListData.rendered_effects || deviceData.current_runtime == nullptr || commandListData.active_rtv == 0) {
		return;
	}

	if (deviceData.allValidRenderTargets.find(commandListData.active_rtv) == deviceData.allValidRenderTargets.end())
		return;

	commandListData.rendered_effects = true;
	deviceData.current_runtime->render_effects(cmd_list, commandListData.active_rtv);
}


static void onBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
	device* device = cmd_list->get_device();
	CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
	DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

	if (!commandListData.rendered_effects && blockDrawCallForCommandList(cmd_list)) {
		RenderEffects(cmd_list);
	}

	if (count == 1) {
		commandListData.active_rtv = rtvs[0];
	}
	else {
		for (int i = 0; i < count; i++)
		{
			if (deviceData.allValidRenderTargets.find(rtvs[i]) != deviceData.allValidRenderTargets.end()) {
				commandListData.active_rtv = rtvs[i];
				break;
			}
		}
	}
}


static void onReshadePresent(effect_runtime* runtime)
{
	if(g_activeCollectorFrameCounter>0)
	{
		--g_activeCollectorFrameCounter;
	}

	for(auto& group: g_toggleGroups)
	{
		if(group.isToggleKeyPressed(runtime))
		{
			group.toggleActive();
			// if the group's shaders are being edited, it should toggle the ones currently marked.
			if(group.getId() == g_toggleGroupIdShaderEditing)
			{
				g_vertexShaderManager.toggleHideMarkedShaders();
				g_pixelShaderManager.toggleHideMarkedShaders();
			}
		}
	}

	// hardcoded hunting keys.
	// If Ctrl is pressed too, it'll step to the next marked shader (if any)
	// Numpad 1: previous pixel shader
	// Numpad 2: next pixel shader
	// Numpad 3: mark current pixel shader as part of the toggle group
	// Numpad 4: previous vertex shader
	// Numpad 5: next vertex shader
	// Numpad 6: mark current vertex shader as part of the toggle group
	if(runtime->is_key_pressed(VK_NUMPAD1))
	{
		g_pixelShaderManager.huntPreviousShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD2))
	{
		g_pixelShaderManager.huntNextShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD3))
	{
		g_pixelShaderManager.toggleMarkOnHuntedShader();
	}
	if(runtime->is_key_pressed(VK_NUMPAD4))
	{
		g_vertexShaderManager.huntPreviousShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD5))
	{
		g_vertexShaderManager.huntNextShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD6))
	{
		g_vertexShaderManager.toggleMarkOnHuntedShader();
	}
}


/// <summary>
/// Function which marks the end of a keybinding editing cycle
/// </summary>
/// <param name="acceptCollectedBinding"></param>
/// <param name="groupEditing"></param>
void endKeyBindingEditing(bool acceptCollectedBinding, ToggleGroup& groupEditing)
{
	if (acceptCollectedBinding && g_toggleGroupIdKeyBindingEditing == groupEditing.getId() && g_keyCollector.isValid())
	{
		groupEditing.setToggleKey(g_keyCollector);
	}
	g_toggleGroupIdKeyBindingEditing = -1;
	g_keyCollector.clear();
}


/// <summary>
/// Function which marks the start of a keybinding editing cycle for the passed in toggle group
/// </summary>
/// <param name="groupEditing"></param>
void startKeyBindingEditing(ToggleGroup& groupEditing)
{
	if (g_toggleGroupIdKeyBindingEditing == groupEditing.getId())
	{
		return;
	}
	if (g_toggleGroupIdKeyBindingEditing >= 0)
	{
		endKeyBindingEditing(false, groupEditing);
	}
	g_toggleGroupIdKeyBindingEditing = groupEditing.getId();
}


/// <summary>
/// Function which marks the end of a shader editing cycle for a given toggle group
/// </summary>
/// <param name="acceptCollectedShaderHashes"></param>
/// <param name="groupEditing"></param>
void endShaderEditing(bool acceptCollectedShaderHashes, ToggleGroup& groupEditing)
{
	if(acceptCollectedShaderHashes && g_toggleGroupIdShaderEditing == groupEditing.getId())
	{
		groupEditing.storeCollectedHashes(g_pixelShaderManager.getMarkedShaderHashes(), g_vertexShaderManager.getMarkedShaderHashes());
		g_pixelShaderManager.stopHuntingMode();
		g_vertexShaderManager.stopHuntingMode();
	}
	g_toggleGroupIdShaderEditing = -1;
}


/// <summary>
/// Function which marks the start of a shader editing cycle for a given toggle group.
/// </summary>
/// <param name="groupEditing"></param>
void startShaderEditing(ToggleGroup& groupEditing)
{
	if(g_toggleGroupIdShaderEditing==groupEditing.getId())
	{
		return;
	}
	if(g_toggleGroupIdShaderEditing >= 0)
	{
		endShaderEditing(false, groupEditing);
	}
	g_toggleGroupIdShaderEditing = groupEditing.getId();
	g_activeCollectorFrameCounter = g_startValueFramecountCollectionPhase;
	g_pixelShaderManager.startHuntingMode(groupEditing.getPixelShaderHashes());
	g_vertexShaderManager.startHuntingMode(groupEditing.getVertexShaderHashes());

	// after copying them to the managers, we can now clear the group's shader.
	groupEditing.clearHashes();
}


static void showHelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(450.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}


static void displaySettings(reshade::api::effect_runtime *runtime)
{
	if (g_toggleGroupIdKeyBindingEditing >= 0)
	{
		// a keybinding is being edited. Read current pressed keys into the collector, cumulatively;
		g_keyCollector.collectKeysPressed(runtime);
	}

	if(ImGui::CollapsingHeader("General info and help"))
	{
		ImGui::PushTextWrapPos();
		ImGui::TextUnformatted("The Shader Toggler allows you to create one or more groups with shaders to toggle on/off. You can assign a keyboard shortcut (including using keys like Shift, Alt and Control) to each group, including a handy name. Each group can have one or more vertex or pixel shaders assigned to it. When you press the assigned keyboard shortcut, any draw calls using these shaders will be disabled, effectively hiding the elements in the 3D scene.");
		ImGui::TextUnformatted("\nThe following (hardcoded) keyboard shortcuts are used when you click a group's 'Change Shaders' button:");
		ImGui::TextUnformatted("* Numpad 1 and Numpad 2: previous/next pixel shader");
		ImGui::TextUnformatted("* Ctrl + Numpad 1 and Ctrl + Numpad 2: previous/next marked pixel shader in the group");
		ImGui::TextUnformatted("* Numpad 3: mark/unmark the current pixel shader as being part of the group");
		ImGui::TextUnformatted("* Numpad 4 and Numpad 5: previous/next vertex shader");
		ImGui::TextUnformatted("* Ctrl + Numpad 4 and Ctrl + Numpad 5: previous/next marked vertex shader in the group");
		ImGui::TextUnformatted("* Numpad 6: mark/unmark the current vertex shader as being part of the group");
		ImGui::TextUnformatted("\nWhen you step through the shaders, the current shader is disabled in the 3D scene so you can see if that's the shader you were looking for.");
		ImGui::TextUnformatted("When you're done, make sure you click 'Save all toggle groups' to preserve the groups you defined so next time you start your game they're loaded in and you can use them right away.");
		ImGui::PopTextWrapPos();
	}

	ImGui::AlignTextToFramePadding();
	if(ImGui::CollapsingHeader("Shader selection parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
			ImGui::SliderFloat("Overlay opacity", &g_overlayOpacity, 0.2f, 1.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::SliderInt("# of frames to collect", &g_startValueFramecountCollectionPhase, 10, 1000);
			ImGui::SameLine();
			showHelpMarker("This is the number of frames the addon will collect active shaders. Set this to a high number if the shader you want to mark is only used occasionally. Only shaders that are used in the frames collected can be marked.");
		ImGui::PopItemWidth();
	}
	ImGui::Separator();

	if(ImGui::CollapsingHeader("List of Toggle Groups", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if(ImGui::Button(" New "))
		{
			addDefaultGroup();
		}
		ImGui::Separator();

		std::vector<ToggleGroup> toRemove;
		for(auto& group : g_toggleGroups)
		{
			ImGui::PushID(group.getId());
			ImGui::AlignTextToFramePadding();
			if(ImGui::Button("X"))
			{
				toRemove.push_back(group);
			}
			ImGui::SameLine();
			ImGui::Text(" %d ", group.getId());
			ImGui::SameLine();
			if(ImGui::Button("Edit"))
			{
				group.setEditing(true);
			}

			ImGui::SameLine();
			if(g_toggleGroupIdShaderEditing>=0)
			{
				if(g_toggleGroupIdShaderEditing==group.getId())
				{
					if(ImGui::Button(" Done "))
					{
						endShaderEditing(true, group);
					}
				}
				else
				{
					ImGui::BeginDisabled(true);
					ImGui::Button("      ");
					ImGui::EndDisabled();
				}
			}
			else
			{
				if(ImGui::Button("Change shaders"))
				{
					ImGui::SameLine();
					startShaderEditing(group);
				}
			}
			ImGui::SameLine();
			ImGui::Text(" %s (%s%s)", group.getName().c_str() , group.getToggleKeyAsString().c_str(), group.isActive() ? ", is active" : "");
			if(group.isEditing())
			{
				ImGui::Separator();
				ImGui::Text("Edit group %d", group.getId());

				// Name of group
				char tmpBuffer[150];
				const string& name = group.getName();
				strncpy_s(tmpBuffer, 150, name.c_str(), name.size());
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
					ImGui::AlignTextToFramePadding();
					ImGui::Text("Name");
					ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);
					ImGui::InputText("##Name", tmpBuffer, 149);
					group.setName(tmpBuffer);
				ImGui::PopItemWidth();

				// Key binding of group
				bool isKeyEditing = false;
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
					ImGui::AlignTextToFramePadding();
					ImGui::Text("Key shortcut");
					ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);
					string textBoxContents = (g_toggleGroupIdKeyBindingEditing == group.getId()) ? g_keyCollector.getKeyAsString() : group.getToggleKeyAsString();	// The 'press a key' is inside keycollector
					string toggleKeyName = group.getToggleKeyAsString();
					ImGui::InputText("##Key shortcut", (char*)textBoxContents.c_str(), textBoxContents.size(), ImGuiInputTextFlags_ReadOnly);
					if(ImGui::IsItemClicked())
					{
						startKeyBindingEditing(group);
					}
					if(g_toggleGroupIdKeyBindingEditing==group.getId())
					{
						isKeyEditing = true;
						ImGui::SameLine();
						if (ImGui::Button("OK"))
						{
							endKeyBindingEditing(true, group);
						}
						ImGui::SameLine();
						if (ImGui::Button("Cancel"))
						{
							endKeyBindingEditing(false, group);
						}
					}
				ImGui::PopItemWidth();

				if(!isKeyEditing)
				{
					if(ImGui::Button("OK"))
					{
						group.setEditing(false);
						g_toggleGroupIdKeyBindingEditing = -1;
						g_keyCollector.clear();
					}
				}
				ImGui::Separator();
			}
					
			ImGui::PopID();
		}
		if(toRemove.size()>0)
		{
			// switch off keybinding editing or shader editing, if in progress
			g_toggleGroupIdKeyBindingEditing = -1;
			g_keyCollector.clear();
			g_toggleGroupIdShaderEditing = -1;
			g_pixelShaderManager.stopHuntingMode();
			g_vertexShaderManager.stopHuntingMode();
		}
		for(const auto& group : toRemove)
		{
			std::erase(g_toggleGroups, group);
		}

		ImGui::Separator();
		if(g_toggleGroups.size()>0)
		{
			if(ImGui::Button("Save all Toggle Groups"))
			{
				saveShaderTogglerIniFile();
			}
		}
	}
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
		reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::register_event<reshade::addon_event::init_device>(onInitDevice);
		reshade::register_event<reshade::addon_event::destroy_device>(onDestroyDevice);
		reshade::register_event<reshade::addon_event::present>(onPresent);
		reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
		reshade::register_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
		reshade::register_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
		reshade::register_event<reshade::addon_event::destroy_resource_view>(onDestroyResourceView);
		reshade::register_event<reshade::addon_event::init_resource_view>(onInitResourceView);
		reshade::register_event<reshade::addon_event::destroy_resource>(onDestroyResource);
		reshade::register_overlay(nullptr, &displaySettings);
		loadShaderTogglerIniFile();
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
		reshade::unregister_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
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
		reshade::unregister_event<reshade::addon_event::destroy_resource_view>(onDestroyResourceView);
		reshade::unregister_event<reshade::addon_event::init_resource_view>(onInitResourceView);
		reshade::unregister_event<reshade::addon_event::destroy_resource>(onDestroyResource);
		reshade::unregister_overlay(nullptr, &displaySettings);
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}
