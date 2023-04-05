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

#pragma once

#include <format>
#include "AddonUIConstants.h"
#include "KeyData.h"

#define MAX_DESCRIPTOR_INDEX 10

// From Reshade, see https://github.com/crosire/reshade/blob/main/source/imgui_widgets.cpp
static bool key_input_box(const char* name, uint32_t* keys, const effect_runtime* runtime)
{
    char buf[48]; buf[0] = '\0';
    if (*keys)
        buf[reshade_key_name(*keys).copy(buf, sizeof(buf) - 1)] = '\0';

    ImGui::InputTextWithHint(name, "Click to set keyboard shortcut", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

    if (ImGui::IsItemActive())
    {
        const uint32_t last_key_pressed = reshade_last_key_pressed(runtime);
        if (last_key_pressed != 0)
        {
            if (last_key_pressed == static_cast<uint32_t>(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
            {
                *keys = 0;

            }
            else if (last_key_pressed < 0x10 || last_key_pressed > 0x12) // Exclude modifier keys
            {
                *keys = last_key_pressed;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x11)) << 8;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x10)) << 16;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x12)) << 24;
            }

            return true;
        }
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Click in the field and press any key to change the shortcut to that key.");
    }

    return false;
}


static constexpr const char* invocationDescription[] =
{
    "BEFORE DRAW",
    "AFTER DRAW",
    "ON RENDER TARGET CHANGE"
};


static void DisplayIsPartOfToggleGroup()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    ImGui::Text(" Shader is part of this toggle group.");
    ImGui::PopStyleColor();
}


static void DisplayTechniqueSelection(AddonImGui::AddonUIData& instance, ToggleGroup* group)
{
    if (group == nullptr)
    {
        return;
    }

    const uint32_t columns = 2;
    const vector<string>* techniques = instance.GetAllTechniques();
    unordered_set<string> curTechniques = group->preferredTechniques();
    unordered_set<string> newTechniques;
    static char searchBuf[256] = "\0";

    ImGui::SetNextWindowSize({ 500, 300 }, ImGuiCond_Once);
    bool wndOpen = true;
    if (ImGui::Begin("Technique selection", &wndOpen))
    {
        if (ImGui::BeginChild("Technique selection##child", { 0, 0 }, true, ImGuiWindowFlags_AlwaysAutoResize))
        {
            bool allowAll = group->getAllowAllTechniques();
            ImGui::Checkbox("Catch all techniques", &allowAll);
            group->setAllowAllTechniques(allowAll);

            bool exceptions = group->getHasTechniqueExceptions();
            if (allowAll)
            {
                ImGui::SameLine();
                ImGui::Checkbox("Except for selected techniques", &exceptions);
                group->setHasTechniqueExceptions(exceptions);
            }

            if (ImGui::Button("Untick all"))
            {
                curTechniques.clear();
            }

            ImGui::SameLine();
            ImGui::Text("Search: ");
            ImGui::SameLine();
            ImGui::InputText("##techniqueSearch", searchBuf, 256, ImGuiInputTextFlags_None);

            ImGui::Separator();

            if (allowAll && !exceptions)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::BeginTable("Technique selection##table", columns, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders))
            {
                string prefix(searchBuf);

                for (int i = 0; i < techniques->size(); i++)
                {
                    bool enabled = curTechniques.find(techniques->at(i)) != curTechniques.end();

                    if (techniques->at(i).rfind(prefix, 0) == 0)
                    {
                        ImGui::TableNextColumn();
                        ImGui::Checkbox(techniques->at(i).c_str(), &enabled);
                    }

                    if (enabled)
                    {
                        newTechniques.insert(techniques->at(i));
                    }
                }
            }
            ImGui::EndTable();

            if (allowAll && !exceptions)
            {
                ImGui::EndDisabled();
            }
        }
        ImGui::EndChild();

        group->setPreferredTechniques(newTechniques);
    }
    ImGui::End();

    if (!wndOpen)
    {
        instance.EndEffectEditing();
    }
}

