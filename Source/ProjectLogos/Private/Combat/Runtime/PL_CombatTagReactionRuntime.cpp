// Copyright ProjectLogos

#include "Combat/Runtime/PL_CombatTagReactionRuntime.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

FPLCombatTagReactionRuntime::FPLCombatTagReactionRuntime(UPL_CombatComponent& InCombatComponent)
	: CombatComponent(&InCombatComponent)
{
}

FPLCombatTagReactionRuntime::~FPLCombatTagReactionRuntime()
{
	Deinitialize();
}

void FPLCombatTagReactionRuntime::Initialize(APL_BaseCharacter* InCharacter,
	UAbilitySystemComponent* InAbilitySystemComponent, UPL_TagReactionData* InTagReactionData,
	TArray<FPL_AnimBoolBinding>& InAnimBoolBindings)
{
	Deinitialize();

	OwningCharacter = InCharacter;
	AbilitySystemComponent = InAbilitySystemComponent;
	TagReactionData = InTagReactionData;
	AnimBoolBindings = &InAnimBoolBindings;

	BindTagReactionEvents();
}

void FPLCombatTagReactionRuntime::Deinitialize()
{
	ClearTagReactionEvents();

	AnimInstance = nullptr;
	OwningCharacter = nullptr;
	AbilitySystemComponent = nullptr;
	TagReactionData = nullptr;
	AnimBoolBindings = nullptr;
}

void FPLCombatTagReactionRuntime::BindTagReactionEvents()
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	if (!ASC || !AnimBoolBindings) return;

	ClearTagReactionEvents();
	CacheAnimBoolBindings();

	TSet<FGameplayTag> WatchedTags;

	if (TagReactionData)
	{
		for (const FPL_TagReactionBinding& Reaction : TagReactionData->Reactions)
			if (Reaction.TriggerTag.IsValid()) WatchedTags.Add(Reaction.TriggerTag);
	}

	for (const FPL_AnimBoolBinding& Binding : *AnimBoolBindings)
	{
		TArray<FGameplayTag> Tags;
		Binding.Tags.GetGameplayTagArray(Tags);

		for (const FGameplayTag& Tag : Tags)
		{
			if (!Tag.IsValid()) continue;
			WatchedTags.Add(Tag);
		}
	}

	for (const FGameplayTag& Tag : WatchedTags)
	{
		FDelegateHandle DelegateHandle = ASC
			->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddRaw(this, &FPLCombatTagReactionRuntime::OnReactionTagChanged);

		TagReactionDelegateHandles.Add(Tag, DelegateHandle);
	}

	for (const FPL_AnimBoolBinding& Binding : *AnimBoolBindings)
	{
		SetAnimBool(Binding, IsAnimBoolActive(Binding));
	}
}

void FPLCombatTagReactionRuntime::ClearTagReactionEvents()
{
	if (UAbilitySystemComponent* ASC = AbilitySystemComponent.Get())
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Entry : TagReactionDelegateHandles)
		{
			if (!Entry.Key.IsValid() || !Entry.Value.IsValid()) continue;

			ASC->RegisterGameplayTagEvent(Entry.Key, EGameplayTagEventType::AnyCountChange).Remove(Entry.Value);
		}
	}

	TagReactionDelegateHandles.Reset();

	if (UPL_CombatComponent* Component = CombatComponent.Get())
	{
		if (UWorld* World = Component->GetWorld())
		{
			for (TPair<FGameplayTag, FTimerHandle>& Entry : AbilityReactionTimers)
			{
				World->GetTimerManager().ClearTimer(Entry.Value);
			}

			for (TPair<FGameplayTag, FTimerHandle>& Entry : ApplyEffectReactionTimers)
			{
				World->GetTimerManager().ClearTimer(Entry.Value);
			}

			for (TPair<FName, FTimerHandle>& Entry : RemoveEffectReactionTimers)
			{
				World->GetTimerManager().ClearTimer(Entry.Value);
			}
		}
	}

	AbilityReactionTimers.Reset();
	ApplyEffectReactionTimers.Reset();
	RemoveEffectReactionTimers.Reset();
}

