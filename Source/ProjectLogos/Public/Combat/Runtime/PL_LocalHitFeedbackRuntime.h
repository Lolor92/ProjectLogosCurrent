// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Combat/Data/PL_HitWindowTypes.h"

class AActor;
class UAnimMontage;
class UGameplayEffect;
class UPL_CombatComponent;
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
};

class PROJECTLOGOS_API FPLLocalHitFeedbackRuntime
{
public:
	explicit FPLLocalHitFeedbackRuntime(UPL_CombatComponent& InCombatComponent);

	void PlayPredictedHitFeedback(
		AActor* HitActor,
		const FHitResult& HitResult,
		const FPLHitWindowSettings& HitWindowSettings);

	bool WasRecentlyPredictedHit(AActor* HitActor) const;

	void RegisterPredictedReactionMontage(UAnimMontage* Montage);

	bool TryCorrectPredictedReactionMontage(
		const UAnimMontage* Montage,
		float CurrentPositionSeconds,
		float& OutCorrectedPositionSeconds,
		bool& bOutShouldStop);

	void PruneOldEntries();

private:
	void PlayPredictedReactionMontage(AActor* HitActor, const FPLHitWindowSettings& HitWindowSettings) const;

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

	float DuplicateSuppressionTime = 0.35f;

	// Must be longer than your longest predicted reaction + bad ping.
	float PredictedReactionCorrectionTime = 2.5f;

	// Prevents tiny normal montage drift from being treated as a server restart.
	float PredictedReactionCorrectionTolerance = 0.05f;
};