static void DisplayOverlay(AddonImGui::AddonUIData& instance, effect_runtime* runtime)
{
    if (instance.GetToggleGroupIdConstantEditing() >= 0)
    {
        ToggleGroup* tGroup = nullptr;
        const int idx = instance.GetToggleGroupIdConstantEditing();
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            tGroup = &instance.GetToggleGroups()[idx];
        }

        DisplayConstantViewer(instance, tGroup, runtime->get_device());
    }

    if (instance.GetToggleGroupIdEffectEditing() >= 0)
    {
        ToggleGroup* tGroup = nullptr;
        const int idx = instance.GetToggleGroupIdEffectEditing();
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            tGroup = &instance.GetToggleGroups()[idx];
        }

        DisplayTechniqueSelection(instance, tGroup);
    }

    if (instance.GetToggleGroupIdShaderEditing() >= 0)
    {
        string editingGroupName = "";
        const int idx = instance.GetToggleGroupIdShaderEditing();
        ToggleGroup* tGroup = nullptr;
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            editingGroupName = instance.GetToggleGroups()[idx].getName();
            tGroup = &instance.GetToggleGroups()[idx];
        }

        bool wndOpen = true;
        ImGui::SetNextWindowBgAlpha(*instance.OverlayOpacity());
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        if (!ImGui::Begin(std::format("Edit group {}", editingGroupName).c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("# of pipelines with vertex shaders: %d. # of different vertex shaders gathered: %d.", instance.GetVertexShaderManager()->getPipelineCount(), instance.GetVertexShaderManager()->getShaderCount());
        ImGui::Text("# of pipelines with pixel shaders: %d. # of different pixel shaders gathered: %d.", instance.GetPixelShaderManager()->getPipelineCount(), instance.GetPixelShaderManager()->getShaderCount());
        if (*instance.ActiveCollectorFrameCounter() > 0)
        {
            const uint32_t counterValue = *instance.ActiveCollectorFrameCounter();
            ImGui::Text("Collecting active shaders... frames to go: %d", counterValue);
        }
        else
        {
            if (instance.GetVertexShaderManager()->isInHuntingMode() || instance.GetPixelShaderManager()->isInHuntingMode())
            {
                ImGui::Text("Editing the shaders for group: %s", editingGroupName.c_str());
                if (tGroup != nullptr)
                {
                    ImGui::Text("Invocation location: %s", invocationDescription[tGroup->getInvocationLocation()]);
                    ImGui::Text("Render target index: %d", tGroup->getDescriptorIndex());
                    ImGui::Text("Render target format %d: ", (uint32_t)instance.cFormat);
                }
            }
            if (instance.GetVertexShaderManager()->isInHuntingMode())
            {
                ImGui::Text("# of vertex shaders active: %d. # of vertex shaders in group: %d", instance.GetVertexShaderManager()->getAmountShaderHashesCollected(), instance.GetVertexShaderManager()->getMarkedShaderCount());
                ImGui::Text("Current selected vertex shader: %d / %d.", instance.GetVertexShaderManager()->getActiveHuntedShaderIndex(), instance.GetVertexShaderManager()->getAmountShaderHashesCollected());
                if (instance.GetVertexShaderManager()->isHuntedShaderMarked())
                {
                    DisplayIsPartOfToggleGroup();
                }
            }
            if (instance.GetPixelShaderManager()->isInHuntingMode())
            {
                ImGui::Text("# of pixel shaders active: %d. # of pixel shaders in group: %d", instance.GetPixelShaderManager()->getAmountShaderHashesCollected(), instance.GetPixelShaderManager()->getMarkedShaderCount());
                ImGui::Text("Current selected pixel shader: %d / %d", instance.GetPixelShaderManager()->getActiveHuntedShaderIndex(), instance.GetPixelShaderManager()->getAmountShaderHashesCollected());
                if (instance.GetPixelShaderManager()->isHuntedShaderMarked())
                {
                    DisplayIsPartOfToggleGroup();
                }
            }
        }
        ImGui::End();
    }
}

