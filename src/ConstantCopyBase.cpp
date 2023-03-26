#include "ConstantCopyBase.h"

using namespace ConstantFeedback;
ConstantHandlerBase* ConstantCopyBase::_constHandler = nullptr;

ConstantCopyBase::ConstantCopyBase()
{

}

ConstantCopyBase::~ConstantCopyBase()
{

}

void ConstantCopyBase::SetConstantHandler(ConstantHandlerBase* constantHandler)
{
    _constHandler = constantHandler;
}

std::string ConstantCopyBase::GetExecutableName()
{
    char fileName[MAX_PATH + 1];
    DWORD charsWritten = GetModuleFileNameA(NULL, fileName, MAX_PATH + 1);
    if (charsWritten != 0)
    {
        std::string ret(fileName);
        std::size_t found = ret.find_last_of("/\\");
        return ret.substr(found + 1);
    }

    return std::string();
}