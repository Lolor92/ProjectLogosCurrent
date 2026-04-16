// Copyright ProjectLogos

#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"

#include "AbilitySystemComponent.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GameFramework/Pawn.h"
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
