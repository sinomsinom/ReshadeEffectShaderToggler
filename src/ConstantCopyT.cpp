#include "ConstantCopyT.h"

using namespace ConstantFeedback;

template<typename T>
ConstantCopyT<T>::ConstantCopyT()
{

}

template<typename T>
ConstantCopyT<T>::~ConstantCopyT()
{

}

template<typename T>
T* ConstantCopyT<T>::InstallHook(void* target, T* callback)
{
    void* original_function = nullptr;

    if (MH_CreateHook(target, reinterpret_cast<void*>(callback), &original_function) != MH_OK)
        return nullptr;

    return reinterpret_cast<T*>(original_function);
}

template<typename T>
T* ConstantCopyT<T>::InstallApiHook(LPCWSTR pszModule, LPCSTR pszProcName, T* callback)
{
    void* original_function = nullptr;

    if (MH_CreateHookApi(pszModule, pszProcName, reinterpret_cast<void*>(callback), &original_function) != MH_OK)
        return nullptr;

    return reinterpret_cast<T*>(original_function);
}

template<typename T>
bool ConstantCopyT<T>::Hook(T** original, T* detour, const sigmatch::signature& sig)
{
    string exe = GetExecutableName();
    if (exe.length() == 0)
        return false;

    sigmatch::this_process_target target;
    sigmatch::search_result result = target.in_module(exe).search(sig);

    for (const std::byte* address : result.matches()) {
        void* adr = static_cast<void*>(const_cast<std::byte*>(address));
        *original = InstallHook(adr, detour);
    }

    if (original != nullptr)
        return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;

    return false;
}

template<typename T>
bool ConstantCopyT<T>::Unhook()
{
    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK)
    {
        return false;
    }

    return true;
}

template class ConstantCopyT<sig_memcpy>;
template class ConstantCopyT<sig_ffxiv_cbload>;
template class ConstantCopyT<sig_nier_replicant_cbload>;