// Copyright ProjectLogos

#include "Combat/Components/PL_CombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Utilities/PL_CombatFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Tag/PL_NativeTags.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogPLCombatHitDetection, Log, All);

UPL_CombatComponent::UPL_CombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(false);
}

void UPL_CombatComponent::InitializeCombat(
	APL_BaseCharacter* InCharacter,
	UAbilitySystemComponent* InAbilitySystemComponent)
{
	if (AbilitySystemComponent && AbilitySystemComponent != InAbilitySystemComponent)
	{
		DeinitializeCombat();
	}

	OwningCharacter = InCharacter;
	AbilitySystemComponent = InAbilitySystemComponent;

	GrantDefaultAbilities();
	BindCrowdControlTagEvent();
	BindTagReactionEvents();
}

void UPL_CombatComponent::DeinitializeCombat()
{
	ClearTagReactionEvents();
	ClearCrowdControlTagEvent();
	RemoveGameplayEffect(AirborneEffectHandle);
	ClearDefaultAbilities();

	ActiveHitDetectionWindows.Reset();
	ActiveHitDetectionWindowCounts.Reset();
	ResetActiveHitDebugWindow();
	LastCombatReferenceActor.Reset();

	OwningCharacter = nullptr;
	AbilitySystemComponent = nullptr;
	AnimInstance = nullptr;
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

bool UPL_CombatComponent::DoesTransformTimingMatch(const EPLHitWindowTransformTriggerTiming ConfiguredTiming,
	const EPLHitWindowTransformTriggerTiming InvocationTiming)
{
	return ConfiguredTiming == EPLHitWindowTransformTriggerTiming::Both || ConfiguredTiming == InvocationTiming;
}

UPL_CombatComponent* UPL_CombatComponent::FindCombatComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(Actor))
	{
		return Character->GetCombatComponent();
	}

	return Actor->FindComponentByClass<UPL_CombatComponent>();
}

void UPL_CombatComponent::SetLastCombatReferenceActor(AActor* InActor)
{
	LastCombatReferenceActor = InActor && InActor != GetOwner() ? InActor : nullptr;
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

	// Movement input is driven by the replicated tag count.
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

void UPL_CombatComponent::BindTagReactionEvents()
{
	if (!AbilitySystemComponent) return;

	ClearTagReactionEvents();
	CacheAnimBoolBindings();

	TSet<FGameplayTag> WatchedTags;

	if (TagReactionData)
	{
		for (const FPL_TagReactionBinding& Reaction : TagReactionData->Reactions)
		{
			if (Reaction.TriggerTag.IsValid())
			{
				WatchedTags.Add(Reaction.TriggerTag);
			}
		}
	}

	for (const FPL_AnimBoolBinding& Binding : AnimBoolBindings)
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
		FDelegateHandle DelegateHandle = AbilitySystemComponent
			->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddUObject(this, &ThisClass::OnReactionTagChanged);

		TagReactionDelegateHandles.Add(Tag, DelegateHandle);
	}

	// Sync anim bools for tags already present when the ASC is initialized.
	for (const FPL_AnimBoolBinding& Binding : AnimBoolBindings)
	{
		SetAnimBool(Binding, IsAnimBoolActive(Binding));
	}
}

void UPL_CombatComponent::ClearTagReactionEvents()
{
	if (AbilitySystemComponent)
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Entry : TagReactionDelegateHandles)
		{
			if (!Entry.Key.IsValid() || !Entry.Value.IsValid()) continue;

			AbilitySystemComponent
				->RegisterGameplayTagEvent(Entry.Key, EGameplayTagEventType::AnyCountChange)
				.Remove(Entry.Value);
		}
	}

	TagReactionDelegateHandles.Reset();

	if (UWorld* World = GetWorld())
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

	AbilityReactionTimers.Reset();
	ApplyEffectReactionTimers.Reset();
	RemoveEffectReactionTimers.Reset();
}

void UPL_CombatComponent::OnReactionTagChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (!AbilitySystemComponent) return;

	const bool bAdded = NewCount > 0;

	// Server runs reactions. Clients only mirror anim bool state.
	if (TagReactionData && AbilitySystemComponent->IsOwnerActorAuthoritative())
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

	for (const FPL_AnimBoolBinding& Binding : AnimBoolBindings)
	{
		if (!Binding.Tags.HasTagExact(Tag)) continue;
		SetAnimBool(Binding, IsAnimBoolActive(Binding));
	}
}

void UPL_CombatComponent::CacheAnimBoolBindings()
{
	if (!OwningCharacter) return;

	if (USkeletalMeshComponent* MeshComp = OwningCharacter->GetMesh())
	{
		AnimInstance = MeshComp->GetAnimInstance();
	}

	if (!AnimInstance) return;

	for (FPL_AnimBoolBinding& Binding : AnimBoolBindings)
	{
		Binding.CachedBoolProperty = nullptr;
		if (Binding.AnimBoolName.IsNone()) continue;

		if (FProperty* Property = AnimInstance->GetClass()->FindPropertyByName(Binding.AnimBoolName))
		{
			Binding.CachedBoolProperty = CastField<FBoolProperty>(Property);
		}
	}
}

