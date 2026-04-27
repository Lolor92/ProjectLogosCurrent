// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PL_CombatFunctionLibrary.generated.h"

class UAbilitySystemComponent;

UCLASS()
class PROJECTLOGOS_API UPL_CombatFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Supports character-owned ASC first, then PlayerState-owned ASC.
	UFUNCTION(BlueprintCallable, Category="Combat|Ability")
	static UAbilitySystemComponent* GetAbilitySystemComponent(AActor* Actor);
};