static void CheckHotkeys(AddonImGui::AddonUIData& instance, effect_runtime* runtime)
{
    if (*instance.ActiveCollectorFrameCounter() > 0)
    {
        --(*instance.ActiveCollectorFrameCounter());
    }

    if (instance.GetToggleGroupIdShaderEditing() == -1)
    {
        return;
    }

    ToggleGroup* editGroup = nullptr;

    for (auto& group : instance.GetToggleGroups())
    {
        if (group.second.getId() == instance.GetToggleGroupIdShaderEditing())
        {
            editGroup = &group.second;
            break;
        }

        if (group.second.getToggleKey() > 0 && areKeysPressed(group.second.getToggleKey(), runtime))
        {
            group.second.toggleActive();
            // if the group's shaders are being edited, it should toggle the ones currently marked.
            if (group.second.getId() == instance.GetToggleGroupIdShaderEditing())
            {
                instance.GetVertexShaderManager()->toggleHideMarkedShaders();
                instance.GetPixelShaderManager()->toggleHideMarkedShaders();
            }
        }
    }

    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_DOWN), runtime))
    {
        instance.GetPixelShaderManager()->huntPreviousShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_UP), runtime))
    {
        instance.GetPixelShaderManager()->huntNextShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_MARKED_DOWN), runtime))
    {
        instance.GetPixelShaderManager()->huntPreviousShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_MARKED_UP), runtime))
    {
        instance.GetPixelShaderManager()->huntNextShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_MARK), runtime))
    {
        instance.GetPixelShaderManager()->toggleMarkOnHuntedShader();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_DOWN), runtime))
    {
        instance.GetVertexShaderManager()->huntPreviousShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_UP), runtime))
    {
        instance.GetVertexShaderManager()->huntNextShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_MARKED_DOWN), runtime))
    {
        instance.GetVertexShaderManager()->huntPreviousShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_MARKED_UP), runtime))
    {
        instance.GetVertexShaderManager()->huntNextShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_MARK), runtime))
    {
        instance.GetVertexShaderManager()->toggleMarkOnHuntedShader();
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::INVOCATION_DOWN), runtime))
    {
        if (instance.GetInvocationLocation() > 0)
        {
            instance.GetInvocationLocation()--;
            if (editGroup != nullptr)
            {
                editGroup->setInvocationLocation(instance.GetInvocationLocation());
            }
        }
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::INVOCATION_UP), runtime))
    {
        if (instance.GetInvocationLocation() < 2)
        {
            instance.GetInvocationLocation()++;
            if (editGroup != nullptr)
            {
                editGroup->setInvocationLocation(instance.GetInvocationLocation());
            }
        }
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::DESCRIPTOR_DOWN), runtime))
    {
        if (instance.GetDescriptorIndex() > 0)
        {
            instance.GetDescriptorIndex()--;
            if (editGroup != nullptr)
            {
                editGroup->setDescriptorIndex(instance.GetDescriptorIndex());
            }
        }
    }
    if (areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::DESCRIPTOR_UP), runtime))
    {
        if (instance.GetDescriptorIndex() < MAX_DESCRIPTOR_INDEX)
        {
            instance.GetDescriptorIndex()++;
            if (editGroup != nullptr)
            {
                editGroup->setDescriptorIndex(instance.GetDescriptorIndex());
            }
        }
    }
}


static void ShowHelpMarker(const char* desc)
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


