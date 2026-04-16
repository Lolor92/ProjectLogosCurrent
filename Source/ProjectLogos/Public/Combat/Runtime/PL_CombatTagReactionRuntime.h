// Copyright ProjectLogos

#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "CoreMinimal.h"
#include "Combat/Data/PL_TagReactionData.h"

class APL_BaseCharacter;
class UAbilitySystemComponent;
class UAnimInstance;
class UGameplayEffect;
class UPL_CombatComponent;
class UPL_TagReactionData;
struct FPL_AnimBoolBinding;
struct FTimerHandle;

class FPLCombatTagReactionRuntime
{
public:
	explicit FPLCombatTagReactionRuntime(UPL_CombatComponent& InCombatComponent);
	~FPLCombatTagReactionRuntime();

	void Initialize(APL_BaseCharacter* InCharacter, UAbilitySystemComponent* InAbilitySystemComponent,
		UPL_TagReactionData* InTagReactionData, TArray<FPL_AnimBoolBinding>& InAnimBoolBindings);
	void Deinitialize();

private:
	void BindTagReactionEvents();
	void ClearTagReactionEvents();
	void OnReactionTagChanged(FGameplayTag Tag, int32 NewCount);
	void CacheAnimBoolBindings();
	void SetAnimBool(const FPL_AnimBoolBinding& Binding, bool bValue) const;
	bool IsAnimBoolActive(const FPL_AnimBoolBinding& Binding) const;
	void QueueAbilityActivation(const FPL_TagReactionBinding& Binding, FGameplayTag TriggeredTag);
	void QueueEffectApply(const FPL_TagReactionBinding& Binding, FGameplayTag TriggeredTag);
	void QueueEffectRemove(const FPL_TagReactionBinding& Binding, FGameplayTag TriggeredTag);
	FName GetRemoveTimerKey(const FPL_TagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const;
	void ExecuteDelayed(TFunction<void()> Function, float DelaySeconds, FTimerHandle& TimerHandle);
	FActiveGameplayEffectHandle ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass, float Level) const;

	TWeakObjectPtr<UPL_CombatComponent> CombatComponent;
	TWeakObjectPtr<APL_BaseCharacter> OwningCharacter;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
	TWeakObjectPtr<UAnimInstance> AnimInstance;

	UPL_TagReactionData* TagReactionData = nullptr;
	TArray<FPL_AnimBoolBinding>* AnimBoolBindings = nullptr;

	TMap<FGameplayTag, FDelegateHandle> TagReactionDelegateHandles;
	TMap<FGameplayTag, FTimerHandle> AbilityReactionTimers;
	TMap<FGameplayTag, FTimerHandle> ApplyEffectReactionTimers;
	TMap<FName, FTimerHandle> RemoveEffectReactionTimers;
};