void UPL_CombatComponent::SetAnimBool(const FPL_AnimBoolBinding& Binding, const bool bValue) const
{
	if (!AnimInstance || !Binding.CachedBoolProperty) return;

	void* ValuePtr = Binding.CachedBoolProperty->ContainerPtrToValuePtr<void>(AnimInstance);
	Binding.CachedBoolProperty->SetPropertyValue(ValuePtr, bValue);
}

bool UPL_CombatComponent::IsAnimBoolActive(const FPL_AnimBoolBinding& Binding) const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasAnyMatchingGameplayTags(Binding.Tags);
}

void UPL_CombatComponent::QueueAbilityActivation(const FPL_TagReactionBinding& Binding, const FGameplayTag TriggeredTag)
{
	if (!Binding.Ability.AbilityTag.IsValid() || !AbilitySystemComponent) return;

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

void UPL_CombatComponent::QueueEffectApply(const FPL_TagReactionBinding& Binding, const FGameplayTag TriggeredTag)
{
	if (Binding.Effects.Apply.IsEmpty() || !AbilitySystemComponent) return;

	TWeakObjectPtr<UPL_CombatComponent> WeakThis = this;
	TArray<TSubclassOf<UGameplayEffect>> EffectsToApply = Binding.Effects.Apply;

	TFunction<void()> ApplyEffects = [WeakThis, EffectsToApply]()
	{
		UPL_CombatComponent* Component = WeakThis.Get();
		if (!Component || !Component->AbilitySystemComponent) return;

		for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectsToApply)
		{
			Component->ApplyEffectToSelf(EffectClass, 1.f);
		}
	};

	FTimerHandle& TimerHandle = ApplyEffectReactionTimers.FindOrAdd(TriggeredTag);
	ExecuteDelayed(MoveTemp(ApplyEffects), Binding.Effects.ApplyDelaySeconds, TimerHandle);
}

void UPL_CombatComponent::QueueEffectRemove(const FPL_TagReactionBinding& Binding, const FGameplayTag TriggeredTag)
{
	if (Binding.Effects.Remove.IsEmpty() || !AbilitySystemComponent) return;

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

FName UPL_CombatComponent::GetRemoveTimerKey(const FPL_TagReactionBinding& Binding, const FGameplayTag& TriggeredTag) const
{
	return Binding.Effects.RemoveTimerKey.IsNone()
		? TriggeredTag.GetTagName()
		: Binding.Effects.RemoveTimerKey;
}

void UPL_CombatComponent::ExecuteDelayed(TFunction<void()> Function, const float DelaySeconds, FTimerHandle& TimerHandle)
{
	if (DelaySeconds <= 0.f)
	{
		Function();
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().ClearTimer(TimerHandle);

	FTimerDelegate TimerDelegate;
	TimerDelegate.BindLambda(MoveTemp(Function));
	World->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, DelaySeconds, false);
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

	if (!bHitDebugWindowActive) return;

	// While the window is open, keep sweeping from previous socket transform to current.
	DebugSweepActiveHitWindow();
}

void UPL_CombatComponent::RunHitDebugQuery(const FTransform& StartTransform, const FTransform& EndTransform, bool bDrawDebug)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector StartLocation = StartTransform.GetLocation();
	const FVector EndLocation = EndTransform.GetLocation();
	const FQuat StartRotation = StartTransform.GetRotation();
	const FQuat EndRotation = EndTransform.GetRotation();
	const FQuat SweepRotation = EndTransform.GetRotation();

	const FPLHitWindowShapeSettings& ShapeSettings = ActiveHitWindowSettings.ShapeSettings;
	const float SphereRadius = FMath::Max(0.f, ShapeSettings.SphereRadius);
	const float CapsuleRadius = FMath::Max(0.f, ShapeSettings.CapsuleRadius);
	const float CapsuleHalfHeight = FMath::Max(CapsuleRadius, ShapeSettings.CapsuleHalfHeight);
	const FVector BoxHalfExtent = ShapeSettings.BoxHalfExtent.ComponentMax(FVector::ZeroVector);

	FCollisionShape CollisionShape;
	switch (ShapeSettings.ShapeType)
	{
		case EPLHitDetectionShapeType::Capsule:
			CollisionShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
			break;

		case EPLHitDetectionShapeType::Box:
			CollisionShape = FCollisionShape::MakeBox(BoxHalfExtent);
			break;

		case EPLHitDetectionShapeType::Sphere:
		default:
			CollisionShape = FCollisionShape::MakeSphere(SphereRadius);
			break;
	}

	if (bDrawDebug)
	{
		switch (ShapeSettings.ShapeType)
		{
			case EPLHitDetectionShapeType::Capsule:
				DrawDebugCapsule(World, EndLocation, CapsuleHalfHeight, CapsuleRadius, SweepRotation,
					FColor::Blue, false, 0.1f,0, 1.5f);
				break;

			case EPLHitDetectionShapeType::Box:
				DrawDebugBox(World, EndLocation, BoxHalfExtent, SweepRotation, FColor::Blue,
					false, 0.1f, 0, 1.5f);
				break;

			case EPLHitDetectionShapeType::Sphere:
			default:
				DrawDebugSphere(World, EndLocation, SphereRadius, 12, FColor::Blue,
					false, 0.1f, 0, 1.5f);
				break;
		}

		DrawDebugLine(World, StartLocation, EndLocation, FColor::Cyan, false, 0.1f, 0, 1.0f);

		DrawDebugDirectionalArrow(World, EndLocation, EndLocation + (EndTransform.GetUnitAxis(EAxis::X) * 30.f),
			12.f, FColor::Green, false, 0.1f, 0, 1.0f);
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PLHitDebugSweep), false, GetOwner());
	QueryParams.AddIgnoredActor(GetOwner());

	// Break long swings into smaller sweep segments to reduce gaps.
	const double RotationDeltaDegrees = FMath::RadiansToDegrees(StartRotation.AngularDistance(EndRotation));
	const double LargestShapeExtent = FMath::Max(
		FMath::Max(static_cast<double>(SphereRadius), static_cast<double>(CapsuleHalfHeight)),
		BoxHalfExtent.GetMax());

	const double DistanceStepSize = FMath::Max(20.0, LargestShapeExtent * 0.75);
	const int32 DistanceSteps = FMath::Clamp(
		FMath::CeilToInt(FVector::Distance(StartLocation, EndLocation) / DistanceStepSize),
		1,
		6);

	const int32 RotationSteps = FMath::Clamp(
		FMath::CeilToInt(RotationDeltaDegrees / 15.f),
		1,
		6);

	const int32 NumSubsteps = FMath::Clamp(FMath::Max(DistanceSteps, RotationSteps), 1, 6);

	for (int32 StepIndex = 1; StepIndex <= NumSubsteps; ++StepIndex)
	{
		const float PrevAlpha = static_cast<float>(StepIndex - 1) / static_cast<float>(NumSubsteps);
		const float CurrAlpha = static_cast<float>(StepIndex) / static_cast<float>(NumSubsteps);

		const FVector SegmentStartLocation = FMath::Lerp(StartLocation, EndLocation, PrevAlpha);
		const FVector SegmentEndLocation = FMath::Lerp(StartLocation, EndLocation, CurrAlpha);
		const FQuat SegmentRotation = FQuat::Slerp(StartRotation, EndRotation, CurrAlpha).GetNormalized();

		TArray<FHitResult> HitResults;
		World->SweepMultiByObjectType(HitResults, SegmentStartLocation, SegmentEndLocation, SegmentRotation,
			ObjectQueryParams, CollisionShape, QueryParams);

		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || HitActor == GetOwner()) continue;

			const TWeakObjectPtr<AActor> WeakHitActor(HitActor);
			if (HitActorsThisWindow.Contains(WeakHitActor)) continue;

			HitActorsThisWindow.Add(WeakHitActor);
			TryApplyHitGameplayEffects(HitActor, Hit);
		}
	}
}

