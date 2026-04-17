// Copyright ProjectLogos

#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"

#include "AbilitySystemComponent.h"
#include "AnimInstance/PL_AnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"
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

	PlayPredictedReactionMontage(HitActor, HitResult, HitWindowSettings);

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
	const FHitResult& HitResult,
	const FPLHitWindowSettings& HitWindowSettings)
{
	if (!HitActor) return;

	APL_BaseCharacter* OwnerCharacter = CombatComponent.GetOwningCharacter();
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled()) return;

	APL_BaseCharacter* TargetCharacter = Cast<APL_BaseCharacter>(HitActor);
	if (!TargetCharacter) return;

	USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
	if (!TargetMesh) return;

	UAnimInstance* TargetAnimInstance = TargetMesh->GetAnimInstance();
	if (!TargetAnimInstance) return;

	for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : HitWindowSettings.GameplayEffectsToApply)
	{
		if (!GameplayEffectToApply.GameplayEffectClass) continue;

		FGameplayTag TriggerTag;
		if (!FindTriggerTagFromGameplayEffectClass(GameplayEffectToApply.GameplayEffectClass, TriggerTag)) continue;

		const FPL_TagReactionBinding* ReactionBinding = CombatComponent.FindTagReactionBindingForTriggerTag(TriggerTag);
		if (!ReactionBinding) continue;

		const FPL_TagReactionAbility& ReactionAbility = ReactionBinding->Ability;

		UAnimMontage* MontageToPlay = ReactionAbility.PredictedReactionMontage;
		if (!MontageToPlay) continue;

		if (TargetAnimInstance->Montage_IsPlaying(MontageToPlay)) return;

		if (ReactionAbility.PredictedMovementMode == EPLPredictedReactionMovementMode::VisualRootMotionOffset)
		{
			StartPredictedReactionVisualOffset(
				TargetCharacter,
				MontageToPlay,
				ReactionAbility,
				HitResult);
		}

		const float MontageLength = TargetAnimInstance->Montage_Play(
			MontageToPlay,
			1.f,
			EMontagePlayReturnType::MontageLength,
			0.f,
			true);

		if (MontageLength <= 0.f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Predicted reaction montage failed to play. Attacker=%s Target=%s Montage=%s"),
				*GetNameSafe(OwnerCharacter),
				*GetNameSafe(TargetCharacter),
				*GetNameSafe(MontageToPlay));

			return;
		}

		if (UPL_CombatComponent* TargetCombatComponent = TargetCharacter->GetCombatComponent())
		{
			TargetCombatComponent->GetLocalHitFeedbackRuntime().RegisterPredictedReactionMontage(MontageToPlay);
		}

		UE_LOG(LogTemp, Warning,
			TEXT("Predicted reaction montage played. Attacker=%s Target=%s GE=%s TriggerTag=%s Montage=%s Length=%.3f MovementMode=%d"),
			*GetNameSafe(OwnerCharacter),
			*GetNameSafe(TargetCharacter),
			*GetNameSafe(GameplayEffectToApply.GameplayEffectClass),
			*TriggerTag.ToString(),
			*GetNameSafe(MontageToPlay),
			MontageLength,
			static_cast<int32>(ReactionAbility.PredictedMovementMode));

		return;
	}
}

void FPLLocalHitFeedbackRuntime::StartPredictedReactionVisualOffset(
	APL_BaseCharacter* TargetCharacter,
	UAnimMontage* Montage,
	const FPL_TagReactionAbility& ReactionAbility,
	const FHitResult& HitResult)
{
	if (!TargetCharacter || !Montage) return;

	USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
	if (!TargetMesh) return;

	UAnimInstance* TargetAnimInstance = TargetMesh->GetAnimInstance();
	if (!TargetAnimInstance) return;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const FVector Direction = GetPredictionDirection(TargetCharacter, HitResult);
	if (Direction.IsNearlyZero()) return;

	FPLPredictedReactionVisualEntry& Entry = PredictedReactionVisuals.AddDefaulted_GetRef();
	Entry.TargetCharacter = TargetCharacter;
	Entry.TargetMesh = TargetMesh;
	Entry.TargetAnimInstance = TargetAnimInstance;
	Entry.Montage = Montage;
	Entry.Direction = Direction;
	Entry.Distance = ReactionAbility.PredictedVisualRootMotionDistance;
	Entry.Duration = FMath::Max(0.01f, ReactionAbility.PredictedVisualRootMotionDuration);
	Entry.BlendOutTime = FMath::Max(0.01f, ReactionAbility.PredictedVisualBlendOutTime);
	Entry.StartTime = World->GetTimeSeconds();
	Entry.BlendOutStartTime = 0.f;
	Entry.LastAppliedOffset = FVector::ZeroVector;
	Entry.bBlendingOut = false;
	Entry.PreviousRootMotionMode = TargetAnimInstance->RootMotionMode;

	if (UPL_AnimInstance* PLAnimInstance = Cast<UPL_AnimInstance>(TargetAnimInstance))
	{
		Entry.bPreviousPLRootMotionEnabled = PLAnimInstance->bRootMotionEnabled;
		PLAnimInstance->bRootMotionEnabled = false;
	}

	// Important: predicted target knockback must not move the target capsule.
	TargetAnimInstance->RootMotionMode = ERootMotionMode::IgnoreRootMotion;

	// Local visual prediction needs ticking even after the hit notify window closes.
	CombatComponent.SetComponentTickEnabled(true);

	UE_LOG(LogTemp, Warning,
		TEXT("Started predicted visual root motion. Target=%s Montage=%s Distance=%.2f Duration=%.2f BlendOut=%.2f"),
		*GetNameSafe(TargetCharacter),
		*GetNameSafe(Montage),
		Entry.Distance,
		Entry.Duration,
		Entry.BlendOutTime);
}

