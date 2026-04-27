// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"
#include "PL_PlayerState.generated.h"

class UAbilitySystemComponent;
class UAttributeSet;

UCLASS()
class PROJECTLOGOS_API APL_PlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()
	
public:
	APL_PlayerState();

	// Player-owned GAS state.
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystemComponent; }
	UAttributeSet* GetAttributeSet() const { return AttributeSet; }
	
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Ability")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Ability")
	TObjectPtr<UAttributeSet> AttributeSet;
};
