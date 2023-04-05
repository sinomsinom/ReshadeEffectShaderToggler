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

#include <sstream>
#include <ranges>
#include "stdafx.h"
#include "ToggleGroup.h"

namespace ShaderToggler
{
    ToggleGroup::ToggleGroup(std::string name, int id)
    {
        _name = name.size() > 0 ? name : "Default";
        _id = id;
        _isActive = false;
        _isEditing = false;
        _allowAllTechniques = true;
        _isProvidingTextureBinding = false;
        _textureBindingName = "";
        _hasTechniqueExceptions = false;
        _extractConstants = false;
        _extractResourceViews = false;
    }


    ToggleGroup::ToggleGroup() : ToggleGroup("Default", 0)
    {
    }


    int ToggleGroup::getNewGroupId()
    {
        static atomic_int s_groupId = 0;

        ++s_groupId;
        return s_groupId;
    }


    void ToggleGroup::storeCollectedHashes(const std::unordered_set<uint32_t> pixelShaderHashes, const std::unordered_set<uint32_t> vertexShaderHashes)
    {
        _vertexShaderHashes.clear();
        _pixelShaderHashes.clear();

        for (const auto hash : vertexShaderHashes)
        {
            _vertexShaderHashes.emplace(hash);
        }
        for (const auto hash : pixelShaderHashes)
        {
            _pixelShaderHashes.emplace(hash);
        }
    }


    bool ToggleGroup::isBlockedVertexShader(uint32_t shaderHash) const
    {
        return _isActive && (_vertexShaderHashes.contains(shaderHash));
    }


    bool ToggleGroup::isBlockedPixelShader(uint32_t shaderHash) const
    {
        return _isActive && (_pixelShaderHashes.contains(shaderHash));
    }


    void ToggleGroup::clearHashes()
    {
        _pixelShaderHashes.clear();
        _vertexShaderHashes.clear();
    }


    void ToggleGroup::setName(std::string newName)
    {
        if (newName.size() <= 0)
        {
            return;
        }
        _name = newName;
    }


    bool ToggleGroup::SetVarMapping(uintptr_t offset, string& variable, bool prev)
    {
        _varOffsetMapping.emplace(variable, make_tuple(offset, prev));

        return true; // do some sanity checking?
    }


    bool ToggleGroup::RemoveVarMapping(string& variable)
    {
        _varOffsetMapping.erase(variable);

        return true; // do some sanity checking?
    }


    void ToggleGroup::saveState(CDataFile& iniFile, int groupCounter) const
    {
        const std::string sectionRoot = "Group" + std::to_string(groupCounter);
        const std::string vertexHashesCategory = sectionRoot + "_VertexShaders";
        const std::string pixelHashesCategory = sectionRoot + "_PixelShaders";
        const std::string constantsCategory = sectionRoot + "_Constants";

        int counter = 0;
        for (const auto hash : _vertexShaderHashes)
        {
            iniFile.SetUInt("ShaderHash" + std::to_string(counter), hash, "", vertexHashesCategory);
            counter++;
        }
        iniFile.SetUInt("AmountHashes", counter, "", vertexHashesCategory);

        counter = 0;
        for (const auto hash : _pixelShaderHashes)
        {
            iniFile.SetUInt("ShaderHash" + std::to_string(counter), hash, "", pixelHashesCategory);
            counter++;
        }
        iniFile.SetUInt("AmountHashes", counter, "", pixelHashesCategory);

        counter = 0;
        for (const auto& [var,tuple] : _varOffsetMapping)
        {
            iniFile.SetUInt("Offset" + std::to_string(counter), static_cast<uint32_t>(std::get<0>(tuple)), "", constantsCategory);
            iniFile.SetValue("Variable" + std::to_string(counter), var, "", constantsCategory);
            iniFile.SetBool("UsePreviousValue" + std::to_string(counter), std::get<1>(tuple), "", constantsCategory);
            counter++;
        }
        iniFile.SetUInt("AmountConstants", counter, "", constantsCategory);

        iniFile.SetValue("Name", _name, "", sectionRoot);
        iniFile.SetUInt("ToggleKey", _keybind, "", sectionRoot);
        iniFile.SetBool("Active", _isActive, "", sectionRoot);

        std::stringstream ss("");
        bool firstElement = true;
        for (const auto& el : _preferredTechniques)
        {
            if (!firstElement)
            {
                ss << ",";
            }

            ss << el;

            firstElement = false;
        }
        iniFile.SetUInt("InvocationLocation", _invocationLocation, "", sectionRoot);

        iniFile.SetValue("Techniques", ss.str(), "", sectionRoot);
        iniFile.SetBool("AllowAllTechniques", _allowAllTechniques, "", sectionRoot);
        iniFile.SetBool("TechniqueExceptions", _hasTechniqueExceptions, "", sectionRoot);

        iniFile.SetBool("ProvideTextureBinding", _isProvidingTextureBinding, "", sectionRoot);
        iniFile.SetValue("TextureBindingName", _textureBindingName, "", sectionRoot);
        iniFile.SetBool("ClearTextureBindings", _clearBindings, "", sectionRoot);

        iniFile.SetBool("ExtractConstants", _extractConstants, "", sectionRoot);
        iniFile.SetUInt("ConstantPipelineSlot", _slotIndex, "", sectionRoot);
        iniFile.SetUInt("ConstantDescriptorIndex", _descIndex, "", sectionRoot);

        iniFile.SetBool("ExtractSRVs", _extractResourceViews, "", sectionRoot);
        iniFile.SetUInt("SRVPipelineSlot", _srvSlotIndex, "", sectionRoot);
        iniFile.SetUInt("SRVDescriptorIndex", _srvDescIndex, "", sectionRoot);
    }


