#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include "ToggleGroup.h"
#include "ConstantHandlerMemcpy.h"

using namespace std;
using namespace reshade::api;
using namespace ShaderToggler;

namespace ConstantFeedback {
    static struct BufferCopy
    {
        uint64_t resource = 0;
        void* destination = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t bufferSize = 0;
    };

    class ConstantCopyMethod {
    public:
        ConstantCopyMethod(ConstantHandlerMemcpy* constHandler);
        ~ConstantCopyMethod();

        virtual void OnMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data) = 0;
        virtual void OnUnmapBufferRegion(device* device, resource resource) = 0;
        virtual void OnMemcpy(void* dest, void* src, size_t size) = 0;
    protected:
        ConstantHandlerMemcpy* _constHandler;
    };
}
