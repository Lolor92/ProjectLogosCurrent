// Copyright ProjectLogos

#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Combat/Utilities/PL_CombatFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Tag/PL_NativeTags.h"
#include "UObject/UnrealType.h"

FPLLocalHitFeedbackRuntime::FPLLocalHitFeedbackRuntime(UPL_CombatComponent& InCombatComponent)
	: CombatComponent(InCombatComponent)
{
}

void FPLLocalHitFeedbackRuntime::PlayPredictedHitFeedback(
	AActor* HitActor,
	const FHitResult& HitResult,
	const FPLHitWindowSettings& HitWindowSettings)
{
	if (!HitActor) return;

	APL_BaseCharacter* OwnerCharacter = CombatComponent.GetOwningCharacter();
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled()) return;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	PruneOldEntries();
	if (WasRecentlyPredictedHit(HitActor)) return;

	const float Now = World->GetTimeSeconds();

	FPLLocalHitFeedbackEntry& Entry = RecentPredictedHits.AddDefaulted_GetRef();
	Entry.TargetActor = HitActor;
	Entry.TimeSeconds = Now;

	PlayPredictedReactionMontage(HitActor, HitWindowSettings);

	for (const FPLHitWindowGameplayCue& Cue : HitWindowSettings.GameplayCuesToExecute)
	{
		if (Cue.TriggerTiming != EPLHitWindowCueTriggerTiming::OnHit) continue;
		if (!Cue.CueTag.MatchesTag(TAG_GameplayCue_Hit_CameraShake)) continue;

		ExecuteLocalCameraShakeCue(Cue, HitResult);
	}

	if (HitWindowSettings.HitStopSettings.IsEnabled())
	{
		OwnerCharacter->ApplyHitStop(
			HitWindowSettings.HitStopSettings.Duration,
			HitWindowSettings.HitStopSettings.TimeScale);
	}
}

void FPLLocalHitFeedbackRuntime::PlayPredictedReactionMontage(
	AActor* HitActor,
	const FPLHitWindowSettings& HitWindowSettings) const
{
	if (!HitActor) return;

	APL_BaseCharacter* TargetCharacter = Cast<APL_BaseCharacter>(HitActor);
	if (!TargetCharacter) return;

	UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC) return;

	for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : HitWindowSettings.GameplayEffectsToApply)
	{
		if (!GameplayEffectToApply.GameplayEffectClass) continue;

		FGameplayTag TriggerTag;
		if (!FindTriggerTagFromGameplayEffect(GameplayEffectToApply.GameplayEffectClass, TriggerTag)) continue;

		UE_LOG(LogTemp, Warning, TEXT("Predict1"));

		FGameplayTag AbilityTag;
		if (!CombatComponent.FindReactionAbilityTag(TriggerTag, AbilityTag)) continue;

		UE_LOG(LogTemp, Warning, TEXT("Predict2"));

		UAnimMontage* MontageToPlay = FindMontageFromAbilityTag(TargetASC, AbilityTag);
		if (!MontageToPlay) continue;

		UE_LOG(LogTemp, Warning, TEXT("Predict3"));

		USkeletalMeshComponent* Mesh = TargetCharacter->GetMesh();
		if (!Mesh) return;

		UE_LOG(LogTemp, Warning, TEXT("Predict4"));

		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		if (!AnimInstance) return;

		UE_LOG(LogTemp, Warning, TEXT("Predict5"));

		if (AnimInstance->Montage_IsPlaying(MontageToPlay)) return;

		const float MontageLength = AnimInstance->Montage_Play(MontageToPlay, 1.f);

		UE_LOG(LogTemp, Warning, TEXT("Predicted reaction montage played. Target=%s GE=%s TriggerTag=%s AbilityTag=%s Montage=%s Length=%.3f"),
			*GetNameSafe(TargetCharacter),
			*GetNameSafe(GameplayEffectToApply.GameplayEffectClass),
			*TriggerTag.ToString(),
			*AbilityTag.ToString(),
			*GetNameSafe(MontageToPlay),
			MontageLength);

		return;
	}
}

