#include <cstring>
#include "ConstantHandlerBase.h"

using namespace ConstantFeedback;

ConstantHandlerBase::ConstantHandlerBase()
{
}

ConstantHandlerBase::~ConstantHandlerBase()
{
}

size_t ConstantHandlerBase::GetConstantBufferSize(const ToggleGroup* group)
{
	if (groupBufferSize.contains(group))
	{
		return groupBufferSize[group];
	}

	return 0;
}

uint8_t* ConstantHandlerBase::GetConstantBuffer(const ToggleGroup* group)
{
	if (groupBufferContent.contains(group))
	{
		return groupBufferContent[group].data();
	}

	return nullptr;
}

void ConstantHandlerBase::ApplyConstantValues(effect_runtime* runtime, const ToggleGroup* group,
	unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>>& constants)
{
	if (!groupBufferContent.contains(group) || runtime == nullptr)
	{
		return;
	}

	const uint8_t* buffer = groupBufferContent[group].data();
	const uint8_t* prevBuffer = groupPrevBufferContent[group].data();

	for (const auto& vars : group->GetVarOffsetMapping())
	{
		string var = vars.first;
		uintptr_t offset = get<0>(vars.second);
		bool prevValue = get<1>(vars.second);

		const uint8_t* bufferInUse = prevValue ? prevBuffer : buffer;

		if (!constants.contains(var))
		{
			continue;
		}

		constant_type type = std::get<0>(constants[var]);
		uint32_t typeIndex = static_cast<uint32_t>(type);

		if (offset + type_size[typeIndex] * type_length[typeIndex] >= groupBufferSize[group])
		{
			continue;
		}

		const vector<effect_uniform_variable>& effect_variables = std::get<1>(constants[var]);

		for (const auto& effect_var : effect_variables)
		{
			if (type <= constant_type::type_float4x4)
			{
				runtime->set_uniform_value_float(effect_var, reinterpret_cast<const float*>(bufferInUse + offset), type_length[typeIndex], 0);
			}
			else if (type == constant_type::type_int)
			{
				runtime->set_uniform_value_int(effect_var, reinterpret_cast<const int32_t*>(bufferInUse + offset), type_length[typeIndex], 0);
			}
			else
			{
				runtime->set_uniform_value_uint(effect_var, reinterpret_cast<const uint32_t*>(bufferInUse + offset), type_length[typeIndex], 0);
			}
		}
	}
}
