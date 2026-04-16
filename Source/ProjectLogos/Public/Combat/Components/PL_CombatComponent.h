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
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void InitializeCombat(APL_BaseCharacter* InCharacter, UAbilitySystemComponent* InAbilitySystemComponent);
	void DeinitializeCombat();
	void HandleMovementModeChanged(EMovementMode NewMovementMode);
	bool IsBlockingActive() const;
	const FGameplayTag& GetBlockingTag() const { return BlockingTag; }

	bool BeginHitDetectionWindow(
		const UAnimNotifyState* NotifyState,
		USkeletalMeshComponent* MeshComp,
		FName DebugSocketName,
		const FPLHitWindowShapeSettings& HitShapeSettings,
		const FPLHitStopSettings& HitStopSettings,
		const FPLHitWindowMovementSettings& MovementSettings,
		const FPLHitWindowRotationSettings& RotationSettings,
		const FPLHitWindowBlockSettings& BlockSettings,
		const FPLHitWindowDodgeSettings& DodgeSettings,
		EPLHitWindowSuperArmorLevel RequiredSuperArmor,
		const TArray<FPLHitWindowGameplayEffect>& GameplayEffectsToApply,
		const TArray<FPLHitWindowGameplayCue>& GameplayCuesToExecute);

	void EndHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp);

protected:
	// Default abilities granted on authority.
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TArray<TObjectPtr<UPL_AbilitySet>> DefaultAbilitySets;

	// Gameplay tag driven reactions.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Tag Reactions")
	TObjectPtr<UPL_TagReactionData> TagReactionData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Tag Reactions", meta=(TitleProperty="AnimBoolName"))
	TArray<FPL_AnimBoolBinding> AnimBoolBindings;

	// Input lock tag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crowd Control")
	FGameplayTag CrowdControlTag;

	// Applied while falling.
	UPROPERTY(EditDefaultsOnly, Category="Effects")
	TSubclassOf<UGameplayEffect> AirborneEffectClass;

	// Tag required on a defender for hits to be considered blockable.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Block")
	FGameplayTag BlockingTag;

	// Tag required on a defender for hits to be considered dodgeable.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dodge")
	FGameplayTag DodgingTag;

	// Tags that mark the owner as having super armor at each level.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Super Armor")
	FGameplayTag SuperArmorTag1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Super Armor")
	FGameplayTag SuperArmorTag2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Super Armor")
	FGameplayTag SuperArmorTag3;

	// Applied when this component's owner lands a hit that gets blocked.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Effects|Block")
	TSubclassOf<UGameplayEffect> AttackerBlockedEffectClass;

	// Applied when this component's owner successfully blocks an incoming hit.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Effects|Block")
	TSubclassOf<UGameplayEffect> DefenderBlockedEffectClass;

	// Applied when this component's owner lands a hit that gets dodged.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Effects|Dodge")
	TSubclassOf<UGameplayEffect> AttackerDodgedEffectClass;

	// Applied when this component's owner successfully dodges an incoming hit.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Effects|Dodge")
	TSubclassOf<UGameplayEffect> DefenderDodgedEffectClass;

	// Applied when this component's owner lands a hit that is resisted by super armor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Effects|Super Armor")
	TSubclassOf<UGameplayEffect> AttackerSuperArmoredEffectClass;

	// Applied when this component's owner successfully resists a hit via super armor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Effects|Super Armor")
	TSubclassOf<UGameplayEffect> DefenderSuperArmoredEffectClass;

