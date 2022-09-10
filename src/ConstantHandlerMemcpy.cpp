#include <cstring>
#include <MinHook.h>
#include "ConstantHandlerMemcpy.h"

using namespace ConstantFeedback;

ConstantHandlerMemcpy::ConstantHandlerMemcpy()
{
}

ConstantHandlerMemcpy::~ConstantHandlerMemcpy()
{
}

void ConstantHandlerMemcpy::SetBufferRange(const ToggleGroup* group, buffer_range range, device* dev, command_list* cmd_list, command_queue* queue)
{
    if (dev == nullptr || cmd_list == nullptr || range.buffer == 0)
    {
        return;
    }

    if (!groupBufferContent.contains(group))
    {
        groupBufferRanges.emplace(group, range);
        groupBufferSize.emplace(group, 0);
    }
    else
    {
        groupBufferRanges[group] = range;
    }

    CopyToScratchpad(group, dev, cmd_list, queue);
}

void ConstantHandlerMemcpy::CopyToScratchpad(const ToggleGroup* group, device* dev, command_list* cmd_list, command_queue* queue)
{
    buffer_range currentBufferRange = groupBufferRanges[group];
    resource_desc targetBufferDesc = dev->get_resource_desc(currentBufferRange.buffer);

    CreateScratchpad(group, dev, targetBufferDesc);

    if (!deviceToHostConstantBuffer.contains(currentBufferRange.buffer.handle))
    {
        return;
    }

    buffersOfInterest.insert(currentBufferRange.buffer.handle);

    vector<uint8_t>& bufferContent = groupBufferContent[group];
    vector<uint8_t>& prevBufferContent = groupPrevBufferContent[group];

    uint64_t size = targetBufferDesc.buffer.size;
    std::memcpy(prevBufferContent.data(), bufferContent.data(), size);
    std::memcpy(bufferContent.data(), deviceToHostConstantBuffer[currentBufferRange.buffer.handle].data(), size);
}

bool ConstantHandlerMemcpy::CreateScratchpad(const ToggleGroup* group, device* dev, resource_desc& targetBufferDesc)
{
    groupBufferSize[group] = targetBufferDesc.buffer.size;

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

void ConstantHandlerMemcpy::RemoveGroup(const ToggleGroup* group, device* dev, command_queue* queue)
{
    if (!groupBufferContent.contains(group))
    {
        return;
    }

    groupBufferRanges.erase(group);
    groupBufferContent.erase(group);
    groupPrevBufferContent.erase(group);
}

uint8_t* ConstantHandlerMemcpy::GetHostConstantBuffer(uint64_t resourceHandle)
{
    if (deviceToHostConstantBuffer.contains(resourceHandle))
    {
        return deviceToHostConstantBuffer[resourceHandle].data();
    }

    return nullptr;
}

void ConstantHandlerMemcpy::CreateHostConstantBuffer(device* dev, resource resource)
{
    resource_desc targetBufferDesc = dev->get_resource_desc(resource);

    deviceToHostConstantBuffer.emplace(resource.handle, vector<uint8_t>(targetBufferDesc.buffer.size, 0));
}

void ConstantHandlerMemcpy::DeleteHostConstantBuffer(resource resource)
{
    deviceToHostConstantBuffer.erase(resource.handle);
}

bool ConstantHandlerMemcpy::IsBufferOfInterest(uint64_t handle)
{
    return buffersOfInterest.contains(handle);
}

string ConstantHandlerMemcpy::GetExecutableName()
{
    char fileName[MAX_PATH + 1];
    DWORD charsWritten = GetModuleFileNameA(NULL, fileName, MAX_PATH + 1);
    if (charsWritten != 0)
    {
        string ret(fileName);
        std::size_t found = ret.find_last_of("/\\");
        return ret.substr(found + 1);
    }

    return string();
}

template<typename T>
static T* ConstantHandlerMemcpy::InstallHook(void* target, T* callback)
{
    void* original_function = nullptr;

    if (MH_CreateHook(target, reinterpret_cast<void*>(callback), &original_function) != MH_OK)
        return nullptr;

    return reinterpret_cast<T*>(original_function);
}

template<typename T>
static T* ConstantHandlerMemcpy::InstallApiHook(LPCWSTR pszModule, LPCSTR pszProcName, T* callback)
{
    void* original_function = nullptr;

    if (MH_CreateHookApi(pszModule, pszProcName, reinterpret_cast<void*>(callback), &original_function) != MH_OK)
        return nullptr;

    return reinterpret_cast<T*>(original_function);
}

bool ConstantHandlerMemcpy::HookStatic(sig_memcpy** original, sig_memcpy* detour)
{
    string exe = GetExecutableName();
    if (exe.length() == 0)
        return false;

    for (const auto& sig : memcpy_static)
    {
        sigmatch::this_process_target target;
        sigmatch::search_result result = target.in_module(exe).search(sig);

        for (const std::byte* address : result.matches()) {
            void* adr = static_cast<void*>(const_cast<std::byte*>(address));
            *original = InstallHook<sig_memcpy>(adr, detour);

            // Assume signature is unique
            if (*original != nullptr)
            {
                return true;
            }
        }
    }

    return false;
}

bool ConstantHandlerMemcpy::HookDynamic(sig_memcpy** original, sig_memcpy* detour)
{
    for (const auto& libFunc : memcpy_dynamic)
    {
        *original = InstallApiHook(get<0>(libFunc).c_str(), get<1>(libFunc).c_str(), detour);

        // Pick first hit and hope for the best
        if (*original != nullptr)
        {
            return true;
        }
    }

    return false;
}

bool ConstantHandlerMemcpy::Hook(sig_memcpy** original, sig_memcpy* detour)
{
    // Try hooking statically linked memcpy first, then look into dynamically linked ones
    if (HookStatic(original, detour) || HookDynamic(original, detour))
    {
        return !(MH_EnableHook(MH_ALL_HOOKS) != MH_OK);
    }

    return false;
}

bool ConstantHandlerMemcpy::Unhook()
{
    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK)
    {
        return false;
    }

    return true;
}