void FPLCombatTagReactionRuntime::OnReactionTagChanged(const FGameplayTag Tag, const int32 NewCount)
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	if (!ASC || !AnimBoolBindings) return;

	const bool bAdded = NewCount > 0;

	if (TagReactionData && ASC->IsOwnerActorAuthoritative())
	{
		for (const FPL_TagReactionBinding& Reaction : TagReactionData->Reactions)
		{
			if (!Reaction.TriggerTag.IsValid() || !Tag.MatchesTag(Reaction.TriggerTag)) continue;

			const bool bShouldRun =
				Reaction.Policy == EPL_TagReactionPolicy::Both ||
				(Reaction.Policy == EPL_TagReactionPolicy::OnAdd && bAdded) ||
				(Reaction.Policy == EPL_TagReactionPolicy::OnRemove && !bAdded);

			if (!bShouldRun) continue;

			QueueEffectRemove(Reaction, Tag);
			QueueEffectApply(Reaction, Tag);
			QueueAbilityActivation(Reaction, Tag);
		}
	}

	for (const FPL_AnimBoolBinding& Binding : *AnimBoolBindings)
	{
		if (!Binding.Tags.HasTagExact(Tag)) continue;
		SetAnimBool(Binding, IsAnimBoolActive(Binding));
	}
}

void FPLCombatTagReactionRuntime::CacheAnimBoolBindings()
{
	APL_BaseCharacter* Character = OwningCharacter.Get();
	if (!Character || !AnimBoolBindings) return;

	if (USkeletalMeshComponent* MeshComp = Character->GetMesh())
	{
		AnimInstance = MeshComp->GetAnimInstance();
	}

	if (!AnimInstance.IsValid()) return;

	for (FPL_AnimBoolBinding& Binding : *AnimBoolBindings)
	{
		Binding.CachedBoolProperty = nullptr;
		if (Binding.AnimBoolName.IsNone()) continue;

		if (FProperty* Property = AnimInstance->GetClass()->FindPropertyByName(Binding.AnimBoolName))
		{
			Binding.CachedBoolProperty = CastField<FBoolProperty>(Property);
		}
	}
}

void FPLCombatTagReactionRuntime::SetAnimBool(const FPL_AnimBoolBinding& Binding, const bool bValue) const
{
	UAnimInstance* CurrentAnimInstance = AnimInstance.Get();
	if (!CurrentAnimInstance || !Binding.CachedBoolProperty) return;

	void* ValuePtr = Binding.CachedBoolProperty->ContainerPtrToValuePtr<void>(CurrentAnimInstance);
	Binding.CachedBoolProperty->SetPropertyValue(ValuePtr, bValue);
}

bool FPLCombatTagReactionRuntime::IsAnimBoolActive(const FPL_AnimBoolBinding& Binding) const
{
	if (const UAbilitySystemComponent* ASC = AbilitySystemComponent.Get())
	{
		return ASC->HasAnyMatchingGameplayTags(Binding.Tags);
	}

	return false;
}

void FPLCombatTagReactionRuntime::QueueAbilityActivation(const FPL_TagReactionBinding& Binding,
	const FGameplayTag TriggeredTag)
{
	if (!Binding.Ability.AbilityTag.IsValid() || !AbilitySystemComponent.IsValid()) return;

	TWeakObjectPtr<UAbilitySystemComponent> WeakASC = AbilitySystemComponent;

	TFunction<void()> ActivateAbility = [WeakASC, AbilityTag = Binding.Ability.AbilityTag]()
	{
		UAbilitySystemComponent* ASC = WeakASC.Get();
		if (!ASC) return;

		ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(AbilityTag));
	};

	FTimerHandle& TimerHandle = AbilityReactionTimers.FindOrAdd(TriggeredTag);
	ExecuteDelayed(MoveTemp(ActivateAbility), Binding.Ability.DelaySeconds, TimerHandle);
}