void UPL_CombatComponent::DebugSweepActiveHitWindow()
{
	if (!GetOwner() || !ActiveHitDebugMesh)
	{
		ResetActiveHitDebugWindow();
		return;
	}

	const FTransform CurrentTransform = GetHitTraceWorldTransform(
		ActiveHitDebugMesh,
		ActiveHitDebugSocketName,
		ActiveHitWindowSettings.ShapeSettings);

	if (!bHasPreviousHitDebugLocation)
	{
		PreviousHitDebugTransform = CurrentTransform;
		bHasPreviousHitDebugLocation = true;
	}

	RunHitDebugQuery(
		PreviousHitDebugTransform,
		CurrentTransform,
		ActiveHitWindowSettings.DebugSettings.bDrawDebugTrace);
	PreviousHitDebugTransform = CurrentTransform;
}

void UPL_CombatComponent::ResetActiveHitDebugWindow()
{
	ActiveHitDebugMesh = nullptr;
	ActiveHitDebugSocketName = NAME_None;
	PreviousHitDebugTransform = FTransform::Identity;
	bHitDebugWindowActive = false;
	bHasPreviousHitDebugLocation = false;
	HitActorsThisWindow.Reset();
	ActiveHitDebugWindowDepth = 0;
	ActiveHitWindowSettings = FPLHitWindowSettings();
	bHasTriggeredHitStopThisWindow = false;

	SetComponentTickEnabled(false);
}

FTransform UPL_CombatComponent::GetHitTraceWorldTransform(USkeletalMeshComponent* MeshComp, FName SocketName,
	const FPLHitWindowShapeSettings& HitShapeSettings) const
{
	if (!MeshComp) return FTransform::Identity;

	const FTransform BaseTransform =
		(!SocketName.IsNone() && MeshComp->DoesSocketExist(SocketName))
			? MeshComp->GetSocketTransform(SocketName)
			: MeshComp->GetComponentTransform();

	return FTransform(
		BaseTransform.TransformRotation(HitShapeSettings.LocalRotation.Quaternion()),
		BaseTransform.TransformPosition(HitShapeSettings.LocalOffset),
		FVector::OneVector);
}

