#pragma once
#include "ConstantCopyMemcpy.h"

namespace ConstantFeedback {
    class ConstantCopyMemcpySingular final : public virtual ConstantCopyMemcpy {
    public:
        ConstantCopyMemcpySingular();
        ~ConstantCopyMemcpySingular();

        void OnMemcpy(void* dest, void* src, size_t size) override final;
        void OnMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data) override final;
        void OnUnmapBufferRegion(device* device, resource resource) override final;
    private:
        static BufferCopy _bufferCopy;
    };
}