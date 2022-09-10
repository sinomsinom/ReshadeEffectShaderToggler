#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include "ToggleGroup.h"
#include "ConstantHandlerBase.h"

using namespace std;
using namespace reshade::api;
using namespace ShaderToggler;

namespace ConstantFeedback {
    class ConstantHandler : public virtual ConstantHandlerBase {
    public:
        ConstantHandler();
        ~ConstantHandler();

        void SetBufferRange(const ToggleGroup* group, buffer_range range, device* dev, command_list* cmd_list, command_queue* queue);
        void RemoveGroup(const ToggleGroup*, device* dev, command_queue* queue);
        using ConstantHandlerBase::GetConstantBuffer;
        using ConstantHandlerBase::GetConstantBufferSize;
        using ConstantHandlerBase::ApplyConstantValues;
    private:
        unordered_map<const ToggleGroup*, resource> groupBufferResourceScratchpad;
        unordered_map<const ToggleGroup*, buffer_range> groupBufferRanges;

        void DestroyScratchpad(const ToggleGroup* group, device* dev, command_queue* queue);
        bool CreateScratchpad(const ToggleGroup* group, device* dev, resource_desc& target);
        void CopyToScratchpad(const ToggleGroup* group, device* dev, command_list* cmd_list, command_queue* queue);
    };
}