void UPL_CombatComponent::TryApplyHitGameplayEffects(AActor* HitActor, const FHitResult& HitResult)
{
	if (!AbilitySystemComponent || !HitActor || HitActor == GetOwner()) return;

	const bool bIsAuthority = GetOwner() && GetOwner()->HasAuthority();
	const bool bWasBlocked = bIsAuthority && IsAttackBlocked(HitActor);
	const bool bWasParried = bWasBlocked && IsAttackParried(HitActor);
	const bool bWasDodged = bIsAuthority && IsAttackDodged(HitActor);
	const bool bHasSuperArmor = bIsAuthority && HasRequiredSuperArmor(HitActor);
	const bool bHasDefenseOutcome = bWasBlocked || bWasParried || bWasDodged || bHasSuperArmor;

	if (bIsAuthority)
	{
		ApplyHitWindowTransformEffects(HitActor, bWasBlocked, bWasDodged, bHasSuperArmor);
	}

	if (bHasDefenseOutcome)
	{
		if (bWasParried)
		{
			SetLastCombatReferenceActor(HitActor);

			if (UPL_CombatComponent* TargetCombatComponent = FindCombatComponent(HitActor))
			{
				TargetCombatComponent->SetLastCombatReferenceActor(GetOwner());
			}
		}

		ApplyDefenseGameplayEffects(HitActor, HitResult, bWasBlocked, bWasParried, bWasDodged, bHasSuperArmor);
		return;
	}

	if (!ActiveHitWindowSettings.GameplayEffectsToApply.IsEmpty() && bIsAuthority)
	{
		if (UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor))
		{
			FGameplayEffectContextHandle ContextHandle = AbilitySystemComponent->MakeEffectContext();
			ContextHandle.AddSourceObject(this);
			ContextHandle.AddHitResult(HitResult);

			for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : ActiveHitWindowSettings.GameplayEffectsToApply)
			{
				if (!GameplayEffectToApply.GameplayEffectClass) continue;

				const FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(
					GameplayEffectToApply.GameplayEffectClass,
					GameplayEffectToApply.EffectLevel,
					ContextHandle);

				if (!SpecHandle.IsValid()) continue;

				AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
			}
		}
	}

	ExecuteHitWindowGameplayCues(HitActor, &HitResult, EPLHitWindowCueTriggerTiming::OnHit);

	if (!bHasTriggeredHitStopThisWindow && ActiveHitWindowSettings.HitStopSettings.IsEnabled() && OwningCharacter)
	{
		OwningCharacter->ApplyHitStop(
			ActiveHitWindowSettings.HitStopSettings.Duration,
			ActiveHitWindowSettings.HitStopSettings.TimeScale);
		bHasTriggeredHitStopThisWindow = true;
	}
}

void UPL_CombatComponent::ApplyHitWindowTransformEffects(AActor* HitActor, const bool bWasBlocked,
	const bool bWasDodged, const bool bHasSuperArmor) const
{
	if (!HitActor || HitActor == GetOwner()) return;

	ApplyHitWindowRotation(HitActor, EPLHitWindowTransformTriggerTiming::OnHit, bWasBlocked, bWasDodged, bHasSuperArmor);
	ApplyHitWindowMovement(HitActor, EPLHitWindowTransformTriggerTiming::OnHit, bWasBlocked, bWasDodged, bHasSuperArmor);
}

void UPL_CombatComponent::ApplyActivationTransformEffects() const
{
	ApplyHitWindowRotation(nullptr, EPLHitWindowTransformTriggerTiming::OnActivation, false, false, false);
	ApplyHitWindowMovement(nullptr, EPLHitWindowTransformTriggerTiming::OnActivation, false, false, false);
}

void UPL_CombatComponent::ApplyHitWindowMovement(AActor* HitActor,
	const EPLHitWindowTransformTriggerTiming InvocationTiming, const bool bWasBlocked,
	const bool bWasDodged, const bool bHasSuperArmor) const
{
	const FPLHitWindowMovementSettings& MovementSettings = ActiveHitWindowSettings.MovementSettings;
	if (MovementSettings.MoveDirection == EPLHitWindowMoveDirection::None || MovementSettings.MoveDistance <= 0.f)
	{
		return;
	}

	if (!DoesTransformTimingMatch(MovementSettings.TriggerTiming, InvocationTiming))
	{
		return;
	}

	if (InvocationTiming == EPLHitWindowTransformTriggerTiming::OnHit)
	{
		if (bWasDodged || bHasSuperArmor)
		{
			return;
		}

		if (bWasBlocked && !ActiveHitWindowSettings.DefenseSettings.BlockSettings.bAllowMovementWhenBlocked)
		{
			return;
		}
	}

	AActor* const OwnerActor = GetOwner();
	AActor* const TargetActor = HitActor ? HitActor : LastCombatReferenceActor.Get();
	AActor* const ReferenceActor = ResolveTransformReferenceActor(
		MovementSettings.ReferenceActorSource,
		HitActor,
		InvocationTiming);

	auto ApplyToRecipient = [this, &MovementSettings, ReferenceActor](AActor* RecipientActor)
	{
		if (!RecipientActor || !ReferenceActor || RecipientActor == ReferenceActor)
		{
			return;
		}

		ApplyMovementToActor(RecipientActor, ReferenceActor, MovementSettings);
	};

	switch (MovementSettings.Recipient)
	{
	case EPLHitWindowTransformRecipient::Instigator:
		ApplyToRecipient(OwnerActor);
		break;

	case EPLHitWindowTransformRecipient::Target:
		ApplyToRecipient(TargetActor);
		break;

	case EPLHitWindowTransformRecipient::Both:
		ApplyToRecipient(OwnerActor);
		if (TargetActor && TargetActor != OwnerActor)
		{
			ApplyToRecipient(TargetActor);
		}
		break;

	default:
		return;
	}
}

