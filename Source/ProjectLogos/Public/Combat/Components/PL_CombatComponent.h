// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Combat/Data/PL_HitWindowTypes.h"
#include "Combat/Data/PL_AbilitySet.h"
#include "Combat/Data/PL_TagReactionData.h"
#include "Combat/Runtime/PL_CombatHitWindowRuntime.h"
#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectKey.h"
#include "PL_CombatComponent.generated.h"

class APL_BaseCharacter;
class UAbilitySystemComponent;
class UAnimMontage;
class UGameplayEffect;
class FBoolProperty;
class UAnimNotifyState;
class USkeletalMeshComponent;
class FPLCombatTagReactionRuntime;

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
	friend class FPLCombatHitWindowRuntime;

public:
	UPL_CombatComponent(FVTableHelper& Helper);
	UPL_CombatComponent();
	virtual ~UPL_CombatComponent() override;
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void InitializeCombat(APL_BaseCharacter* InCharacter, UAbilitySystemComponent* InAbilitySystemComponent);
	void DeinitializeCombat();
	void HandleMovementModeChanged(EMovementMode NewMovementMode);
	bool IsBlockingActive() const;
	bool IsParryingActive() const;
	UFUNCTION(BlueprintPure, Category="Combat|Crowd Control")
	bool IsCrowdControlActive() const;
	void PlayPredictedHitReaction(const FHitResult& HitResult);
	void SetLastCombatReferenceActor(AActor* InActor);
	const FGameplayTag& GetBlockingTag() const { return BlockingTag; }
	const FGameplayTag& GetParryingTag() const { return ParryingTag; }
	APL_BaseCharacter* GetOwningCharacter() const { return OwningCharacter; }
	UAbilitySystemComponent* GetAbilitySystemComponent() const { return AbilitySystemComponent; }
	UPL_TagReactionData* GetTagReactionData() const { return TagReactionData; }
	FPLLocalHitFeedbackRuntime& GetLocalHitFeedbackRuntime() { return LocalHitFeedbackRuntime; }
	const FPL_TagReactionBinding* FindTagReactionBindingForTriggerTag(const FGameplayTag& TriggerTag) const;
	bool FindReactionAbilityTag(const FGameplayTag& TriggerTag, FGameplayTag& OutAbilityTag) const;
	UFUNCTION(BlueprintCallable, Category="Combat|Prediction")
	bool ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage);

	bool BeginHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp,
		FName TraceSocketName, const FPLHitWindowSettings& HitWindowSettings);

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

	// Tag required on a defender for hits to be considered parrying.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Parry")
	FGameplayTag ParryingTag;

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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Block")
	TSubclassOf<UGameplayEffect> AttackerBlockedEffectClass;

	// Applied when this component's owner successfully blocks an incoming hit.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Block")
	TSubclassOf<UGameplayEffect> DefenderBlockedEffectClass;

	// Applied when this component's owner lands a hit that gets parried.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Parry")
	TSubclassOf<UGameplayEffect> AttackerParriedEffectClass;

	// Applied when this component's owner successfully parries an incoming hit.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Parry")
	TSubclassOf<UGameplayEffect> DefenderParrySuccessEffectClass;

	// Applied when this component's owner lands a hit that gets dodged.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dodge")
	TSubclassOf<UGameplayEffect> AttackerDodgedEffectClass;

	// Applied when this component's owner successfully dodges an incoming hit.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Dodge")
	TSubclassOf<UGameplayEffect> DefenderDodgedEffectClass;

	// Applied when this component's owner lands a hit that is resisted by super armor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Super Armor")
	TSubclassOf<UGameplayEffect> AttackerSuperArmoredEffectClass;

	// Applied when this component's owner successfully resists a hit via super armor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Super Armor")
	TSubclassOf<UGameplayEffect> DefenderSuperArmoredEffectClass;

private:
	// Ability setup.
	void GrantDefaultAbilities();
	void ClearDefaultAbilities();

	// Crowd control.
	void BindCrowdControlTagEvent();
	void ClearCrowdControlTagEvent();
	void OnCrowdControlTagChanged(const FGameplayTag Tag, int32 NewCount);

	// Gameplay effects.
	FActiveGameplayEffectHandle ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass, float Level) const;

	void RemoveGameplayEffect(FActiveGameplayEffectHandle& EffectHandle);

	// Cached owner references.
	UPROPERTY()
	TObjectPtr<APL_BaseCharacter> OwningCharacter = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	// Granted default abilities.
	FPLAbilitySetGrantedHandles DefaultAbilityHandles;

	// Crowd control binding state.
	FGameplayTag BoundCrowdControlTag;
	FDelegateHandle CrowdControlTagDelegateHandle;

	// Airborne effect state.
	FActiveGameplayEffectHandle AirborneEffectHandle;
	bool bAirborneEffectApplied = false;

	FPLCombatTagReactionRuntime* TagReactionRuntime = nullptr;
	bool HasSuperArmorAtOrAbove(EPLHitWindowSuperArmorLevel RequiredSuperArmor) const;
	FPLCombatHitWindowRuntime HitWindowRuntime;
	FPLLocalHitFeedbackRuntime LocalHitFeedbackRuntime;
};
