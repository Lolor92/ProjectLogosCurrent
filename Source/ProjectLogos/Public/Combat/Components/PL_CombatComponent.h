// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Combat/Data/PL_HitWindowTypes.h"
#include "Combat/Data/PL_AbilitySet.h"
#include "Combat/Data/PL_TagReactionData.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectKey.h"
#include "PL_CombatComponent.generated.h"

class APL_BaseCharacter;
class UAbilitySystemComponent;
class UAnimInstance;
class UGameplayEffect;
class FBoolProperty;
class UAnimNotifyState;
class USkeletalMeshComponent;

// Drives an AnimInstance bool from one or more gameplay tags.
USTRUCT(BlueprintType)
struct FPL_AnimBoolBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim Bool")
	FGameplayTagContainer Tags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim Bool")
	FName AnimBoolName;

	FBoolProperty* CachedBoolProperty = nullptr;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTLOGOS_API UPL_CombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPL_CombatComponent();

	void InitializeCombat(APL_BaseCharacter* InCharacter, UAbilitySystemComponent* InAbilitySystemComponent);
	void DeinitializeCombat();
	void HandleMovementModeChanged(EMovementMode NewMovementMode);
	
	bool BeginHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp,
		FName DebugSocketName, const FPLHitWindowShapeSettings& HitShapeSettings,
		const TArray<FPLHitWindowGameplayEffect>& GameplayEffectsToApply);
	void EndHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp);

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	
	// Default abilities.
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TArray<TObjectPtr<UPL_AbilitySet>> DefaultAbilitySets;

	// Gameplay tag reactions.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Tag Reactions")
	TObjectPtr<UPL_TagReactionData> TagReactionData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Tag Reactions", meta=(TitleProperty="AnimBoolName"))
	TArray<FPL_AnimBoolBinding> AnimBoolBindings;

	// Crowd control.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crowd Control")
	FGameplayTag CrowdControlTag;

	// Airborne gameplay effect.
	UPROPERTY(EditDefaultsOnly, Category="Effects")
	TSubclassOf<UGameplayEffect> AirborneEffectClass;

private:
	void GrantDefaultAbilities();
	void ClearDefaultAbilities();

	void BindCrowdControlTagEvent();
	void ClearCrowdControlTagEvent();
	void OnCrowdControlTagChanged(const FGameplayTag Tag, int32 NewCount);

	void BindTagReactionEvents();
	void ClearTagReactionEvents();
	void OnReactionTagChanged(const FGameplayTag Tag, int32 NewCount);
	void CacheAnimBoolBindings();
	void SetAnimBool(const FPL_AnimBoolBinding& Binding, bool bValue) const;
	bool IsAnimBoolActive(const FPL_AnimBoolBinding& Binding) const;
	void QueueAbilityActivation(const FPL_TagReactionBinding& Binding, FGameplayTag TriggeredTag);
	void QueueEffectApply(const FPL_TagReactionBinding& Binding, FGameplayTag TriggeredTag);
	void QueueEffectRemove(const FPL_TagReactionBinding& Binding, FGameplayTag TriggeredTag);
	FName GetRemoveTimerKey(const FPL_TagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const;
	void ExecuteDelayed(TFunction<void()> Function, float DelaySeconds, FTimerHandle& TimerHandle);

	FActiveGameplayEffectHandle ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass,
		float Level) const;
	void RemoveGameplayEffect(FActiveGameplayEffectHandle& EffectHandle);

	UPROPERTY()
	TObjectPtr<APL_BaseCharacter> OwningCharacter = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAnimInstance> AnimInstance = nullptr;

	FPLAbilitySetGrantedHandles DefaultAbilityHandles;
	FGameplayTag BoundCrowdControlTag;
	FDelegateHandle CrowdControlTagDelegateHandle;
	TMap<FGameplayTag, FDelegateHandle> TagReactionDelegateHandles;
	TMap<FGameplayTag, FTimerHandle> AbilityReactionTimers;
	TMap<FGameplayTag, FTimerHandle> ApplyEffectReactionTimers;
	TMap<FName, FTimerHandle> RemoveEffectReactionTimers;
	FActiveGameplayEffectHandle AirborneEffectHandle;

	bool bAirborneEffectApplied = false;
	
	TMap<FObjectKey, FName> ActiveHitDetectionWindows;
	
	void RunHitDebugQuery(const FTransform& StartTransform, const FTransform& EndTransform, bool bDrawDebug);
	void DebugSweepActiveHitWindow();
	void ResetActiveHitDebugWindow();
	FTransform GetHitTraceWorldTransform(USkeletalMeshComponent* MeshComp, FName SocketName,
		const FPLHitWindowShapeSettings& HitShapeSettings) const;
	void TryApplyHitGameplayEffects(AActor* HitActor, const FHitResult& HitResult);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ActiveHitDebugMesh = nullptr;

	FName ActiveHitDebugSocketName = NAME_None;
	FTransform PreviousHitDebugTransform = FTransform::Identity;
	bool bHitDebugWindowActive = false;
	bool bHasPreviousHitDebugLocation = false;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;
	
	TMap<FObjectKey, int32> ActiveHitDetectionWindowCounts;
	int32 ActiveHitDebugWindowDepth = 0;
	FPLHitWindowShapeSettings ActiveHitShapeSettings;
	TArray<FPLHitWindowGameplayEffect> ActiveGameplayEffectsToApply;
};