void UPL_CombatComponent::ApplyMovementToActor(AActor* RecipientActor, AActor* ReferenceActor,
	const FPLHitWindowMovementSettings& MovementSettings) const
{
	if (!RecipientActor || !ReferenceActor) return;

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();

	FVector ReferenceForward = ReferenceActor->GetActorForwardVector();
	ReferenceForward.Z = 0.f;
	ReferenceForward = ReferenceForward.GetSafeNormal();

	if (ReferenceForward.IsNearlyZero())
	{
		return;
	}

	FVector ReferenceRight = ReferenceActor->GetActorRightVector();
	ReferenceRight.Z = 0.f;
	ReferenceRight = ReferenceRight.GetSafeNormal();

	if (ReferenceRight.IsNearlyZero())
	{
		ReferenceRight = FVector::CrossProduct(FVector::UpVector, ReferenceForward).GetSafeNormal();
	}

	const FVector RelativeLocation = RecipientLocation - ReferenceLocation;
	const float CurrentForwardProjection = FVector::DotProduct(RelativeLocation, ReferenceForward);
	const float CurrentLateralProjection = FVector::DotProduct(RelativeLocation, ReferenceRight);

	float TargetForwardProjection = CurrentForwardProjection;
	float TargetLateralProjection = CurrentLateralProjection;

	switch (MovementSettings.MoveDirection)
	{
	case EPLHitWindowMoveDirection::KeepCurrentDistance:
		break;

	case EPLHitWindowMoveDirection::MoveCloser:
		TargetForwardProjection -= MovementSettings.MoveDistance;
		break;

	case EPLHitWindowMoveDirection::MoveAway:
		TargetForwardProjection += MovementSettings.MoveDistance;
		break;

	case EPLHitWindowMoveDirection::SnapToDistance:
		TargetForwardProjection = MovementSettings.MoveDistance;
		break;

	case EPLHitWindowMoveDirection::None:
	default:
		return;
	}

	switch (MovementSettings.LateralOffsetMode)
	{
	case EPLHitWindowLateralOffsetMode::KeepCurrent:
		break;

	case EPLHitWindowLateralOffsetMode::AddOffset:
		TargetLateralProjection += MovementSettings.LateralOffset;
		break;

	case EPLHitWindowLateralOffsetMode::SnapToOffset:
		TargetLateralProjection = MovementSettings.LateralOffset;
		break;

	default:
		break;
	}

	if (FMath::IsNearlyEqual(TargetForwardProjection, CurrentForwardProjection)
		&& FMath::IsNearlyEqual(TargetLateralProjection, CurrentLateralProjection))
	{
		return;
	}

	FVector NewLocation = ReferenceLocation
		+ (ReferenceForward * TargetForwardProjection)
		+ (ReferenceRight * TargetLateralProjection);
	NewLocation.Z = RecipientLocation.Z;

	RecipientActor->SetActorLocation(NewLocation, MovementSettings.bSweep, nullptr,
		ToTeleportType(MovementSettings.TeleportType));
}

void UPL_CombatComponent::ApplyHitWindowRotation(AActor* HitActor,
	const EPLHitWindowTransformTriggerTiming InvocationTiming, const bool bWasBlocked,
	const bool bWasDodged, const bool bHasSuperArmor) const
{
	const FPLHitWindowRotationSettings& RotationSettings = ActiveHitWindowSettings.RotationSettings;
	if (RotationSettings.RotationDirection == EPLHitWindowRotationDirection::None)
	{
		return;
	}

	if (!DoesTransformTimingMatch(RotationSettings.TriggerTiming, InvocationTiming))
	{
		return;
	}

	if (InvocationTiming == EPLHitWindowTransformTriggerTiming::OnHit)
	{
		if (bWasDodged || bHasSuperArmor)
		{
			return;
		}

		if (bWasBlocked && !ActiveHitWindowSettings.DefenseSettings.BlockSettings.bAllowRotationWhenBlocked)
		{
			return;
		}
	}

	AActor* const OwnerActor = GetOwner();
	AActor* const TargetActor = HitActor ? HitActor : LastCombatReferenceActor.Get();
	AActor* const ReferenceActor = ResolveTransformReferenceActor(
		RotationSettings.ReferenceActorSource,
		HitActor,
		InvocationTiming);

	auto ApplyToRecipient = [this, &RotationSettings, ReferenceActor](AActor* RecipientActor)
	{
		if (!RecipientActor || !ReferenceActor || RecipientActor == ReferenceActor)
		{
			return;
		}

		ApplyRotationToActor(RecipientActor, ReferenceActor, RotationSettings);
	};

	switch (RotationSettings.Recipient)
	{
	case EPLHitWindowTransformRecipient::Instigator:
		ApplyToRecipient(OwnerActor);
		break;

	case EPLHitWindowTransformRecipient::Target:
		ApplyToRecipient(TargetActor);
		break;

	case EPLHitWindowTransformRecipient::Both:
		ApplyToRecipient(OwnerActor);
		if (TargetActor && TargetActor != OwnerActor)
		{
			ApplyToRecipient(TargetActor);
		}
		break;

	default:
		return;
	}
}