void FPLLocalHitFeedbackRuntime::Tick(float DeltaTime)
{
	if (PredictedReactionVisuals.IsEmpty()) return;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	for (int32 Index = PredictedReactionVisuals.Num() - 1; Index >= 0; --Index)
	{
		FPLPredictedReactionVisualEntry& Entry = PredictedReactionVisuals[Index];

		if (!Entry.TargetCharacter.IsValid() ||
			!Entry.TargetMesh.IsValid() ||
			!Entry.TargetAnimInstance.IsValid() ||
			!Entry.Montage.IsValid())
		{
			FinishPredictedReactionVisual(Entry);
			PredictedReactionVisuals.RemoveAtSwap(Index);
			continue;
		}

		UpdatePredictedReactionVisual(Entry, Now);

		if (Entry.bBlendingOut)
		{
			const float BlendElapsed = Now - Entry.BlendOutStartTime;
			if (BlendElapsed >= Entry.BlendOutTime)
			{
				FinishPredictedReactionVisual(Entry);
				PredictedReactionVisuals.RemoveAtSwap(Index);
			}
		}
	}
}

void FPLLocalHitFeedbackRuntime::UpdatePredictedReactionVisual(
	FPLPredictedReactionVisualEntry& Entry,
	const float Now)
{
	USkeletalMeshComponent* TargetMesh = Entry.TargetMesh.Get();
	UAnimInstance* TargetAnimInstance = Entry.TargetAnimInstance.Get();

	if (!TargetMesh || !TargetAnimInstance) return;

	const bool bMontageStillPlaying = TargetAnimInstance->Montage_IsPlaying(Entry.Montage.Get());

	if (!Entry.bBlendingOut && !bMontageStillPlaying)
	{
		Entry.bBlendingOut = true;
		Entry.BlendOutStartTime = Now;
	}

	const float Elapsed = Now - Entry.StartTime;

	FVector DesiredOffset = FVector::ZeroVector;

	if (!Entry.bBlendingOut)
	{
		const float Alpha = FMath::Clamp(Elapsed / Entry.Duration, 0.f, 1.f);
		const float SmoothedAlpha = FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f);

		DesiredOffset = Entry.Direction * Entry.Distance * SmoothedAlpha;

		if (Alpha >= 1.f)
		{
			Entry.bBlendingOut = true;
			Entry.BlendOutStartTime = Now;
		}
	}
	else
	{
		const float BlendElapsed = Now - Entry.BlendOutStartTime;
		const float BlendAlpha = FMath::Clamp(BlendElapsed / Entry.BlendOutTime, 0.f, 1.f);

		DesiredOffset = FMath::Lerp(Entry.LastAppliedOffset, FVector::ZeroVector, BlendAlpha);
	}

	// Additive over whatever CharacterMovement/network smoothing has done this frame.
	const FVector CurrentBaseRelativeLocation = TargetMesh->GetRelativeLocation() - Entry.LastAppliedOffset;
	TargetMesh->SetRelativeLocation(CurrentBaseRelativeLocation + DesiredOffset);

	Entry.LastAppliedOffset = DesiredOffset;
}

void FPLLocalHitFeedbackRuntime::FinishPredictedReactionVisual(FPLPredictedReactionVisualEntry& Entry)
{
	USkeletalMeshComponent* TargetMesh = Entry.TargetMesh.Get();
	UAnimInstance* TargetAnimInstance = Entry.TargetAnimInstance.Get();

	if (TargetMesh)
	{
		const FVector CurrentBaseRelativeLocation = TargetMesh->GetRelativeLocation() - Entry.LastAppliedOffset;
		TargetMesh->SetRelativeLocation(CurrentBaseRelativeLocation);
	}

	if (TargetAnimInstance)
	{
		TargetAnimInstance->RootMotionMode = Entry.PreviousRootMotionMode;

		if (UPL_AnimInstance* PLAnimInstance = Cast<UPL_AnimInstance>(TargetAnimInstance))
		{
			PLAnimInstance->bRootMotionEnabled = Entry.bPreviousPLRootMotionEnabled;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("Finished predicted visual root motion. Target=%s Montage=%s"),
		*GetNameSafe(Entry.TargetCharacter.Get()),
		*GetNameSafe(Entry.Montage.Get()));
}

FVector FPLLocalHitFeedbackRuntime::GetPredictionDirection(
	APL_BaseCharacter* TargetCharacter,
	const FHitResult& HitResult) const
{
	if (!TargetCharacter) return FVector::ZeroVector;

	AActor* OwnerActor = CombatComponent.GetOwner();

	FVector Direction = FVector::ZeroVector;

	if (OwnerActor)
	{
		Direction = TargetCharacter->GetActorLocation() - OwnerActor->GetActorLocation();
	}
	else if (!HitResult.ImpactNormal.IsNearlyZero())
	{
		Direction = -HitResult.ImpactNormal;
	}

	Direction.Z = 0.f;
	return Direction.GetSafeNormal();
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