bool FPLLocalHitFeedbackRuntime::FindTriggerTagFromGameplayEffect(
	TSubclassOf<UGameplayEffect> GameplayEffectClass,
	FGameplayTag& OutTriggerTag) const
{
	OutTriggerTag = FGameplayTag();

	if (!GameplayEffectClass) return false;

	const UGameplayEffect* GameplayEffectCDO = GameplayEffectClass->GetDefaultObject<UGameplayEffect>();
	if (!GameplayEffectCDO) return false;

	const FGameplayTagContainer& GrantedTags = GameplayEffectCDO->GetGrantedTags();

	for (const FGameplayTag& Tag : GrantedTags)
	{
		if (!Tag.IsValid()) continue;

		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Trigger"))))
		{
			OutTriggerTag = Tag;
			return true;
		}
	}

	return false;
}

UAnimMontage* FPLLocalHitFeedbackRuntime::FindMontageFromAbilityTag(
	UAbilitySystemComponent* ASC,
	const FGameplayTag& AbilityTag) const
{
	if (!ASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("FindMontageFromAbilityTag failed: ASC is null."));
		return nullptr;
	}

	if (!AbilityTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FindMontageFromAbilityTag failed: AbilityTag is invalid."));
		return nullptr;
	}

	UE_LOG(LogTemp, Warning, TEXT("FindMontageFromAbilityTag: ASC Owner=%s Avatar=%s AbilityTag=%s"),
		*GetNameSafe(ASC->GetOwnerActor()),
		*GetNameSafe(ASC->GetAvatarActor()),
		*AbilityTag.ToString());

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);

	TArray<FGameplayAbilitySpec*> MatchingSpecs;
	ASC->GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTags,
		MatchingSpecs,
		false);

	UE_LOG(LogTemp, Warning, TEXT("FindMontageFromAbilityTag: MatchingSpecs count=%d"),
		MatchingSpecs.Num());

	if (MatchingSpecs.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No ability matched tag: %s. Listing granted abilities on ASC:"),
			*AbilityTag.ToString());

		const TArray<FGameplayAbilitySpec>& ActivatableAbilities = ASC->GetActivatableAbilities();

		for (const FGameplayAbilitySpec& Spec : ActivatableAbilities)
		{
			const UGameplayAbility* Ability = Spec.Ability;
			if (!Ability) continue;

			FGameplayTagContainer AbilityOwnedTags;
			Ability->GetAssetTags(AbilityOwnedTags);

			UE_LOG(LogTemp, Warning, TEXT("Granted Ability: %s | Tags: %s"),
				*GetNameSafe(Ability),
				*AbilityOwnedTags.ToString());
		}

		return nullptr;
	}

	for (const FGameplayAbilitySpec* Spec : MatchingSpecs)
	{
		if (!Spec)
		{
			UE_LOG(LogTemp, Warning, TEXT("Skipping null ability spec."));
			continue;
		}

		const UGameplayAbility* AbilityCDO = Spec->Ability;
		if (!AbilityCDO)
		{
			UE_LOG(LogTemp, Warning, TEXT("Skipping spec with null ability."));
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("Checking ability: %s | Class: %s"),
			*GetNameSafe(AbilityCDO),
			*GetNameSafe(AbilityCDO->GetClass()));

		FGameplayTagContainer AbilityOwnedTags;
		AbilityCDO->GetAssetTags(AbilityOwnedTags);

		UE_LOG(LogTemp, Warning, TEXT("Ability tags: %s"),
			*AbilityOwnedTags.ToString());

		for (TFieldIterator<FProperty> PropertyIt(AbilityCDO->GetClass()); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property) continue;

			UE_LOG(LogTemp, Warning, TEXT("Ability property: %s | Type: %s"),
				*Property->GetName(),
				*Property->GetClass()->GetName());

			if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue_InContainer(AbilityCDO);

				UE_LOG(LogTemp, Warning, TEXT("  ObjectProperty value: %s | Class: %s"),
					*GetNameSafe(ObjectValue),
					ObjectValue ? *GetNameSafe(ObjectValue->GetClass()) : TEXT("None"));

				if (UAnimMontage* Montage = Cast<UAnimMontage>(ObjectValue))
				{
					UE_LOG(LogTemp, Warning, TEXT("Found montage %s on ability %s from object property %s."),
						*GetNameSafe(Montage),
						*GetNameSafe(AbilityCDO),
						*ObjectProperty->GetName());

					return Montage;
				}
			}

			if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
			{
				const FSoftObjectPtr SoftObjectValue = SoftObjectProperty->GetPropertyValue_InContainer(AbilityCDO);

				UE_LOG(LogTemp, Warning, TEXT("  SoftObjectProperty value: %s"),
					*SoftObjectValue.ToString());

				UObject* LoadedObject = SoftObjectValue.LoadSynchronous();

				if (UAnimMontage* Montage = Cast<UAnimMontage>(LoadedObject))
				{
					UE_LOG(LogTemp, Warning, TEXT("Found montage %s on ability %s from soft object property %s."),
						*GetNameSafe(Montage),
						*GetNameSafe(AbilityCDO),
						*SoftObjectProperty->GetName());

					return Montage;
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FindMontageFromAbilityTag failed: ability matched tag %s, but no montage property was found."),
		*AbilityTag.ToString());

	return nullptr;
}

