// Copyright ProjectLogos

#include "GAS/ASC/PL_AbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"

UPL_AbilitySystemComponent::UPL_AbilitySystemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UPL_AbilitySystemComponent::PlayMontage(
	UGameplayAbility* AnimatingAbility,
	FGameplayAbilityActivationInfo ActivationInfo,
	UAnimMontage* Montage,
	float InPlayRate,
	FName StartSectionName,
	float StartTimeSeconds)
{
	return Super::PlayMontage(
		AnimatingAbility,
		ActivationInfo,
		Montage,
		InPlayRate,
		StartSectionName,
		StartTimeSeconds);
}

void UPL_AbilitySystemComponent::OnRep_ReplicatedAnimMontage()
{
	Super::OnRep_ReplicatedAnimMontage();
}
