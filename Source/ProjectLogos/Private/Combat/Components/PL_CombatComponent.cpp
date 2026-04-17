// Copyright ProjectLogos

#include "Combat/Components/PL_CombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Runtime/PL_CombatHitWindowRuntime.h"
#include "Combat/Runtime/PL_CombatTagReactionRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Tag/PL_NativeTags.h"

UPL_CombatComponent::UPL_CombatComponent(FVTableHelper& Helper)
	: Super(Helper)
	, HitWindowRuntime(*this)
	, LocalHitFeedbackRuntime(*this)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(false);
	TagReactionRuntime = new FPLCombatTagReactionRuntime(*this);
}

UPL_CombatComponent::UPL_CombatComponent()
	: HitWindowRuntime(*this)
	, LocalHitFeedbackRuntime(*this)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(false);
	TagReactionRuntime = new FPLCombatTagReactionRuntime(*this);
}

UPL_CombatComponent::~UPL_CombatComponent()
{
	delete TagReactionRuntime;
	TagReactionRuntime = nullptr;
}

void UPL_CombatComponent::InitializeCombat(
	APL_BaseCharacter* InCharacter,
	UAbilitySystemComponent* InAbilitySystemComponent)
{
	if (AbilitySystemComponent && AbilitySystemComponent != InAbilitySystemComponent) DeinitializeCombat();

	OwningCharacter = InCharacter;
	AbilitySystemComponent = InAbilitySystemComponent;

	GrantDefaultAbilities();
	BindCrowdControlTagEvent();
	if (TagReactionRuntime) TagReactionRuntime->Initialize(OwningCharacter, AbilitySystemComponent, TagReactionData, AnimBoolBindings);
}

void UPL_CombatComponent::DeinitializeCombat()
{
	if (TagReactionRuntime) TagReactionRuntime->Deinitialize();
	HitWindowRuntime.Deinitialize();

	ClearCrowdControlTagEvent();
	RemoveGameplayEffect(AirborneEffectHandle);
	ClearDefaultAbilities();

	OwningCharacter = nullptr;
	AbilitySystemComponent = nullptr;
	bAirborneEffectApplied = false;
}

void UPL_CombatComponent::HandleMovementModeChanged(EMovementMode NewMovementMode)
{
	if (!OwningCharacter || !AbilitySystemComponent) return;

	const bool bIsAirborne = NewMovementMode == MOVE_Falling;
	if (bIsAirborne)
	{
		if (!bAirborneEffectApplied && AirborneEffectClass)
		{
			AirborneEffectHandle = ApplyEffectToSelf(AirborneEffectClass, 1.f);
			bAirborneEffectApplied = AirborneEffectHandle.IsValid();
		}

		return;
	}

	if (bAirborneEffectApplied)
	{
		RemoveGameplayEffect(AirborneEffectHandle);
		bAirborneEffectApplied = false;
	}
}

bool UPL_CombatComponent::IsBlockingActive() const
{
	return AbilitySystemComponent
		&& BlockingTag.IsValid()
		&& AbilitySystemComponent->HasMatchingGameplayTag(BlockingTag);
}

bool UPL_CombatComponent::IsParryingActive() const
{
	return AbilitySystemComponent
		&& ParryingTag.IsValid()
		&& AbilitySystemComponent->HasMatchingGameplayTag(ParryingTag);
}

void UPL_CombatComponent::PlayPredictedHitReaction(const FHitResult& HitResult)
{
	AActor* HitActor = HitResult.GetActor();
	UE_LOG(LogTemp, Warning, TEXT("HitActor: %s"), *GetNameSafe(HitActor));

	APL_BaseCharacter* TargetCharacter = Cast<APL_BaseCharacter>(HitActor);

	if (TargetCharacter->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("Stop: Server should not play predicted local reaction."));
		return;
	}

	USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
	if (!TargetMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Stop: TargetMesh is null."));
		return;
	}

	UAnimInstance* TargetAnimInstance = TargetMesh->GetAnimInstance();
	if (!TargetAnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("Stop: TargetAnimInstance is null."));
		return;
	}
}

void UPL_CombatComponent::SetLastCombatReferenceActor(AActor* InActor)
{
	HitWindowRuntime.SetLastCombatReferenceActor(InActor);
}

const FPL_TagReactionBinding* UPL_CombatComponent::FindTagReactionBindingForTriggerTag(const FGameplayTag& TriggerTag) const
{
	if (!TagReactionData || !TriggerTag.IsValid()) return nullptr;

	for (const FPL_TagReactionBinding& Reaction : TagReactionData->Reactions)
	{
		if (!Reaction.TriggerTag.IsValid()) continue;
		if (!TriggerTag.MatchesTag(Reaction.TriggerTag)) continue;

		return &Reaction;
	}

	return nullptr;
}