void FPLCombatTagReactionRuntime::QueueEffectApply(const FPL_TagReactionBinding& Binding, const FGameplayTag TriggeredTag)
{
	if (Binding.Effects.Apply.IsEmpty() || !AbilitySystemComponent.IsValid()) return;

	TArray<TSubclassOf<UGameplayEffect>> EffectsToApply = Binding.Effects.Apply;

	TFunction<void()> ApplyEffects = [this, EffectsToApply]()
	{
		for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectsToApply)
		{
			ApplyEffectToSelf(EffectClass, 1.f);
		}
	};

	FTimerHandle& TimerHandle = ApplyEffectReactionTimers.FindOrAdd(TriggeredTag);
	ExecuteDelayed(MoveTemp(ApplyEffects), Binding.Effects.ApplyDelaySeconds, TimerHandle);
}

void FPLCombatTagReactionRuntime::QueueEffectRemove(const FPL_TagReactionBinding& Binding, const FGameplayTag TriggeredTag)
{
	if (Binding.Effects.Remove.IsEmpty() || !AbilitySystemComponent.IsValid()) return;

	TWeakObjectPtr<UAbilitySystemComponent> WeakASC = AbilitySystemComponent;
	TArray<TSubclassOf<UGameplayEffect>> EffectsToRemove = Binding.Effects.Remove;

	TFunction<void()> RemoveEffects = [WeakASC, EffectsToRemove]()
	{
		UAbilitySystemComponent* ASC = WeakASC.Get();
		if (!ASC) return;

		for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectsToRemove)
		{
			if (!EffectClass) continue;

			FGameplayEffectQuery Query;
			Query.EffectDefinition = EffectClass;
			ASC->RemoveActiveEffects(Query);
		}
	};

	FTimerHandle& TimerHandle = RemoveEffectReactionTimers.FindOrAdd(GetRemoveTimerKey(Binding, TriggeredTag));
	ExecuteDelayed(MoveTemp(RemoveEffects), Binding.Effects.RemoveDelaySeconds, TimerHandle);
}

FName FPLCombatTagReactionRuntime::GetRemoveTimerKey(const FPL_TagReactionBinding& Binding,
	const FGameplayTag& TriggeredTag) const
{
	return Binding.Effects.RemoveTimerKey.IsNone()
		? TriggeredTag.GetTagName()
		: Binding.Effects.RemoveTimerKey;
}

void FPLCombatTagReactionRuntime::ExecuteDelayed(TFunction<void()> Function, const float DelaySeconds,
	FTimerHandle& TimerHandle)
{
	if (DelaySeconds <= 0.f) { Function(); return; }

	UPL_CombatComponent* Component = CombatComponent.Get();
	if (!Component) return;

	UWorld* World = Component->GetWorld();
	if (!World) return;

	World->GetTimerManager().ClearTimer(TimerHandle);

	FTimerDelegate TimerDelegate;
	TimerDelegate.BindLambda(MoveTemp(Function));
	World->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, DelaySeconds, false);
}

FActiveGameplayEffectHandle FPLCombatTagReactionRuntime::ApplyEffectToSelf(
	const TSubclassOf<UGameplayEffect>& GameplayEffectClass, const float Level) const
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	UPL_CombatComponent* Component = CombatComponent.Get();
	if (!GameplayEffectClass || !ASC || !Component || !ASC->IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}

	AActor* OwnerActor = Component->GetOwner();

	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddSourceObject(OwnerActor ? static_cast<UObject*>(OwnerActor) : static_cast<UObject*>(Component));

	const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(GameplayEffectClass, Level, ContextHandle);

	return SpecHandle.IsValid()
		? ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())
		: FActiveGameplayEffectHandle();
}
