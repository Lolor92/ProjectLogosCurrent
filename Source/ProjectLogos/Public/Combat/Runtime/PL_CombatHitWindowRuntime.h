// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Combat/Data/PL_HitWindowTypes.h"
#include "UObject/ObjectKey.h"

class AActor;
class UAbilitySystemComponent;
class UAnimNotifyState;
class UGameplayEffect;
class USceneComponent;
class USkeletalMeshComponent;
class UPL_CombatComponent;

class FPLCombatHitWindowRuntime
{
public:
	explicit FPLCombatHitWindowRuntime(UPL_CombatComponent& InCombatComponent);

	void Deinitialize();
	void Tick();
	void SetLastCombatReferenceActor(AActor* InActor);
	bool BeginHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp,
		FName TraceSocketName, const FPLHitWindowSettings& HitWindowSettings);
	void EndHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp);

private:
	void RunHitDebugQuery(const FTransform& StartTransform, const FTransform& EndTransform, bool bDrawDebug);
	void DebugSweepActiveHitWindow();
	void ResetActiveHitDebugWindow();

	FTransform GetHitTraceWorldTransform(USkeletalMeshComponent* MeshComp, FName SocketName,
		const FPLHitWindowShapeSettings& HitShapeSettings) const;

	void TryApplyHitGameplayEffects(AActor* HitActor, const FHitResult& HitResult);
	void ApplyActivationTransformEffects() const;
	void ApplyHitWindowTransformEffects(AActor* HitActor, bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyHitWindowMovement(AActor* HitActor, EPLHitWindowTransformTriggerTiming InvocationTiming,
		bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyHitWindowRotation(AActor* HitActor, EPLHitWindowTransformTriggerTiming InvocationTiming,
		bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyMovementToActor(AActor* RecipientActor, AActor* ReferenceActor,
		const FPLHitWindowMovementSettings& MovementSettings) const;
	void ApplyRotationToActor(AActor* RecipientActor, AActor* ReferenceActor,
		const FPLHitWindowRotationSettings& RotationSettings) const;
	AActor* ResolveTransformReferenceActor(EPLHitWindowReferenceActorSource ReferenceSource, AActor* HitActor,
		EPLHitWindowTransformTriggerTiming InvocationTiming) const;
	void ExecuteHitWindowGameplayCues(AActor* HitActor, const FHitResult* HitResult,
		EPLHitWindowCueTriggerTiming TriggerTiming) const;
	void ExecuteGameplayCueOnASC(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
		const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const;
	void ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const;
	bool ShouldExecuteLocalCameraShakeCue() const;
	FVector GetGameplayCueSpawnLocation(const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const;
	USceneComponent* GetGameplayCueAttachComponent(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
		const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const;
	bool IsAttackBlocked(AActor* HitActor) const;
	bool IsAttackParried(AActor* HitActor) const;
	bool IsAttackDodged(AActor* HitActor) const;
	bool HasRequiredSuperArmor(AActor* HitActor) const;
	void ApplyDefenseGameplayEffects(AActor* HitActor, const FHitResult& HitResult,
		bool bWasBlocked, bool bWasParried, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyGameplayEffectToActor(AActor* RecipientActor, const TSubclassOf<UGameplayEffect>& GameplayEffectClass,
		float EffectLevel, const FHitResult* HitResult) const;

	static bool IsWithinBlockAngle(const AActor* DefenderActor, const AActor* AttackerActor, float BlockAngleDegrees);
	static bool DoesTransformTimingMatch(EPLHitWindowTransformTriggerTiming ConfiguredTiming,
		EPLHitWindowTransformTriggerTiming InvocationTiming);
	static UPL_CombatComponent* FindCombatComponent(AActor* Actor);

	UPL_CombatComponent& CombatComponent;

	TMap<FObjectKey, FName> ActiveHitDetectionWindows;
	TMap<FObjectKey, int32> ActiveHitDetectionWindowCounts;

	TObjectPtr<USkeletalMeshComponent> ActiveHitDebugMesh = nullptr;
	FName ActiveHitDebugSocketName = NAME_None;
	FTransform PreviousHitDebugTransform = FTransform::Identity;
	bool bHitDebugWindowActive = false;
	bool bHasPreviousHitDebugLocation = false;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;
	int32 ActiveHitDebugWindowDepth = 0;

	FPLHitWindowSettings ActiveHitWindowSettings;
	bool bHasTriggeredHitStopThisWindow = false;

	TWeakObjectPtr<AActor> LastCombatReferenceActor;
};