bool UPL_CombatComponent::FindReactionAbilityTag(const FGameplayTag& TriggerTag, FGameplayTag& OutAbilityTag) const
{
	OutAbilityTag = FGameplayTag();

	if (!TagReactionData || !TriggerTag.IsValid()) return false;

	for (const FPL_TagReactionBinding& Reaction : TagReactionData->Reactions)
	{
		if (!Reaction.TriggerTag.IsValid()) continue;
		if (!TriggerTag.MatchesTag(Reaction.TriggerTag)) continue;
		if (!Reaction.Ability.AbilityTag.IsValid()) continue;

		OutAbilityTag = Reaction.Ability.AbilityTag;
		return true;
	}

	return false;
}

bool UPL_CombatComponent::HasSuperArmorAtOrAbove(const EPLHitWindowSuperArmorLevel RequiredSuperArmor) const
{
	if (!AbilitySystemComponent)
	{
		return false;
	}

	auto HasConfiguredTag = [this](const FGameplayTag& Tag)
	{
		return Tag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(Tag);
	};

	switch (RequiredSuperArmor)
	{
	case EPLHitWindowSuperArmorLevel::SuperArmor1:
		return HasConfiguredTag(SuperArmorTag1) || HasConfiguredTag(SuperArmorTag2) || HasConfiguredTag(SuperArmorTag3);

	case EPLHitWindowSuperArmorLevel::SuperArmor2:
		return HasConfiguredTag(SuperArmorTag2) || HasConfiguredTag(SuperArmorTag3);

	case EPLHitWindowSuperArmorLevel::SuperArmor3:
		return HasConfiguredTag(SuperArmorTag3);

	case EPLHitWindowSuperArmorLevel::None:
	default:
		return false;
	}
}

void UPL_CombatComponent::GrantDefaultAbilities()
{
	if (!AbilitySystemComponent || !OwningCharacter || !OwningCharacter->HasAuthority()) return;

	for (const UPL_AbilitySet* AbilitySet : DefaultAbilitySets)
	{
		if (!AbilitySet) continue;

		AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &DefaultAbilityHandles, OwningCharacter);
	}
}

void UPL_CombatComponent::ClearDefaultAbilities()
{
	if (!AbilitySystemComponent || !OwningCharacter || !OwningCharacter->HasAuthority()) return;

	DefaultAbilityHandles.TakeFromAbilitySystem(AbilitySystemComponent);
}

void UPL_CombatComponent::BindCrowdControlTagEvent()
{
	if (!AbilitySystemComponent || !CrowdControlTag.IsValid() || CrowdControlTagDelegateHandle.IsValid()) return;

	CrowdControlTagDelegateHandle = AbilitySystemComponent
		->RegisterGameplayTagEvent(CrowdControlTag, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::OnCrowdControlTagChanged);

	BoundCrowdControlTag = CrowdControlTag;
}

void UPL_CombatComponent::ClearCrowdControlTagEvent()
{
	if (AbilitySystemComponent && BoundCrowdControlTag.IsValid() && CrowdControlTagDelegateHandle.IsValid())
	{
		AbilitySystemComponent
			->RegisterGameplayTagEvent(BoundCrowdControlTag, EGameplayTagEventType::NewOrRemoved)
			.Remove(CrowdControlTagDelegateHandle);
	}

	CrowdControlTagDelegateHandle.Reset();
	BoundCrowdControlTag = FGameplayTag();
}

void UPL_CombatComponent::OnCrowdControlTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (!OwningCharacter || !OwningCharacter->IsLocallyControlled()) return;

	if (AController* ActorController = OwningCharacter->GetController())
	{
		ActorController->SetIgnoreMoveInput(NewCount > 0);
	}
}

FActiveGameplayEffectHandle UPL_CombatComponent::ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass,
	float Level) const
{
	if (!GameplayEffectClass || !AbilitySystemComponent || !AbilitySystemComponent->IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle ContextHandle = AbilitySystemComponent->MakeEffectContext();
	ContextHandle.AddSourceObject(GetOwner());

	const FGameplayEffectSpecHandle SpecHandle =
		AbilitySystemComponent->MakeOutgoingSpec(GameplayEffectClass, Level, ContextHandle);

	return SpecHandle.IsValid()
		? AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get())
		: FActiveGameplayEffectHandle();
}

void UPL_CombatComponent::RemoveGameplayEffect(FActiveGameplayEffectHandle& EffectHandle)
{
	if (!AbilitySystemComponent || !EffectHandle.IsValid()) return;

	AbilitySystemComponent->RemoveActiveGameplayEffect(EffectHandle);
	EffectHandle.Invalidate();
}

void UPL_CombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	HitWindowRuntime.Tick();
}

bool UPL_CombatComponent::BeginHitDetectionWindow(const UAnimNotifyState* NotifyState,
	USkeletalMeshComponent* MeshComp, FName TraceSocketName, const FPLHitWindowSettings& HitWindowSettings)
{
	return HitWindowRuntime.BeginHitDetectionWindow(NotifyState, MeshComp, TraceSocketName, HitWindowSettings);
}

void UPL_CombatComponent::EndHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp)
{
	HitWindowRuntime.EndHitDetectionWindow(NotifyState, MeshComp);
}
