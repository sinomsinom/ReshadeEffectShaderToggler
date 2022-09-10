#include <cstring>
#include "ConstantHandler.h"

using namespace ConstantFeedback;

ConstantHandler::ConstantHandler()
{
}

ConstantHandler::~ConstantHandler()
{
}

void ConstantHandler::SetBufferRange(const ToggleGroup* group, buffer_range range, device* dev, command_list* cmd_list, command_queue* queue)
{
    if (dev == nullptr || cmd_list == nullptr || range.buffer == 0)
    {
        return;
    }

    if (!groupBufferContent.contains(group))
    {
        groupBufferRanges.emplace(group, range);
        groupBufferResourceScratchpad.emplace(group, resource{ 0 });
        groupBufferSize.emplace(group, 0);
    }
    else
    {
        groupBufferRanges[group] = range;
    }

    CopyToScratchpad(group, dev, cmd_list, queue);
}

void ConstantHandler::CopyToScratchpad(const ToggleGroup* group, device* dev, command_list* cmd_list, command_queue* queue)
{
    resource bufferResource = { 0 };
    buffer_range currentBufferRange = groupBufferRanges[group];
    resource_desc targetBufferDesc = dev->get_resource_desc(currentBufferRange.buffer);

    if (CreateScratchpad(group, dev, targetBufferDesc))
    {
        bufferResource = groupBufferResourceScratchpad[group];
        resource_desc desc = dev->get_resource_desc(bufferResource);

        if (desc.buffer.size != targetBufferDesc.buffer.size)
        {
            DestroyScratchpad(group, dev, queue);

            if (!CreateScratchpad(group, dev, targetBufferDesc))
            {
                return;
            }

            bufferResource = groupBufferResourceScratchpad[group];
        }
    }
    else
    {
        return;
    }

    vector<uint8_t>& bufferContent = groupBufferContent[group];
    vector<uint8_t>& prevBufferContent = groupPrevBufferContent[group];

    cmd_list->copy_resource(currentBufferRange.buffer, bufferResource);

    void* mapped_buffer;
    uint64_t size = targetBufferDesc.buffer.size;
    if (dev->map_buffer_region(bufferResource, currentBufferRange.offset, currentBufferRange.size, map_access::read_only, &mapped_buffer))
    {
        std::memcpy(prevBufferContent.data(), bufferContent.data(), size);
        std::memcpy(bufferContent.data(), mapped_buffer, size);

        dev->unmap_buffer_region(bufferResource);
    }
}

void ConstantHandler::RemoveGroup(const ToggleGroup* group, device* dev, command_queue* queue)
{
    if (!groupBufferContent.contains(group))
    {
        return;
    }

    DestroyScratchpad(group, dev, queue);

    groupBufferRanges.erase(group);
    groupBufferResourceScratchpad.erase(group);
    groupBufferContent.erase(group);
    groupPrevBufferContent.erase(group);
}

void ConstantHandler::DestroyScratchpad(const ToggleGroup* group, device* dev, command_queue* queue)
{
    if (dev == nullptr || queue == nullptr || !groupBufferResourceScratchpad.contains(group))
    {
        return;
    }

    resource res = groupBufferResourceScratchpad[group];

    if (res != 0)
    {
        queue->wait_idle();
        dev->destroy_resource(res);
        groupBufferResourceScratchpad[group] = { 0 };
        groupBufferContent[group].clear();
        groupPrevBufferContent[group].clear();
    }
}

bool ConstantHandler::CreateScratchpad(const ToggleGroup* group, device* dev, resource_desc& targetBufferDesc)
{
    resource bufferResource = groupBufferResourceScratchpad[group];

    if (bufferResource == 0)
    {
        memory_heap heap = targetBufferDesc.heap == memory_heap::cpu_only ? memory_heap::cpu_only : memory_heap::gpu_to_cpu;

        if (!dev->create_resource(
            resource_desc(targetBufferDesc.buffer.size, heap, resource_usage::copy_dest | resource_usage::copy_source),
            nullptr, resource_usage::copy_dest, &bufferResource))
        {
            return false;
        }

        groupBufferSize[group] = targetBufferDesc.buffer.size;
        groupBufferResourceScratchpad[group] = bufferResource;
        if (groupBufferContent.contains(group))
        {
            groupBufferContent[group].resize(targetBufferDesc.buffer.size, 0);
            groupPrevBufferContent[group].resize(targetBufferDesc.buffer.size, 0);
        }
        else
        {
            groupBufferContent.emplace(group, vector<uint8_t>(targetBufferDesc.buffer.size, 0));
            groupPrevBufferContent.emplace(group, vector<uint8_t>(targetBufferDesc.buffer.size, 0));
        }
        return true;
    }

    return true;
}