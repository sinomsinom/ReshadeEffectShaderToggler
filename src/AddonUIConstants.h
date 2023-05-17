#pragma once

#include <unordered_map>
#include <ranges>
#include <reshade.hpp>
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include "AddonUIData.h"

static const std::unordered_set<std::string> varExclusionSet({
    "frametime",
    "framecount",
    "random",
    "pingpong",
    "date",
    "timer",
    "key",
    "mousepoint",
    "mousedelta",
    "mousebutton",
    "mousewheel",
    "ui_open",
    "overlay_open",
    "ui_active",
    "overlay_active",
    "ui_hovered",
    "overlay_hovered" });

static void DisplayConstantSettings(ShaderToggler::ToggleGroup* group)
{
    ImGui::Text("Slot #: %u", group->getSlotIndex());

    ImGui::SameLine();

    ImGui::PushID(0);
    if (ImGui::SmallButton("+"))
    {
        group->setSlotIndex(group->getSlotIndex() + 1);
    }
    ImGui::PopID();

    if (group->getSlotIndex() != 0)
    {
        ImGui::SameLine();

        if (ImGui::SmallButton("-"))
        {
            group->setSlotIndex(group->getSlotIndex() - 1);
        }
    }

    ImGui::Text("Binding #: %u", group->getDescriptorIndex());

    ImGui::SameLine();

    ImGui::PushID(2);
    if (ImGui::SmallButton("+"))
    {
        group->setDescriptorIndex(group->getDescriptorIndex() + 1);
    }
    ImGui::PopID();

    if (group->getDescriptorIndex() != 0)
    {
        ImGui::SameLine();

        ImGui::PushID(1);
        if (ImGui::SmallButton("-"))
        {
            group->setDescriptorIndex(group->getDescriptorIndex() - 1);
        }
        ImGui::PopID();
    }
}

static void DisplaySRVSettings(ShaderToggler::ToggleGroup* group)
{
    ImGui::Text("Slot #: %u", group->getSRVSlotIndex());

    ImGui::SameLine();

    ImGui::PushID(0);
    if (ImGui::SmallButton("+"))
    {
        group->setSRVSlotIndex(group->getSRVSlotIndex() + 1);
    }
    ImGui::PopID();

    if (group->getSRVSlotIndex() != 0)
    {
        ImGui::SameLine();

        if (ImGui::SmallButton("-"))
        {
            group->setSRVSlotIndex(group->getSRVSlotIndex() - 1);
        }
    }

    ImGui::Text("Binding #: %u", group->getSRVDescriptorIndex());

    ImGui::SameLine();

    ImGui::PushID(2);
    if (ImGui::SmallButton("+"))
    {
        group->setSRVDescriptorIndex(group->getSRVDescriptorIndex() + 1);
    }
    ImGui::PopID();

    if (group->getSRVDescriptorIndex() != 0)
    {
        ImGui::SameLine();

        ImGui::PushID(1);
        if (ImGui::SmallButton("-"))
        {
            group->setSRVDescriptorIndex(group->getSRVDescriptorIndex() - 1);
        }
        ImGui::PopID();
    }
}