void UPL_CombatComponent::ApplyRotationToActor(AActor* RecipientActor, AActor* ReferenceActor,
	const FPLHitWindowRotationSettings& RotationSettings) const
{
	if (!RecipientActor || !ReferenceActor) return;

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();
	FRotator DesiredRotation = RecipientActor->GetActorRotation();

	switch (RotationSettings.RotationDirection)
	{
	case EPLHitWindowRotationDirection::FaceToFace:
		{
			FVector ToReference = ReferenceLocation - RecipientLocation;
			ToReference.Z = 0.f;
			if (const FVector FacingDirection = ToReference.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPLHitWindowRotationDirection::FaceAway:
		{
			FVector AwayFromReference = RecipientLocation - ReferenceLocation;
			AwayFromReference.Z = 0.f;
			if (const FVector FacingDirection = AwayFromReference.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPLHitWindowRotationDirection::FaceOppositeInstigatorForward:
		{
			FVector OppositeDirection = -ReferenceActor->GetActorForwardVector();
			OppositeDirection.Z = 0.f;
			if (const FVector FacingDirection = OppositeDirection.GetSafeNormal(); !FacingDirection.IsNearlyZero())
			{
				DesiredRotation = FacingDirection.Rotation();
			}
			break;
		}

	case EPLHitWindowRotationDirection::FaceDirection:
		DesiredRotation = RotationSettings.DirectionToFace;
		break;

	case EPLHitWindowRotationDirection::None:
	default:
		return;
	}

	RecipientActor->SetActorRotation(DesiredRotation, ToTeleportType(RotationSettings.TeleportType));
}

AActor* UPL_CombatComponent::ResolveTransformReferenceActor(
	const EPLHitWindowReferenceActorSource ReferenceSource, AActor* HitActor,
	const EPLHitWindowTransformTriggerTiming InvocationTiming) const
{
	switch (ReferenceSource)
	{
	case EPLHitWindowReferenceActorSource::Instigator:
		return GetOwner();

	case EPLHitWindowReferenceActorSource::Target:
		return HitActor ? HitActor : (InvocationTiming == EPLHitWindowTransformTriggerTiming::OnActivation
			? LastCombatReferenceActor.Get()
			: nullptr);

	case EPLHitWindowReferenceActorSource::LastCombatReferenceActor:
		return LastCombatReferenceActor.Get();

	default:
		return nullptr;
	}
}

bool UPL_CombatComponent::IsAttackBlocked(AActor* HitActor) const
{
	const FPLHitWindowBlockSettings& BlockSettings = ActiveHitWindowSettings.DefenseSettings.BlockSettings;
	if (!BlockSettings.bBlockable || !HitActor) return false;
	if (!BlockingTag.IsValid()) return false;

	const UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC || !TargetASC->HasMatchingGameplayTag(BlockingTag))
	{
		return false;
	}

	return IsWithinBlockAngle(HitActor, GetOwner(), BlockSettings.BlockAngleDegrees);
}

bool UPL_CombatComponent::IsAttackParried(AActor* HitActor) const
{
	if (!HitActor) return false;

	const UPL_CombatComponent* TargetCombatComponent = FindCombatComponent(HitActor);
	return TargetCombatComponent && TargetCombatComponent->IsParryingActive();
}

bool UPL_CombatComponent::IsAttackDodged(AActor* HitActor) const
{
	if (!ActiveHitWindowSettings.DefenseSettings.DodgeSettings.bDodgeable || !HitActor) return false;
	if (!DodgingTag.IsValid()) return false;

	const UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor);
	return TargetASC && TargetASC->HasMatchingGameplayTag(DodgingTag);
}

bool UPL_CombatComponent::HasRequiredSuperArmor(AActor* HitActor) const
{
	const EPLHitWindowSuperArmorLevel RequiredSuperArmor = ActiveHitWindowSettings.DefenseSettings.RequiredSuperArmor;
	if (RequiredSuperArmor == EPLHitWindowSuperArmorLevel::None || !HitActor) return false;

	const UPL_CombatComponent* TargetCombatComponent = FindCombatComponent(HitActor);
	return TargetCombatComponent && TargetCombatComponent->HasSuperArmorAtOrAbove(RequiredSuperArmor);
}

bool UPL_CombatComponent::IsWithinBlockAngle(const AActor* DefenderActor, const AActor* AttackerActor,
	const float BlockAngleDegrees)
{
	if (!DefenderActor || !AttackerActor) return false;

	const FVector ToAttacker = (AttackerActor->GetActorLocation() - DefenderActor->GetActorLocation()).GetSafeNormal();
	const FVector DefenderForward = DefenderActor->GetActorForwardVector().GetSafeNormal();
	const float Dot = FVector::DotProduct(DefenderForward, ToAttacker);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
	return AngleDegrees < BlockAngleDegrees;
}

void UPL_CombatComponent::ApplyDefenseGameplayEffects(AActor* HitActor, const FHitResult& HitResult,
	const bool bWasBlocked, const bool bWasParried, const bool bWasDodged, const bool bHasSuperArmor) const
{
	if (bWasParried)
	{
		ApplyGameplayEffectToActor(GetOwner(), AttackerParriedEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, DefenderParrySuccessEffectClass, 1.f, &HitResult);
	}
	else if (bWasBlocked)
	{
		ApplyGameplayEffectToActor(GetOwner(), AttackerBlockedEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, DefenderBlockedEffectClass, 1.f, &HitResult);
	}

	if (bWasDodged)
	{
		ApplyGameplayEffectToActor(GetOwner(), AttackerDodgedEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, DefenderDodgedEffectClass, 1.f, &HitResult);
	}

	if (bHasSuperArmor)
	{
		ApplyGameplayEffectToActor(GetOwner(), AttackerSuperArmoredEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, DefenderSuperArmoredEffectClass, 1.f, &HitResult);
	}
}

void UPL_CombatComponent::ApplyGameplayEffectToActor(AActor* RecipientActor,
	const TSubclassOf<UGameplayEffect>& GameplayEffectClass, const float EffectLevel, const FHitResult* HitResult) const
{
	if (!RecipientActor || !GameplayEffectClass) return;

	UAbilitySystemComponent* RecipientASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(RecipientActor);
	if (!RecipientASC) return;

	FGameplayEffectContextHandle ContextHandle = RecipientASC->MakeEffectContext();
	ContextHandle.AddSourceObject(GetOwner() ? static_cast<const UObject*>(GetOwner()) : static_cast<const UObject*>(this));

	if (AActor* OwnerActor = GetOwner())
	{
		ContextHandle.AddInstigator(OwnerActor, OwnerActor);
	}

	if (HitResult)
	{
		ContextHandle.AddHitResult(*HitResult);
	}

	const FGameplayEffectSpecHandle SpecHandle =
		RecipientASC->MakeOutgoingSpec(GameplayEffectClass, EffectLevel, ContextHandle);

	if (!SpecHandle.IsValid()) return;

	RecipientASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
}

void UPL_CombatComponent::ExecuteHitWindowGameplayCues(AActor* HitActor, const FHitResult* HitResult,
	const EPLHitWindowCueTriggerTiming TriggerTiming) const
{
	if (!AbilitySystemComponent || ActiveHitWindowSettings.GameplayCuesToExecute.IsEmpty()) return;

	AActor* const OwnerActor = GetOwner();
	UAbilitySystemComponent* const InstigatorASC = AbilitySystemComponent;
	UAbilitySystemComponent* const TargetASC = HitActor
		? UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor)
		: nullptr;

	for (const FPLHitWindowGameplayCue& Cue : ActiveHitWindowSettings.GameplayCuesToExecute)
	{
		if (Cue.TriggerTiming != TriggerTiming) continue;

		if (Cue.CueTag.MatchesTag(TAG_GameplayCue_Hit_CameraShake))
		{
			ExecuteLocalCameraShakeCue(Cue, HitResult);
			continue;
		}

		if (!OwnerActor || !OwnerActor->HasAuthority()) continue;

		switch (Cue.Recipient)
		{
		case EPLHitWindowCueRecipient::Instigator:
			ExecuteGameplayCueOnASC(InstigatorASC, TargetASC, Cue, HitResult);
			break;

		case EPLHitWindowCueRecipient::Target:
			ExecuteGameplayCueOnASC(TargetASC, TargetASC, Cue, HitResult);
			break;

		case EPLHitWindowCueRecipient::Both:
			ExecuteGameplayCueOnASC(InstigatorASC, TargetASC, Cue, HitResult);
			if (TargetASC && TargetASC != InstigatorASC)
			{
				ExecuteGameplayCueOnASC(TargetASC, TargetASC, Cue, HitResult);
			}
			break;

		default:
			break;
		}
	}
}

void UPL_CombatComponent::ExecuteGameplayCueOnASC(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
	const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const
{
	if (!ASC || !Cue.HasValidCueTag()) return;

	AActor* const OwnerActor = GetOwner();

	FGameplayCueParameters Params;
	Params.Instigator = OwnerActor;
	Params.EffectCauser = OwnerActor;
	Params.SourceObject = const_cast<UPL_CombatComponent*>(this);
	Params.Location = GetGameplayCueSpawnLocation(Cue, HitResult);
	Params.Normal = HitResult
		? FVector(HitResult->ImpactNormal)
		: (OwnerActor ? OwnerActor->GetActorForwardVector() : FVector::ForwardVector);
	Params.TargetAttachComponent = GetGameplayCueAttachComponent(ASC, TargetASC, Cue, HitResult);

	ASC->ExecuteGameplayCue(Cue.CueTag, Params);
}

void UPL_CombatComponent::ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const
{
	if (!ShouldExecuteLocalCameraShakeCue()) return;

	AActor* const OwnerActor = GetOwner();

	FGameplayCueParameters Params;
	Params.Instigator = OwnerActor;
	Params.EffectCauser = OwnerActor;
	Params.SourceObject = const_cast<UPL_CombatComponent*>(this);
	Params.Location = GetGameplayCueSpawnLocation(Cue, HitResult);
	Params.Normal = HitResult
		? FVector(HitResult->ImpactNormal)
		: (OwnerActor ? OwnerActor->GetActorForwardVector() : FVector::ForwardVector);
	Params.TargetAttachComponent = GetGameplayCueAttachComponent(AbilitySystemComponent, nullptr, Cue, HitResult);

	AbilitySystemComponent->InvokeGameplayCueEvent(Cue.CueTag, EGameplayCueEvent::Executed, Params);
}

bool UPL_CombatComponent::ShouldExecuteLocalCameraShakeCue() const
{
	if (GetNetMode() == NM_DedicatedServer || !AbilitySystemComponent) return false;

	const AActor* LocalCueActor = AbilitySystemComponent->GetAvatarActor_Direct();
	if (!LocalCueActor)
	{
		LocalCueActor = AbilitySystemComponent->GetOwnerActor();
	}

	const APawn* LocalCuePawn = Cast<APawn>(LocalCueActor);
	if (!LocalCuePawn) return false;

	const bool bIsLocallyControlled = LocalCuePawn->IsLocallyControlled();

	return bIsLocallyControlled;
}

FVector UPL_CombatComponent::GetGameplayCueSpawnLocation(const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const
{
	AActor* const OwnerActor = GetOwner();
	FVector SpawnLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
	if (HitResult)
	{
		switch (Cue.SpawnPoint)
		{
		case EPLHitWindowCueSpawnPoint::OwnerLocation:
			SpawnLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
			break;

		case EPLHitWindowCueSpawnPoint::HitImpactPoint:
			SpawnLocation = HitResult->ImpactPoint;
			break;

		case EPLHitWindowCueSpawnPoint::HitLocation:
			SpawnLocation = HitResult->Location;
			break;

		default:
			break;
		}
	}

	return SpawnLocation + Cue.LocationOffset;
}

USceneComponent* UPL_CombatComponent::GetGameplayCueAttachComponent(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
	const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const
{
	if (!Cue.bAttachToTarget) return nullptr;

	if (ASC == TargetASC && HitResult)
	{
		return HitResult->GetComponent();
	}

	AActor* const OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
}

bool UPL_CombatComponent::BeginHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp,
	FName TraceSocketName, const FPLHitWindowSettings& HitWindowSettings)
{
	if (!NotifyState || !MeshComp) return false;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || MeshComp->GetOwner() != OwnerActor) return false;

	const FObjectKey NotifyKey(NotifyState);
	ActiveHitDetectionWindows.FindOrAdd(NotifyKey) = TraceSocketName;

	int32& ActiveWindowCount = ActiveHitDetectionWindowCounts.FindOrAdd(NotifyKey);
	++ActiveWindowCount;
	++ActiveHitDebugWindowDepth;

	if (!TraceSocketName.IsNone() && !MeshComp->DoesSocketExist(TraceSocketName))
	{
		UE_LOG(LogPLCombatHitDetection, Warning,
			TEXT("[%s] Socket %s was not found. Falling back to mesh location."),
			*GetNameSafe(OwnerActor),
			*TraceSocketName.ToString());
	}

	// Cache active window data used by tick sweeps.
	ActiveHitDebugMesh = MeshComp;
	ActiveHitDebugSocketName = TraceSocketName;
	ActiveHitWindowSettings = HitWindowSettings;
	HitActorsThisWindow.Reset();
	bHasTriggeredHitStopThisWindow = false;

	ApplyActivationTransformEffects();
	ExecuteHitWindowGameplayCues(nullptr, nullptr, EPLHitWindowCueTriggerTiming::OnActivation);

	// Run an initial overlap immediately in case the attack starts inside a target.
	const FTransform InitialTransform = GetHitTraceWorldTransform(MeshComp,
		TraceSocketName, HitWindowSettings.ShapeSettings);

	RunHitDebugQuery(InitialTransform, InitialTransform, false);

	PreviousHitDebugTransform = InitialTransform;
	bHasPreviousHitDebugLocation = true;
	bHitDebugWindowActive = true;

	SetComponentTickEnabled(true);
	return true;
}

void UPL_CombatComponent::EndHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp)
{
	if (!NotifyState || !MeshComp) return;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || MeshComp->GetOwner() != OwnerActor) return;

	// Final sweep to catch the tail end of the notify window.
	if (bHitDebugWindowActive && ActiveHitDebugMesh && bHasPreviousHitDebugLocation)
	{
		const FTransform CurrentTransform = GetHitTraceWorldTransform(
			ActiveHitDebugMesh,
			ActiveHitDebugSocketName,
			ActiveHitWindowSettings.ShapeSettings);

		RunHitDebugQuery(PreviousHitDebugTransform, CurrentTransform, false);
		PreviousHitDebugTransform = CurrentTransform;
	}

	const FObjectKey NotifyKey(NotifyState);

	if (int32* ActiveWindowCount = ActiveHitDetectionWindowCounts.Find(NotifyKey))
	{
		*ActiveWindowCount = FMath::Max(0, *ActiveWindowCount - 1);

		if (*ActiveWindowCount == 0)
		{
			ActiveHitDetectionWindowCounts.Remove(NotifyKey);
			ActiveHitDetectionWindows.Remove(NotifyKey);
		}
	}
	else
	{
		ActiveHitDetectionWindows.Remove(NotifyKey);
	}

	ActiveHitDebugWindowDepth = FMath::Max(0, ActiveHitDebugWindowDepth - 1);

	if (ActiveHitDebugWindowDepth == 0)
	{
		ResetActiveHitDebugWindow();
	}
}
