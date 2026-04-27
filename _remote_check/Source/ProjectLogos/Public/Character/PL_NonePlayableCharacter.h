// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "PL_BaseCharacter.h"
#include "PL_NonePlayableCharacter.generated.h"

UCLASS()
class PROJECTLOGOS_API APL_NonePlayableCharacter : public APL_BaseCharacter
{
	GENERATED_BODY()

public:
	APL_NonePlayableCharacter();
	
protected:
	// NPC-owned ASC setup.
	virtual void BeginPlay() override;
	
	virtual void InitAbilityActorInfo();
};
