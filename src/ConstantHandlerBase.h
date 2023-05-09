#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include <functional>
#include <shared_mutex>
#include "ToggleGroup.h"
#include "ShaderManager.h"
#include "ConstantCopyBase.h"

using namespace std;
using namespace reshade::api;
using namespace ShaderToggler;

struct CommandListDataContainer;
struct DeviceDataContainer;

namespace ConstantFeedback {
    enum class constant_type
    {
        type_unknown = 0,
        type_float,
        type_float2,
        type_float3,
        type_float4,
        type_float3x3,
        type_float4x3,
        type_float4x4,
        type_int,
        type_uint
    };

    static constexpr size_t type_size[] =
    {
        0,	// dummy
        4,	// float
        4,	// float2
        4,	// float3
        4,  // float4
        4,	// float3x3
        4,  // float4x3
        4,	// float4x4
        4,	// int
        4	// uint
    };

    static constexpr size_t type_length[] =
    {
        0,	// dummy
        1,	// float
        2,	// float2
        3,	// float3
        4,	// float4
        9,	// float3x3
        12, // float4x3
        16,	// float4x4
        1,	// int
        1	// uint
    };

    static constexpr const char* type_desc[] =
    {
        "",	// dummy
        "float",	// float
        "float2",	// float2
        "float3",	// float3
        "float4",	// float4
        "float3x3",	// float3x3
        "float4x3", // float4x3
        "float4x4",	// float4x4
        "int",	// int
        "uint"	// uint
    };

    static constexpr size_t CHAR_BUFFER_SIZE = 256;

    class __declspec(novtable) ConstantHandlerBase final {
    public:
        ConstantHandlerBase();
        ~ConstantHandlerBase();

        void SetBufferRange(const ToggleGroup* group, buffer_range range, device* dev, command_list* cmd_list);
        void RemoveGroup(const ToggleGroup*, device* dev);
        uint8_t* GetConstantBuffer(const ToggleGroup* group);
        size_t GetConstantBufferSize(const ToggleGroup* group);
        void ReloadConstantVariables(effect_runtime* runtime);
        void UpdateConstants(command_list* cmd_list);
        void ClearConstantVariables();
        void ApplyConstantValues(effect_runtime* runtime, const ToggleGroup*, const unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>>& constants);

        void OnReshadeReloadedEffects(effect_runtime* runtime, int32_t enabledCount);
        void OnReshadeSetTechniqueState(effect_runtime* runtime, int32_t enabledCount);

        unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>>* GetRESTVariables();

        static void SetConstantCopy(ConstantCopyBase* constantHandler);
    private:
        unordered_map<const ToggleGroup*, vector<uint8_t>> groupBufferContent;
        unordered_map<const ToggleGroup*, vector<uint8_t>> groupPrevBufferContent;
        unordered_map<const ToggleGroup*, size_t> groupBufferSize;
        int32_t previousEnableCount = std::numeric_limits<int32_t>::max();
        shared_mutex varMutex;

        static unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>> restVariables;
        static char charBuffer[CHAR_BUFFER_SIZE];

        unordered_map<const ToggleGroup*, buffer_range> groupBufferRanges;

        static ConstantCopyBase* _constCopy;

        bool CreateScratchpad(const ToggleGroup* group, device* dev, resource_desc& target);
        void CopyToScratchpad(const ToggleGroup* group, device* dev, command_list* cmd_list);
        bool UpdateConstantEntries(command_list* cmd_list, CommandListDataContainer& cmdData, DeviceDataContainer& devData, const ToggleGroup* group, uint32_t index);
    };
}