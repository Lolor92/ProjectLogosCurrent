#include "Input/PL_InputConfig.h"

const UInputAction* UPL_InputConfig::FindInputActionByTag(const FGameplayTag& InputTag) const
{
	for (const FPLInputAction& InputActionRow : InputActions)
	{
		if (InputActionRow.InputAction && InputActionRow.InputTag == InputTag)
		{
			return InputActionRow.InputAction;
		}
	}

	return nullptr;
}
