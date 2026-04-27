// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "PL_BaseCharacter.h"
#include "PL_NonPlayableCharacter.generated.h"

class UGameplayAbility;
class USphereComponent;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EPLDesiredCombatRangeBias : uint8
{
	Random UMETA(DisplayName="Random"),
	Min UMETA(DisplayName="Min"),
	Mid UMETA(DisplayName="Mid"),
	Max UMETA(DisplayName="Max")
};

USTRUCT(BlueprintType)
struct FPLWeightedGameplayAbilityEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat")
	TSubclassOf<UGameplayAbility> AbilityClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(ClampMin="0.0", UIMin="0.0"))
	float SelectionWeight = 1.f;
};

UCLASS()
class PROJECTLOGOS_API APL_NonPlayableCharacter : public APL_BaseCharacter
{
	GENERATED_BODY()

public:
	APL_NonPlayableCharacter();
	
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category="AI|Targeting")
	APL_BaseCharacter* FindBestTargetInAggroRadius();

	UFUNCTION(BlueprintCallable, Category="AI|Combat")
	float ChooseDesiredCombatRange() const;

	UFUNCTION(BlueprintCallable, Category="AI|Combat")
	TSubclassOf<UGameplayAbility> ChooseCombatAbilityForDistance(float DistanceToTarget) const;

	UFUNCTION(BlueprintPure, Category="AI|Combat")
	bool ShouldBackoffAfterAttack() const;

	UFUNCTION(BlueprintCallable, Category="AI|Targeting")
	void SetAggroRadius(float NewRadius);

	UFUNCTION(BlueprintPure, Category="AI|Targeting")
	float GetAggroRadius() const { return AggroRadius; }

	UFUNCTION(BlueprintPure, Category="AI|Targeting")
	int32 GetCachedAggroCandidateCount() const;
	
protected:
	virtual void InitAbilityActorInfo();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI|Targeting")
	TObjectPtr<USphereComponent> AggroSensor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Targeting", meta=(ClampMin="0.0"))
	float AggroRadius = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Targeting|Filters")
	bool bRequireTargetableTargets = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Targeting|Filters")
	bool bRequireAliveTargets = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Targeting|Filters")
	bool bRequireHostileTargets = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(ClampMin="0.0"))
	float DesiredCombatRangeMin = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(ClampMin="0.0"))
	float DesiredCombatRangeMax = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat")
	EPLDesiredCombatRangeBias DesiredCombatRangeBias = EPLDesiredCombatRangeBias::Random;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float DesiredCombatRangeBiasStrength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(TitleProperty="AbilityClass"))
	TArray<FPLWeightedGameplayAbilityEntry> CloseRangeAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(TitleProperty="AbilityClass"))
	TArray<FPLWeightedGameplayAbilityEntry> MidRangeAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(TitleProperty="AbilityClass"))
	TArray<FPLWeightedGameplayAbilityEntry> FarRangeAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat", meta=(ClampMin="0"))
	int32 AttackCountBeforeBackoff = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Combat",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float BackoffChance = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Combat")
	int32 SuccessfulAttackCount = 0;

private:
	UFUNCTION()
	void HandleAggroSensorBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleAggroSensorEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	bool IsValidAggroCandidate(const APL_BaseCharacter* Candidate) const;
	TSubclassOf<UGameplayAbility> ChooseWeightedAbilityFromPool(
		const TArray<FPLWeightedGameplayAbilityEntry>& AbilityPool) const;
	void RefreshAggroCandidatesFromSensor();
	void AddAggroCandidate(APL_BaseCharacter* Candidate);
	void RemoveAggroCandidate(APL_BaseCharacter* Candidate);

	TSet<TWeakObjectPtr<APL_BaseCharacter>> CachedAggroCandidates;
};
