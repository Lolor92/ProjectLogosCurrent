// Copyright ProjectLogos

#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Combat/Data/PL_TagReactionData.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Tag/PL_NativeTags.h"

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

	APL_BaseCharacter* OwnerCharacter = CombatComponent.GetOwningCharacter();
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled()) return;

	APL_BaseCharacter* TargetCharacter = Cast<APL_BaseCharacter>(HitActor);
	if (!TargetCharacter) return;

	for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : HitWindowSettings.GameplayEffectsToApply)
	{
		if (!GameplayEffectToApply.GameplayEffectClass) continue;

		FGameplayTag TriggerTag;
		if (!FindTriggerTagFromGameplayEffectClass(GameplayEffectToApply.GameplayEffectClass, TriggerTag)) continue;

		const FPL_TagReactionBinding* ReactionBinding = CombatComponent.FindTagReactionBindingForTriggerTag(TriggerTag);
		if (!ReactionBinding) continue;

		UAnimMontage* MontageToPlay = ReactionBinding->Ability.PredictedReactionMontage;
		if (!MontageToPlay) continue;

		UE_LOG(LogTemp, Warning,
			TEXT("Predicted reaction montage found but one-mesh prediction path is disabled for cleanup. Attacker=%s Target=%s GE=%s TriggerTag=%s Montage=%s"),
			*GetNameSafe(OwnerCharacter),
			*GetNameSafe(TargetCharacter),
			*GetNameSafe(GameplayEffectToApply.GameplayEffectClass),
			*TriggerTag.ToString(),
			*GetNameSafe(MontageToPlay));

		return;
	}
}

void FPLLocalHitFeedbackRuntime::RegisterPredictedReactionMontage(UAnimMontage* Montage)
{
	if (!Montage) return;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	RecentPredictedReactionMontages.RemoveAll(
		[Montage](const FPLPredictedReactionMontageEntry& Entry)
		{
			return Entry.Montage.Get() == Montage;
		});

	FPLPredictedReactionMontageEntry& Entry = RecentPredictedReactionMontages.AddDefaulted_GetRef();
	Entry.Montage = Montage;
	Entry.TimeSeconds = Now;
	Entry.bHasLoggedSuppression = false;

	UE_LOG(LogTemp, Warning, TEXT("Registered predicted reaction montage receipt. Owner=%s Montage=%s Time=%.3f"),
		*GetNameSafe(CombatComponent.GetOwner()),
		*GetNameSafe(Montage),
		Now);
}

bool FPLLocalHitFeedbackRuntime::ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage)
{
	if (!Montage) return false;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return false;

	PruneOldReactionMontageEntries();

	const float Now = World->GetTimeSeconds();

	for (FPLPredictedReactionMontageEntry& Entry : RecentPredictedReactionMontages)
	{
		if (Entry.Montage.Get() != Montage) continue;

		const float ElapsedSincePredictedPlaySeconds = Now - Entry.TimeSeconds;
		if (ElapsedSincePredictedPlaySeconds <= 0.f) return false;

		if (!Entry.bHasLoggedSuppression)
		{
			Entry.bHasLoggedSuppression = true;

			UE_LOG(LogTemp, Warning,
				TEXT("Suppressing duplicate server reaction montage before replay. Owner=%s Montage=%s Elapsed=%.3f"),
				*GetNameSafe(CombatComponent.GetOwner()),
				*GetNameSafe(Montage),
				ElapsedSincePredictedPlaySeconds);
		}

		return true;
	}

	return false;
}

bool FPLLocalHitFeedbackRuntime::FindTriggerTagFromGameplayEffectClass(
	TSubclassOf<UGameplayEffect> GameplayEffectClass,
	FGameplayTag& OutTriggerTag) const
{
	OutTriggerTag = FGameplayTag();

	if (!GameplayEffectClass) return false;

	const UGameplayEffect* GameplayEffectCDO = GameplayEffectClass->GetDefaultObject<UGameplayEffect>();
	if (!GameplayEffectCDO) return false;

	const FGameplayTagContainer& GrantedTags = GameplayEffectCDO->GetGrantedTags();
	const FGameplayTag TriggerRootTag = FGameplayTag::RequestGameplayTag(TEXT("Trigger"), false);

	for (const FGameplayTag& GrantedTag : GrantedTags)
	{
		if (!GrantedTag.IsValid()) continue;

		if (TriggerRootTag.IsValid() && GrantedTag.MatchesTag(TriggerRootTag))
		{
			OutTriggerTag = GrantedTag;
			return true;
		}

		if (GrantedTag.ToString().StartsWith(TEXT("Trigger.")))
		{
			OutTriggerTag = GrantedTag;
			return true;
		}
	}

	return false;
}

void FPLLocalHitFeedbackRuntime::PruneOldReactionMontageEntries()
{
	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	RecentPredictedReactionMontages.RemoveAll(
		[this, Now](const FPLPredictedReactionMontageEntry& Entry)
		{
			return !Entry.Montage.IsValid() ||
				Now - Entry.TimeSeconds > PredictedReactionReplaySuppressionTime;
		});
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

	PruneOldReactionMontageEntries();
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
