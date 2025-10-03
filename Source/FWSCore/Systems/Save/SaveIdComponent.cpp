#include "SaveIdComponent.h"

USaveIdComponent::USaveIdComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void USaveIdComponent::OnRegister()
{
	Super::OnRegister();
	if (!SaveGuid.IsValid())
	{
		SaveGuid = FGuid::NewGuid();
		// No disk write here; persistence happens via SaveSystem
	}
}

FGuid USaveIdComponent::GetOrCreateGuid()
{
	if (!SaveGuid.IsValid())
	{
		SaveGuid = FGuid::NewGuid();
	}
	return SaveGuid;
}