static void DisplayConstantTab(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group, reshade::api::device* dev)
{
    if (instance.GetConstantHandler() == nullptr)
    {
        return;
    }

    const uint32_t columns = 4;
    const char* typeItems[] = { "byte", "float", "int", "uint" };
    const uint32_t typeSizes[] = { 1, 4, 4, 4 };
    static int typeSelectionIndex = 0;
    static const char* typeSelectedItem = typeItems[0];
    const uint8_t* bufferContent = instance.GetConstantHandler()->GetConstantBuffer(group);
    const size_t bufferSize = instance.GetConstantHandler()->GetConstantBufferSize(group);
    auto& varMap = group->GetVarOffsetMapping();
    const size_t offsetInputBufSize = 32;
    static char offsetInputBuf[offsetInputBufSize] = { "000" };

    bool extractionEnabled = group->getExtractConstants();
    ImGui::Checkbox("Extract constant buffer", &extractionEnabled);
    group->setExtractConstant(extractionEnabled);

    if (!extractionEnabled)
    {
        instance.GetConstantHandler()->RemoveGroup(group, dev);
    }

    ImGui::Separator();

    DisplayConstantSettings(group);

    float height = ImGui::GetWindowHeight();

    if (ImGui::BeginChild("Constant Buffer Viewer##child", { 0, height / 1.5f }, true, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::BeginCombo("View mode", typeSelectedItem, ImGuiComboFlags_None))
        {
            for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++)
            {
                bool is_selected = (typeSelectedItem == typeItems[n]);
                if (ImGui::Selectable(typeItems[n], is_selected))
                {
                    typeSelectionIndex = n;
                    typeSelectedItem = typeItems[n];
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        if (bufferContent != nullptr && bufferSize > 0 && ImGui::BeginTable("Buffer View Grid##table", columns + 1, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            size_t elements = bufferSize / typeSizes[typeSelectionIndex];

            ImGui::TableSetupScrollFreeze(0, 1);
            for (int i = 0; i < columns + 1; i++)
            {
                if (i == 0)
                {
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
                }
                else
                {
                    ImGui::TableSetupColumn(std::format("{:#04x}", (i - 1) * typeSizes[typeSelectionIndex]).c_str(), ImGuiTableColumnFlags_None);
                }
            }

            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;

            double clipElements = (static_cast<double>(elements) + static_cast<double>(elements) / static_cast<double>(columns)) / static_cast<double>(columns + 1);
            clipper.Begin(ceil(clipElements));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                {
                    for (int i = row * (columns + 1); i < row * (columns + 1) + (columns + 1); i++)
                    {
                        if (i % (columns + 1) == 0)
                        {
                            ImGui::TableNextColumn();
                            ImGui::TableHeader(std::format("{:#05x}", i / (columns + 1) * typeSizes[typeSelectionIndex] * columns).c_str());
                            continue;
                        }

                        std::stringstream sContent;

                        if (typeSelectionIndex == 0) {
                            sContent << std::format("{:02X}", bufferContent[i - i / (columns + 1) - 1]) << std::endl;
                        }
                        else
                        {
                            uint32_t bufferOffset = (i - i / (columns + 1) - 1) * typeSizes[typeSelectionIndex];

                            switch (typeSelectionIndex)
                            {
                            case 1:
                                sContent << std::format("{:.8f}", *(reinterpret_cast<const float*>(&bufferContent[bufferOffset]))) << std::endl;
                                break;
                            case 2:
                                sContent << *(reinterpret_cast<const int32_t*>(&bufferContent[bufferOffset])) << std::endl;
                                break;
                            case 3:
                                sContent << *(reinterpret_cast<const uint32_t*>(&bufferContent[bufferOffset])) << std::endl;
                                break;
                            }
                        }

                        ImGui::TableNextColumn();
                        ImGui::Text(sContent.str().c_str());
                    }
                }
            }
            clipper.End();

            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    if (ImGui::BeginChild("Constant Buffer Viewer##vars", { 0, 0 }, true, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::Button("Add Variable Binding"))
        {
            ImGui::OpenPopup("Add###const_variables");
        }

        ImGui::Separator();

        if (ImGui::BeginPopupModal("Add###const_variables", nullptr, ImGuiWindowFlags_AlwaysAutoResize) && instance.GetRESTVariables()->size() > 0)
        {
            ImGui::Text("Add constant buffer offset to variable binding:");

            static int varSelectionIndex = 0;
            std::vector<std::string> varNames;
            std::transform(instance.GetRESTVariables()->begin(), instance.GetRESTVariables()->end(), std::back_inserter(varNames),
                [](const std::pair<std::string, std::tuple<Shim::Constants::constant_type, std::vector<reshade::api::effect_uniform_variable>>>& kV)
                {
                    return kV.first;
                });
            std::vector<std::string> filteredVars;
            std::copy_if(varNames.begin(), varNames.end(), std::back_inserter(filteredVars), [](const std::string& s) { return !varExclusionSet.contains(s); });

            static std::string varSelectedItem = filteredVars.size() > 0 ? filteredVars[0] : "";

            if (ImGui::BeginCombo("Variable", varSelectedItem.c_str(), ImGuiComboFlags_None))
            {
                for (auto& v : filteredVars)
                {
                    bool is_selected = (varSelectedItem == v);
                    if (ImGui::Selectable(v.c_str(), is_selected))
                    {
                        varSelectedItem = v;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Text("0x");
            ImGui::SameLine();
            ImGui::InputText("Offset", offsetInputBuf, offsetInputBufSize, ImGuiInputTextFlags_CharsHexadecimal);

            static bool prevValue = false;

            ImGui::Checkbox("Use previous value", &prevValue);

            ImGui::Separator();

            ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2 - 120 - ImGui::GetStyle().ItemSpacing.x / 2 - ImGui::GetStyle().FramePadding.x / 2);
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                if (varSelectedItem.size() > 0)
                {
                    group->SetVarMapping(std::stoul(std::string(offsetInputBuf), nullptr, 16), varSelectedItem, prevValue);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        const char* varColumns[] = { "Variable", "Offset", "Type", "Use Previous Value" };
        std::vector<std::string> removal;

        if (varMap.size() > 0 && ImGui::BeginTable("Buffer View Grid##vartable", IM_ARRAYSIZE(varColumns) + 1, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBody))
        {
            for (int i = 0; i < IM_ARRAYSIZE(varColumns); i++)
            {
                ImGui::TableSetupColumn(varColumns[i], ImGuiTableColumnFlags_None);
            }

            ImGui::TableHeadersRow();

            for (const auto& varMapping : varMap)
            {
                if (!instance.GetRESTVariables()->contains(varMapping.first))
                    continue;

                ImGui::TableNextColumn();
                ImGui::Text(varMapping.first.c_str());
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{:#05x}", std::get<0>(varMapping.second)).c_str());
                ImGui::TableNextColumn();
                ImGui::Text(Shim::Constants::type_desc[static_cast<uint32_t>(std::get<0>(instance.GetRESTVariables()->at(varMapping.first)))]);
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{}", std::get<1>(varMapping.second)).c_str());
                ImGui::TableNextColumn();
                if (ImGui::Button(std::format("Remove##{}", varMapping.first).c_str()))
                {
                    removal.push_back(varMapping.first);
                }
            }

            ImGui::EndTable();
        }

        std::for_each(removal.begin(), removal.end(), [&group](std::string& e) { group->RemoveVarMapping(e); });

        ImGui::EndChild();
    }
}

static void DisplaySRVTab(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group, reshade::api::device* dev)
{
    bool extractionEnabled = group->getExtractResourceViews();
    ImGui::Checkbox("Extract shader resource views", &extractionEnabled);
    group->setExtractResourceViews(extractionEnabled);

    ImGui::Separator();

    DisplaySRVSettings(group);
}

static void DisplayConstantViewer(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group, reshade::api::device* dev)
{
    if (group == nullptr)
    {
        return;
    }

    ImGui::SetNextWindowSize({ 500, 800 }, ImGuiCond_Once);
    bool wndOpen = true;

    if (ImGui::Begin("Shader Resource Viewer", &wndOpen))
    {
        ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
        if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags))
        {
            if (ImGui::BeginTabItem("Constants"))
            {
                DisplayConstantTab(instance, group, dev);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Shader Resource Views"))
            {
                DisplaySRVTab(instance, group, dev);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    if (!wndOpen)
    {
        instance.EndConstantEditing();
    }
}