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
	UE_LOG(LogTemp, Warning,
		TEXT("ASC PlayMontage called. Avatar=%s HasAuthority=%s IsLocallyControlled=%s Ability=%s Montage=%s"),
		*GetNameSafe(GetAvatarActor_Direct()),
		GetAvatarActor_Direct() && GetAvatarActor_Direct()->HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"),
		Cast<APawn>(GetAvatarActor_Direct()) && Cast<APawn>(GetAvatarActor_Direct())->IsLocallyControlled() ? TEXT("TRUE") : TEXT("FALSE"),
		*GetNameSafe(AnimatingAbility),
		*GetNameSafe(Montage));

	if (ShouldSuppressPredictedReactionMontageReplay(Montage))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ASC suppressed duplicate predicted reaction montage replay from PlayMontage. Avatar=%s Montage=%s"),
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

	UE_LOG(LogTemp, Warning,
		TEXT("ASC OnRep_ReplicatedAnimMontage called. Avatar=%s HasAuthority=%s IsLocallyControlled=%s Montage=%s"),
		*GetNameSafe(GetAvatarActor_Direct()),
		GetAvatarActor_Direct() && GetAvatarActor_Direct()->HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"),
		Cast<APawn>(GetAvatarActor_Direct()) && Cast<APawn>(GetAvatarActor_Direct())->IsLocallyControlled() ? TEXT("TRUE") : TEXT("FALSE"),
		*GetNameSafe(ReplicatedMontage));

	if (ShouldSuppressPredictedReactionMontageReplay(ReplicatedMontage))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ASC suppressed duplicate replicated reaction montage from OnRep. Avatar=%s Montage=%s"),
			*GetNameSafe(GetAvatarActor_Direct()),
			*GetNameSafe(ReplicatedMontage));

		return;
	}

	Super::OnRep_ReplicatedAnimMontage();
}

bool UPL_AbilitySystemComponent::ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage) const
{
	if (!Montage)
	{
		UE_LOG(LogTemp, Warning, TEXT("Suppress check failed: Montage is null."));
		return false;
	}

	AActor* AvatarActorInstance = GetAvatarActor_Direct();
	if (!AvatarActorInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Suppress check failed: AvatarActor is null. Montage=%s"),
			*GetNameSafe(Montage));
		return false;
	}

	if (AvatarActorInstance->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("Suppress check failed: Avatar has authority. Avatar=%s Montage=%s"),
			*GetNameSafe(AvatarActorInstance),
			*GetNameSafe(Montage));
		return false;
	}

	const APawn* AvatarPawn = Cast<APawn>(AvatarActorInstance);
	if (AvatarPawn && AvatarPawn->IsLocallyControlled())
	{
		UE_LOG(LogTemp, Warning, TEXT("Suppress check failed: Avatar is locally controlled. Avatar=%s Montage=%s"),
			*GetNameSafe(AvatarActorInstance),
			*GetNameSafe(Montage));
		return false;
	}

	APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(AvatarActorInstance);
	if (!Character)
	{
		UE_LOG(LogTemp, Warning, TEXT("Suppress check failed: Avatar is not APL_BaseCharacter. Avatar=%s Montage=%s"),
			*GetNameSafe(AvatarActorInstance),
			*GetNameSafe(Montage));
		return false;
	}

	UPL_CombatComponent* CombatComponent = Character->GetCombatComponent();
	if (!CombatComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("Suppress check failed: CombatComponent is null. Avatar=%s Montage=%s"),
			*GetNameSafe(AvatarActorInstance),
			*GetNameSafe(Montage));
		return false;
	}

	const bool bShouldSuppress = CombatComponent->GetLocalHitFeedbackRuntime()
		.ShouldSuppressPredictedReactionMontageReplay(Montage);

	UE_LOG(LogTemp, Warning, TEXT("Suppress check result. Avatar=%s Montage=%s Result=%s"),
		*GetNameSafe(AvatarActorInstance),
		*GetNameSafe(Montage),
		bShouldSuppress ? TEXT("TRUE") : TEXT("FALSE"));

	return bShouldSuppress;
}
