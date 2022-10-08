#include "ConstantCopyMethodSingularMapping.h"

using namespace ConstantFeedback;

ConstantCopyMethodSingularMapping::ConstantCopyMethodSingularMapping(ConstantHandlerMemcpy* constHandler) : ConstantCopyMethod(constHandler)
{

}

ConstantCopyMethodSingularMapping::~ConstantCopyMethodSingularMapping()
{

}

void ConstantCopyMethodSingularMapping::OnMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data)
{
    if ((access == map_access::write_discard || access == map_access::write_only) && _bufferCopy.resource == 0)
    {
        resource_desc desc = device->get_resource_desc(resource);
        if (desc.heap == memory_heap::cpu_to_gpu && static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer))
        {
            _bufferCopy.resource = resource.handle;
            _bufferCopy.destination = *data;
            _bufferCopy.size = size;
            _bufferCopy.offset = offset;
            _bufferCopy.bufferSize = desc.buffer.size;
        }
    }
}

void ConstantCopyMethodSingularMapping::OnUnmapBufferRegion(device* device, resource resource)
{
    if (_bufferCopy.resource != 0)
    {

        resource_desc desc = device->get_resource_desc(resource);
        if (desc.heap == memory_heap::cpu_to_gpu && static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer))
        {
            _bufferCopy.resource = 0;
            _bufferCopy.destination = nullptr;
        }
    }
}

void ConstantCopyMethodSingularMapping::OnMemcpy(void* dest, void* src, size_t size)
{
    if (_bufferCopy.resource != 0 && dest >= _bufferCopy.destination && static_cast<uintptr_t>(reinterpret_cast<intptr_t>(dest)) <= reinterpret_cast<intptr_t>(_bufferCopy.destination) + _bufferCopy.bufferSize - _bufferCopy.offset)
    {
        _constHandler->SetHostConstantBuffer(_bufferCopy.resource, src, size, reinterpret_cast<intptr_t>(dest) - reinterpret_cast<intptr_t>(_bufferCopy.destination), _bufferCopy.bufferSize);
    }
}