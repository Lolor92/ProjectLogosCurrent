// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Combat/Data/PL_HitWindowTypes.h"

class AActor;
class UAbilitySystemComponent;
class UAnimMontage;
class UGameplayEffect;
class UPL_CombatComponent;
struct FHitResult;

struct FPLLocalHitFeedbackEntry
{
	TWeakObjectPtr<AActor> TargetActor;
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

	void PruneOldEntries();

private:
	void PlayPredictedReactionMontage(AActor* HitActor, const FPLHitWindowSettings& HitWindowSettings) const;
	bool FindTriggerTagFromGameplayEffect(TSubclassOf<UGameplayEffect> GameplayEffectClass, FGameplayTag& OutTriggerTag) const;
	UAnimMontage* FindMontageFromAbilityTag(UAbilitySystemComponent* ASC, const FGameplayTag& AbilityTag) const;
	void ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue, const FHitResult& HitResult) const;
	bool ShouldExecuteLocalCameraShakeCue() const;
	FVector GetCueSpawnLocation(const FPLHitWindowGameplayCue& Cue, const FHitResult& HitResult) const;

	UPL_CombatComponent& CombatComponent;

	TArray<FPLLocalHitFeedbackEntry> RecentPredictedHits;

	float DuplicateSuppressionTime = 0.35f;
};
