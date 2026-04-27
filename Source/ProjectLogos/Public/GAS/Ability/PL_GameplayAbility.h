// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbility.h"
#include "PL_GameplayAbility.generated.h"

class UCapsuleComponent;

USTRUCT(BlueprintType)
struct FMontageLockout
{
	GENERATED_BODY()

	// Prevents another ability from interrupting this montage too early.
	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout", meta=(InLineEditConditionToggle))
	bool bUseMontageProgressLockout = false;

	UPROPERTY(EditDefaultsOnly, Category="Montage Lockout", meta=(EditCondition="bUseMontageProgressLockout",
		ClampMin="0.0", ClampMax="100.0", UIMin="0.0", UIMax="100.0", Units="Percent"))
	float MontageProgressBeforeInterrupt = 0.f;
};

UCLASS()
class PROJECTLOGOS_API UPL_GameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UPL_GameplayAbility();

	// Activation flow.
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Montage", meta=(DisplayName="Montage Lockout"))
	FMontageLockout MontageLockout;

	// Combo setup.
	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo")
	TSubclassOf<UGameplayAbility> ComboAbilityClass = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Combo", meta=(ClampMin="0.0", Units="Seconds"))
	float ComboWindowDuration = 2.f;

	TSubclassOf<UGameplayAbility> GetComboAbilityClass() const { return ComboAbilityClass; }
	float GetComboWindowDuration() const { return ComboWindowDuration; }
	bool IsComboWindowOpen() const { return bComboWindowOpen; }
	uint32 GetActivationSequenceId() const { return ActivationSequenceId; }

	// Used by animation state updates while this ability is active.
	bool IsRootMotionStoppedByCollision() const { return bRootMotionStoppedByCollision; }

protected:
	// Optional activation setup.
	UFUNCTION(BlueprintCallable, Category="Ability|Rotation")
	void RotateAvatarToControllerYawOnActivate() const;

	UFUNCTION(BlueprintCallable, Category="Ability|Rotation")
	void RotateAvatarToFaceActorOnActivate(AActor* TargetActor) const;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Rotation")
	bool bRotateToControllerYawOnActivate = false;

	// Allows this ability to break in over another active ability such as stagger or knockdown.
	UPROPERTY(EditDefaultsOnly, Category="Ability|Interrupt")
	bool bInterruptOtherAbilitiesOnActivate = false;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion")
	bool bStopRootMotionOnCollision = true;

	UPROPERTY(EditDefaultsOnly, Category="Ability|Root Motion", meta=(EditCondition="bStopRootMotionOnCollision", ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="180.0", Units="Degrees"))
	float RootMotionCollisionForwardAngleDegrees = 40.f;

	// Combo window control.
	UFUNCTION(BlueprintCallable, Category="Ability|Combo")
	void OpenComboWindow();

	UFUNCTION(BlueprintCallable, Category="Ability|Combo")
	void CloseComboWindow();

private:
	// Internal activation checks.
	bool CanUseAbility(const FGameplayAbilityActorInfo* ActorInfo) const;
	void InterruptOtherActiveAbilities() const;

	// Collision-driven root motion stop.
	void BindRootMotionCollisionStop();

	bool ShouldStopRootMotionFromCapsuleHit(
		const ACharacter* Character,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		const FHitResult& Hit
	) const;

	UFUNCTION()
	void OnCapsuleHit(
		UPrimitiveComponent* HitComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit
	);

	void ResetComboWindow();

	FTimerHandle ComboWindowTimerHandle;

	bool bComboWindowOpen = false;

	UPROPERTY()
	TObjectPtr<UCapsuleComponent> CachedCapsule = nullptr;

	// Incremented every time this instanced ability activates so retriggers can be detected.
	uint32 ActivationSequenceId = 0;

	bool bRootMotionStoppedByCollision = false;
};