static void ParameterSection(AddonImGui::AddonUIData& instance)
{
    if (ImGui::CollapsingHeader("Shader selection parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
        ImGui::SliderFloat("Overlay opacity", instance.OverlayOpacity(), 0.0f, 1.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::SliderInt("# of frames to collect", instance.StartValueFramecountCollectionPhase(), 10, 1000);
        ImGui::SameLine();
        ShowHelpMarker("This is the number of frames the addon will collect active shaders. Set this to a high number if the shader you want to mark is only used occasionally. Only shaders that are used in the frames collected can be marked.");
        ImGui::PopItemWidth();
    }
}

static void OptionSection(AddonImGui::AddonUIData& instance)
{
    if (ImGui::CollapsingHeader("General Options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool autoSaveConfig = instance.IsAutoSaveConfig();
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
        ImGui::LabelText("##SetAutoSave","Autosave config");
        ImGui::SameLine();
        ImGui::Checkbox("##SetAutoSave", &autoSaveConfig);
        instance.SetAutoSaveConfig(autoSaveConfig);
        ImGui::PopItemWidth();
    }
}

static void InfoSection()
{
    if (ImGui::CollapsingHeader("General info and help"))
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
}

static void KeybindingsSection(AddonImGui::AddonUIData& instance, effect_runtime* runtime)
{
    if (ImGui::CollapsingHeader("Keybindings", ImGuiTreeNodeFlags_None))
    {
        for (uint32_t i = 0; i < IM_ARRAYSIZE(AddonImGui::KeybindNames); i++)
        {
            uint32_t keys = instance.GetKeybinding(static_cast<AddonImGui::Keybind>(i));
            ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.35f);
            if (key_input_box(AddonImGui::KeybindNames[i], &keys, runtime))
            {
                instance.SetKeybinding(static_cast<AddonImGui::Keybind>(i), keys);
            }
            ImGui::PopItemWidth();
        }
    }
}

static void ListOfToggleGroupsSection(AddonImGui::AddonUIData& instance, effect_runtime* runtime)
{
    if (ImGui::CollapsingHeader("List of Toggle Groups", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button(" New "))
        {
            instance.AddDefaultGroup();
        }
        ImGui::Separator();

        std::vector<ToggleGroup> toRemove;
        for (auto& [groupKey, group] : instance.GetToggleGroups())
        {

            ImGui::PushID(group.getId());
            ImGui::AlignTextToFramePadding();
            if (ImGui::Button("X"))
            {
                toRemove.push_back(group);
            }
            ImGui::SameLine();
            ImGui::Text(" %d ", group.getId());

            ImGui::SameLine();
            bool groupActive = group.isActive();
            ImGui::Checkbox("Active", &groupActive);
            if (groupActive != group.isActive())
            {
                group.toggleActive();

                if (!groupActive && instance.GetConstantHandler() != nullptr)
                {
                    instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
                }
                instance.AutoSave();
            }

            ImGui::SameLine();
            if (ImGui::Button("Edit"))
            {
                group.setEditing(true);
            }

            ImGui::SameLine();
            if (instance.GetToggleGroupIdShaderEditing() >= 0)
            {
                if (instance.GetToggleGroupIdShaderEditing() == group.getId())
                {
                    if (ImGui::Button(" Done "))
                    {
                        instance.EndShaderEditing(true, group);
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
                if (ImGui::Button("Change shaders"))
                {
                    ImGui::SameLine();
                    instance.StartShaderEditing(group);
                }
            }

            ImGui::SameLine();
            if (instance.GetToggleGroupIdEffectEditing() >= 0)
            {
                if (instance.GetToggleGroupIdEffectEditing() == group.getId())
                {
                    if (ImGui::Button("  Done  "))
                    {
                        instance.EndEffectEditing();
                    }
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Button("        ");
                    ImGui::EndDisabled();
                }
            }
            else
            {
                if (ImGui::Button("Change effects"))
                {
                    instance.StartEffectEditing(group);
                    instance.GetInvocationLocation() = group.getInvocationLocation();
                }
            }

            ImGui::SameLine();
            if (instance.GetToggleGroupIdConstantEditing() >= 0)
            {
                if (instance.GetToggleGroupIdConstantEditing() == group.getId())
                {
                    if (ImGui::Button("Done"))
                    {
                        instance.EndConstantEditing();
                    }
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Button("    ");
                    ImGui::EndDisabled();
                }
            }
            else
            {
                if (ImGui::Button("Constants"))
                {
                    instance.StartConstantEditing(group);
                }
            }

            ImGui::SameLine();
            if (group.getToggleKey() > 0)
            {
                ImGui::Text(" %s (%s)", group.getName().c_str(), reshade_key_name(group.getToggleKey()).c_str());
            }
            else
            {
                ImGui::Text(" %s", group.getName().c_str());
            }

            if (group.isEditing())
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

                // Name of Binding
                bool isBindingEnabled = group.isProvidingTextureBinding();
                const string& bindingName = group.getTextureBindingName();
                strncpy_s(tmpBuffer, 150, bindingName.c_str(), bindingName.size());
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Binding Name");
                ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);
                if (isBindingEnabled)
                {
                    ImGui::InputText("##BindingName", tmpBuffer, 149);
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::InputText("##BindingName", tmpBuffer, 149);
                    ImGui::EndDisabled();
                }
                ImGui::PopItemWidth();

                group.setTextureBindingName(tmpBuffer);

                ImGui::SameLine(ImGui::GetWindowWidth() * 0.905f);
                ImGui::Checkbox("##isBindingEnabled", &isBindingEnabled);
                group.setProvidingTextureBinding(isBindingEnabled);

                // Key binding of group
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Key shortcut");
                ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);

                uint32_t keys = group.getToggleKey();
                if (key_input_box(reshade_key_name(keys).c_str(), &keys, runtime))
                {
                    group.setToggleKey(keys);
                }
                ImGui::PopItemWidth();

                if (ImGui::Button("OK"))
                {
                    group.setEditing(false);
                    instance.AutoSave();
                }
                ImGui::Separator();
            }

            ImGui::PopID();
        }
        if (toRemove.size() > 0)
        {
            // switch off keybinding editing or shader editing, if in progress
            instance.GetToggleGroupIdEffectEditing() = -1;
            instance.GetToggleGroupIdShaderEditing() = -1;
            instance.GetToggleGroupIdConstantEditing() = -1;
            instance.StopHuntingMode();
        }
        for (const auto& group : toRemove)
        {
            std::erase_if(instance.GetToggleGroups(), [&group](const auto& item) {
                return item.first == group.getId();
                });
        }

        if (toRemove.size() > 0)
        {
            instance.UpdateToggleGroupsForShaderHashes();
        }

        ImGui::Separator();
        if (instance.GetToggleGroups().size() > 0)
        {
            if (ImGui::Button("Save all Toggle Groups"))
            {
                instance.SaveShaderTogglerIniFile();
            }
        }
    }
}

static void DisplaySettings(AddonImGui::AddonUIData& instance, effect_runtime* runtime)
{
    //Major Section 1
    InfoSection();

    ImGui::AlignTextToFramePadding();
    ParameterSection(instance);

    OptionSection(instance);

    ImGui::Separator();

    KeybindingsSection(instance, runtime);

    ListOfToggleGroupsSection(instance, runtime);
}
