// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "PL_AbilitySystemComponent.generated.h"

class UAnimMontage;
class UGameplayAbility;

UCLASS()
class PROJECTLOGOS_API UPL_AbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	UPL_AbilitySystemComponent();

	virtual float PlayMontage(
		UGameplayAbility* AnimatingAbility,
		FGameplayAbilityActivationInfo ActivationInfo,
		UAnimMontage* Montage,
		float InPlayRate,
		FName StartSectionName = NAME_None,
		float StartTimeSeconds = 0.f) override;

protected:
	virtual void OnRep_ReplicatedAnimMontage() override;
};
