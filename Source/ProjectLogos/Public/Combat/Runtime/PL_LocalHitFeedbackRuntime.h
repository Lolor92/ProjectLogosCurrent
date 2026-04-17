// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Combat/Data/PL_HitWindowTypes.h"
#include "Combat/Data/PL_TagReactionData.h"

class AActor;
class APL_BaseCharacter;
class UAnimMontage;
class UGameplayEffect;
class UPL_CombatComponent;
class USkeletalMeshComponent;
struct FHitResult;

struct FPLLocalHitFeedbackEntry
{
	TWeakObjectPtr<AActor> TargetActor;
	float TimeSeconds = 0.f;
};

struct FPLPredictedReactionMontageEntry
{
	TWeakObjectPtr<UAnimMontage> Montage;
	float TimeSeconds = 0.f;
	bool bHasLoggedSuppression = false;
};

struct FPLPredictedReactionVisualEntry
{
	TWeakObjectPtr<APL_BaseCharacter> TargetCharacter;
	TWeakObjectPtr<USkeletalMeshComponent> TargetMesh;
	TWeakObjectPtr<UAnimInstance> TargetAnimInstance;
	TWeakObjectPtr<UAnimMontage> Montage;

	FVector Direction = FVector::ZeroVector;
	FVector LastAppliedOffset = FVector::ZeroVector;

	float Distance = 0.f;
	float Duration = 0.25f;
	float BlendOutTime = 0.15f;

	float StartTime = 0.f;
	float BlendOutStartTime = 0.f;

	bool bBlendingOut = false;
	bool bPreviousPLRootMotionEnabled = true;

	ERootMotionMode::Type PreviousRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
};

class PROJECTLOGOS_API FPLLocalHitFeedbackRuntime
{
public:
	explicit FPLLocalHitFeedbackRuntime(UPL_CombatComponent& InCombatComponent);

	void Tick(float DeltaTime);

	void PlayPredictedHitFeedback(
		AActor* HitActor,
		const FHitResult& HitResult,
		const FPLHitWindowSettings& HitWindowSettings);

	bool WasRecentlyPredictedHit(AActor* HitActor) const;

	void RegisterPredictedReactionMontage(UAnimMontage* Montage);
	bool ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage);

	bool HasActivePredictedReactionVisuals() const { return !PredictedReactionVisuals.IsEmpty(); }

	void PruneOldEntries();

private:
	void PlayPredictedReactionMontage(
		AActor* HitActor,
		const FHitResult& HitResult,
		const FPLHitWindowSettings& HitWindowSettings);

	void StartPredictedReactionVisualOffset(
		APL_BaseCharacter* TargetCharacter,
		UAnimMontage* Montage,
		const FPL_TagReactionAbility& ReactionAbility,
		const FHitResult& HitResult);

	void UpdatePredictedReactionVisual(FPLPredictedReactionVisualEntry& Entry, float Now);
	void FinishPredictedReactionVisual(FPLPredictedReactionVisualEntry& Entry);

	FVector GetPredictionDirection(
		APL_BaseCharacter* TargetCharacter,
		const FHitResult& HitResult) const;

	bool FindTriggerTagFromGameplayEffectClass(
		TSubclassOf<UGameplayEffect> GameplayEffectClass,
		FGameplayTag& OutTriggerTag) const;

	void PruneOldReactionMontageEntries();

	void ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue, const FHitResult& HitResult) const;
	bool ShouldExecuteLocalCameraShakeCue() const;
	FVector GetCueSpawnLocation(const FPLHitWindowGameplayCue& Cue, const FHitResult& HitResult) const;

	UPL_CombatComponent& CombatComponent;

	TArray<FPLLocalHitFeedbackEntry> RecentPredictedHits;
	TArray<FPLPredictedReactionMontageEntry> RecentPredictedReactionMontages;
	TArray<FPLPredictedReactionVisualEntry> PredictedReactionVisuals;

	float DuplicateSuppressionTime = 0.35f;

	// Long enough for knockdown / pushback / stagger plus ping and replication delay.
	float PredictedReactionReplaySuppressionTime = 3.0f;
};