private:
	// Ability setup.
	void GrantDefaultAbilities();
	void ClearDefaultAbilities();

	// Crowd control.
	void BindCrowdControlTagEvent();
	void ClearCrowdControlTagEvent();
	void OnCrowdControlTagChanged(const FGameplayTag Tag, int32 NewCount);

	// Tag reactions.
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

	// Gameplay effects.
	FActiveGameplayEffectHandle ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass, float Level) const;

	void RemoveGameplayEffect(FActiveGameplayEffectHandle& EffectHandle);

	// Cached owner references.
	UPROPERTY()
	TObjectPtr<APL_BaseCharacter> OwningCharacter = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAnimInstance> AnimInstance = nullptr;

	// Granted default abilities.
	FPLAbilitySetGrantedHandles DefaultAbilityHandles;

	// Crowd control binding state.
	FGameplayTag BoundCrowdControlTag;
	FDelegateHandle CrowdControlTagDelegateHandle;

	// Tag reaction timers and delegates.
	TMap<FGameplayTag, FDelegateHandle> TagReactionDelegateHandles;
	TMap<FGameplayTag, FTimerHandle> AbilityReactionTimers;
	TMap<FGameplayTag, FTimerHandle> ApplyEffectReactionTimers;
	TMap<FName, FTimerHandle> RemoveEffectReactionTimers;

	// Airborne effect state.
	FActiveGameplayEffectHandle AirborneEffectHandle;
	bool bAirborneEffectApplied = false;

	// Active notify windows.
	TMap<FObjectKey, FName> ActiveHitDetectionWindows;
	TMap<FObjectKey, int32> ActiveHitDetectionWindowCounts;

	// Hit detection.
	void RunHitDebugQuery(const FTransform& StartTransform, const FTransform& EndTransform, bool bDrawDebug);
	void DebugSweepActiveHitWindow();
	void ResetActiveHitDebugWindow();

	FTransform GetHitTraceWorldTransform(USkeletalMeshComponent* MeshComp, FName SocketName, 
		const FPLHitWindowShapeSettings& HitShapeSettings) const;

	void TryApplyHitGameplayEffects(AActor* HitActor, const FHitResult& HitResult);
	void ApplyHitWindowTransformEffects(AActor* HitActor, bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyHitWindowMovement(AActor* HitActor, bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyHitWindowRotation(AActor* HitActor, bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ExecuteHitWindowGameplayCues(AActor* HitActor, const FHitResult* HitResult, EPLHitWindowCueTriggerTiming TriggerTiming) const;
	void ExecuteGameplayCueOnASC(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
		const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const;
	void ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const;
	bool ShouldExecuteLocalCameraShakeCue() const;
	FVector GetGameplayCueSpawnLocation(const FPLHitWindowGameplayCue& Cue,const FHitResult* HitResult) const;
	USceneComponent* GetGameplayCueAttachComponent(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
		const FPLHitWindowGameplayCue& Cue,const FHitResult* HitResult) const;
	bool IsAttackBlocked(AActor* HitActor) const;
	bool IsAttackDodged(AActor* HitActor) const;
	bool HasRequiredSuperArmor(AActor* HitActor) const;
	static bool IsWithinBlockAngle(const AActor* DefenderActor, const AActor* AttackerActor, float BlockAngleDegrees);
	bool HasSuperArmorAtOrAbove(EPLHitWindowSuperArmorLevel RequiredSuperArmor) const;
	void ApplyDefenseGameplayEffects(AActor* HitActor, const FHitResult& HitResult,
		bool bWasBlocked, bool bWasDodged, bool bHasSuperArmor) const;
	void ApplyGameplayEffectToActor(AActor* RecipientActor, const TSubclassOf<UGameplayEffect>& GameplayEffectClass,
		float EffectLevel, const FHitResult* HitResult) const;

	// Active hit window state.
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ActiveHitDebugMesh = nullptr;

	FName ActiveHitDebugSocketName = NAME_None;
	FTransform PreviousHitDebugTransform = FTransform::Identity;
	bool bHitDebugWindowActive = false;
	bool bHasPreviousHitDebugLocation = false;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;
	int32 ActiveHitDebugWindowDepth = 0;

	FPLHitWindowShapeSettings ActiveHitShapeSettings;
	FPLHitStopSettings ActiveHitStopSettings;
	FPLHitWindowMovementSettings ActiveHitMovementSettings;
	FPLHitWindowRotationSettings ActiveHitRotationSettings;
	FPLHitWindowBlockSettings ActiveHitBlockSettings;
	FPLHitWindowDodgeSettings ActiveHitDodgeSettings;
	EPLHitWindowSuperArmorLevel ActiveHitRequiredSuperArmor = EPLHitWindowSuperArmorLevel::None;
	TArray<FPLHitWindowGameplayEffect> ActiveGameplayEffectsToApply;
	TArray<FPLHitWindowGameplayCue> ActiveGameplayCuesToExecute;
	bool bHasTriggeredHitStopThisWindow = false;
};