    void ToggleGroup::loadState(CDataFile& iniFile, int groupCounter)
    {



        if (groupCounter < 0)
        {
            int amount = iniFile.GetInt("AmountHashes", "PixelShaders").value_or(0);
            for (int i = 0; i < amount; i++)
            {
                auto hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), "PixelShaders");
                if (hash.has_value())
                    _pixelShaderHashes.emplace(hash.value());
            }
            amount = iniFile.GetInt("AmountHashes", "VertexShaders").value_or(0);
            for (int i = 0; i < amount; i++)
            {
                auto hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), "VertexShaders");
                if (hash.has_value())
                    _vertexShaderHashes.emplace(hash.value());
            }

            // done
            return;
        }

        const std::string sectionRoot = "Group" + std::to_string(groupCounter);
        const std::string vertexHashesCategory = sectionRoot + "_VertexShaders";
        const std::string pixelHashesCategory = sectionRoot + "_PixelShaders";
        const std::string constantsCategory = sectionRoot + "_Constants";



        int amountShaders = iniFile.GetInt("AmountHashes", vertexHashesCategory).value_or(0);
        for (int i = 0; i < amountShaders; i++)
        {
            auto hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), vertexHashesCategory);
            if (hash.has_value())
                _vertexShaderHashes.emplace(hash.value());
        }

        amountShaders = iniFile.GetInt("AmountHashes", pixelHashesCategory).value_or(0);
        for (int i = 0; i < amountShaders; i++)
        {
            auto hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), pixelHashesCategory);
            if (hash.has_value())
                _pixelShaderHashes.emplace(hash.value());
        }

        int amountConstants = iniFile.GetInt("AmountConstants", constantsCategory).value_or(0);
        for (int i = 0; i < amountConstants; i++)
        {
            auto offset = iniFile.GetUInt("Offset" + std::to_string(i), constantsCategory);
            auto varName = iniFile.GetString("Variable" + std::to_string(i), constantsCategory);
            bool prevValue = iniFile.GetBool("UsePreviousValue" + std::to_string(i), constantsCategory).value_or(false);
            if (offset.has_value() && varName.has_value())
            {
                _varOffsetMapping.emplace(varName.value(), make_tuple(offset.value(), prevValue));
            }
        }

        _name = iniFile.GetValue("Name", sectionRoot).value_or("Default");

        _keybind = iniFile.GetUInt("ToggleKey", sectionRoot).value_or(VK_CAPITAL);

        _isActive = iniFile.GetBool("Active", sectionRoot).value_or(false);

        _invocationLocation = iniFile.GetUInt("InvocationLocation", sectionRoot).value_or(0);

        std::string techniques = iniFile.GetString("Techniques", sectionRoot).value_or("");
        if (!techniques.empty())
        {
            for (const auto& technique : techniques | std::ranges::views::split(','))
                _preferredTechniques.insert(std::string(technique.begin(), technique.end()));
        }
            
        

        _allowAllTechniques = iniFile.GetBool("AllowAllTechniques", sectionRoot).value_or(false);
        _hasTechniqueExceptions = iniFile.GetBool("TechniqueExceptions", sectionRoot).value_or(false);

        _isProvidingTextureBinding = iniFile.GetBool("ProvideTextureBinding", sectionRoot).value_or(false);
        _clearBindings = iniFile.GetBool("ClearTextureBindings", sectionRoot).value_or(true);
        _textureBindingName = iniFile.GetString("TextureBindingName", sectionRoot).value_or("");

        _extractConstants = iniFile.GetBool("ExtractConstants", sectionRoot).value_or(false);

        _slotIndex = iniFile.GetUInt("ConstantPipelineSlot", sectionRoot).value_or(2);

        _descIndex = iniFile.GetUInt("ConstantDescriptorIndex", sectionRoot).value_or(0);

        _extractResourceViews = iniFile.GetBool("ExtractSRVs", sectionRoot).value_or(false);

        _srvSlotIndex = iniFile.GetUInt("SRVPipelineSlot", sectionRoot).value_or(1);

        _srvDescIndex = iniFile.GetUInt("SRVDescriptorIndex", sectionRoot).value_or(0);
    }
}
