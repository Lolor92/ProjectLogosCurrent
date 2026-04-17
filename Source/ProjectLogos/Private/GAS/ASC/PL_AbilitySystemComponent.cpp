// Copyright ProjectLogos

#include "GAS/ASC/PL_AbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GameFramework/Pawn.h"

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
	if (ShouldSuppressPredictedReactionMontageReplay(Montage))
	{
		UE_LOG(LogTemp, Warning, TEXT("ASC suppressed duplicate predicted reaction montage replay from PlayMontage. Avatar=%s Montage=%s"),
			*GetNameSafe(GetAvatarActor_Direct()),
			*GetNameSafe(Montage));

		return Montage ? Montage->GetPlayLength() : 0.f;
	}

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
	UAnimMontage* ReplicatedMontage = RepAnimMontageInfo.GetAnimMontage();

	if (ShouldSuppressPredictedReactionMontageReplay(ReplicatedMontage))
	{
		UE_LOG(LogTemp, Warning, TEXT("ASC suppressed duplicate replicated reaction montage from OnRep. Avatar=%s Montage=%s"),
			*GetNameSafe(GetAvatarActor_Direct()),
			*GetNameSafe(ReplicatedMontage));

		return;
	}

	Super::OnRep_ReplicatedAnimMontage();
}

bool UPL_AbilitySystemComponent::ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage) const
{
	if (!Montage) return false;

	AActor* AvatarActorInstance = GetAvatarActor_Direct();
	if (!AvatarActorInstance) return false;

	// Never suppress on the server.
	if (AvatarActorInstance->HasAuthority()) return false;

	// Never suppress the local player's own ability montages.
	// This suppression is only for remote target copies on the attacking client.
	const APawn* AvatarPawn = Cast<APawn>(AvatarActorInstance);
	if (AvatarPawn && AvatarPawn->IsLocallyControlled()) return false;

	APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(AvatarActorInstance);
	if (!Character) return false;

	UPL_CombatComponent* CombatComponent = Character->GetCombatComponent();
	if (!CombatComponent) return false;

	return CombatComponent->GetLocalHitFeedbackRuntime().ShouldSuppressPredictedReactionMontageReplay(Montage);
}