bool FPLLocalHitFeedbackRuntime::WasRecentlyPredictedHit(AActor* HitActor) const
{
	if (!HitActor) return false;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return false;

	const float Now = World->GetTimeSeconds();

	for (const FPLLocalHitFeedbackEntry& Entry : RecentPredictedHits)
	{
		if (Entry.TargetActor == HitActor && Now - Entry.TimeSeconds <= DuplicateSuppressionTime)
		{
			return true;
		}
	}

	return false;
}

void FPLLocalHitFeedbackRuntime::PruneOldEntries()
{
	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	RecentPredictedHits.RemoveAll([this, Now](const FPLLocalHitFeedbackEntry& Entry)
	{
		return !Entry.TargetActor.IsValid() || Now - Entry.TimeSeconds > DuplicateSuppressionTime;
	});
}

void FPLLocalHitFeedbackRuntime::ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	if (!ShouldExecuteLocalCameraShakeCue()) return;

	UAbilitySystemComponent* AbilitySystemComponent = CombatComponent.GetAbilitySystemComponent();
	if (!AbilitySystemComponent) return;

	AActor* const OwnerActor = CombatComponent.GetOwner();

	FGameplayCueParameters Params;
	Params.Instigator = OwnerActor;
	Params.EffectCauser = OwnerActor;
	Params.SourceObject = &CombatComponent;
	Params.Location = GetCueSpawnLocation(Cue, HitResult);
	Params.Normal = FVector(HitResult.ImpactNormal);
	Params.TargetAttachComponent = Cue.bAttachToTarget ? HitResult.GetComponent() : nullptr;

	AbilitySystemComponent->InvokeGameplayCueEvent(Cue.CueTag, EGameplayCueEvent::Executed, Params);
}

bool FPLLocalHitFeedbackRuntime::ShouldExecuteLocalCameraShakeCue() const
{
	if (CombatComponent.GetNetMode() == NM_DedicatedServer) return false;

	const UAbilitySystemComponent* AbilitySystemComponent = CombatComponent.GetAbilitySystemComponent();
	if (!AbilitySystemComponent) return false;

	const AActor* LocalCueActor = AbilitySystemComponent->GetAvatarActor_Direct();
	if (!LocalCueActor)
	{
		LocalCueActor = AbilitySystemComponent->GetOwnerActor();
	}

	const APawn* LocalCuePawn = Cast<APawn>(LocalCueActor);
	return LocalCuePawn && LocalCuePawn->IsLocallyControlled();
}

FVector FPLLocalHitFeedbackRuntime::GetCueSpawnLocation(const FPLHitWindowGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	AActor* const OwnerActor = CombatComponent.GetOwner();
	FVector SpawnLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;

	switch (Cue.SpawnPoint)
	{
	case EPLHitWindowCueSpawnPoint::OwnerLocation:
		SpawnLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
		break;

	case EPLHitWindowCueSpawnPoint::HitImpactPoint:
		SpawnLocation = HitResult.ImpactPoint;
		break;

	case EPLHitWindowCueSpawnPoint::HitLocation:
		SpawnLocation = HitResult.Location;
		break;

	default:
		break;
	}

	return SpawnLocation + Cue.LocationOffset;
}
