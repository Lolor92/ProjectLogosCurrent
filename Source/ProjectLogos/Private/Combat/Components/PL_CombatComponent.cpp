// Copyright ProjectLogos

#include "Combat/Components/PL_CombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Character/PL_BaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Component/PL_CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Controller.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "TimerManager.h"
#include "Combat/Utilities/PL_CombatFunctionLibrary.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogPLCombatHitDetection, Log, All);

UPL_CombatComponent::UPL_CombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(false);
}

void UPL_CombatComponent::InitializeCombat(APL_BaseCharacter* InCharacter,
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

	// Movement input is toggled from the replicated gameplay tag count.
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
			if (Tag.IsValid())
			{
				WatchedTags.Add(Tag);
			}
		}
	}

	for (const FGameplayTag& Tag : WatchedTags)
	{
		FDelegateHandle DelegateHandle = AbilitySystemComponent
			->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::AnyCountChange)
			.AddUObject(this, &ThisClass::OnReactionTagChanged);

		TagReactionDelegateHandles.Add(Tag, DelegateHandle);
	}

	// Sync anim bools for tags that already exist when the ASC initializes.
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
		if (Binding.Tags.HasTagExact(Tag))
		{
			SetAnimBool(Binding, IsAnimBoolActive(Binding));
		}
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

FName UPL_CombatComponent::GetRemoveTimerKey(const FPL_TagReactionBinding& Binding,
	const FGameplayTag& TriggeredTag) const
{
	return Binding.Effects.RemoveTimerKey.IsNone()
		? TriggeredTag.GetTagName()
		: Binding.Effects.RemoveTimerKey;
}

void UPL_CombatComponent::ExecuteDelayed(TFunction<void()> Function, const float DelaySeconds,
	FTimerHandle& TimerHandle)
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

FActiveGameplayEffectHandle UPL_CombatComponent::ApplyEffectToSelf(
	const TSubclassOf<UGameplayEffect>& GameplayEffectClass, float Level) const
{
	if (!GameplayEffectClass || !AbilitySystemComponent || !AbilitySystemComponent->IsOwnerActorAuthoritative())
		return FActiveGameplayEffectHandle();

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

void UPL_CombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (!bHitDebugWindowActive)
	{
		return;
	}

	DebugSweepActiveHitWindow();
}

void UPL_CombatComponent::RunHitDebugQuery(const FTransform& StartTransform, const FTransform& EndTransform, bool bDrawDebug)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector StartLocation = StartTransform.GetLocation();
	const FVector EndLocation = EndTransform.GetLocation();
	const FQuat StartRotation = StartTransform.GetRotation();
	const FQuat EndRotation = EndTransform.GetRotation();
	const FQuat SweepRotation = EndTransform.GetRotation();
	const float SphereRadius = FMath::Max(0.f, ActiveHitShapeSettings.SphereRadius);
	const float CapsuleRadius = FMath::Max(0.f, ActiveHitShapeSettings.CapsuleRadius);
	const float CapsuleHalfHeight = FMath::Max(CapsuleRadius, ActiveHitShapeSettings.CapsuleHalfHeight);
	const FVector BoxHalfExtent = ActiveHitShapeSettings.BoxHalfExtent.ComponentMax(FVector::ZeroVector);

	FCollisionShape CollisionShape;
	switch (ActiveHitShapeSettings.ShapeType)
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
		switch (ActiveHitShapeSettings.ShapeType)
		{
		case EPLHitDetectionShapeType::Capsule:
			DrawDebugCapsule(World, EndLocation, CapsuleHalfHeight, CapsuleRadius, SweepRotation,
				FColor::Blue, false, 0.1f, 0, 1.5f);
			break;

		case EPLHitDetectionShapeType::Box:
			DrawDebugBox(World, EndLocation, BoxHalfExtent, SweepRotation, FColor::Blue,
				false, 0.1f, 0, 1.5f);
			break;

		case EPLHitDetectionShapeType::Sphere:
		default:
			DrawDebugSphere(World, EndLocation, SphereRadius, 12, FColor::Blue, false, 0.1f, 0, 1.5f);
			break;
		}

		DrawDebugLine(World, StartLocation, EndLocation, FColor::Cyan, false, 0.1f, 0, 1.0f);
		DrawDebugDirectionalArrow(
			World,
			EndLocation,
			EndLocation + (EndTransform.GetUnitAxis(EAxis::X) * 30.f),
			12.f,
			FColor::Green,
			false,
			0.1f,
			0,
			1.0f);
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PLHitDebugSweep), false, GetOwner());
	QueryParams.AddIgnoredActor(GetOwner());

	const double RotationDeltaDegrees = FMath::RadiansToDegrees(StartRotation.AngularDistance(EndRotation));
	const double LargestShapeExtent = FMath::Max(
		FMath::Max(static_cast<double>(SphereRadius), static_cast<double>(CapsuleHalfHeight)),
		BoxHalfExtent.GetMax());
	const double DistanceStepSize = FMath::Max(20.0, LargestShapeExtent * 0.75);
	const int32 DistanceSteps = FMath::Clamp(FMath::CeilToInt(FVector::Distance(StartLocation, EndLocation) / DistanceStepSize), 1, 6);
	const int32 RotationSteps = FMath::Clamp(FMath::CeilToInt(RotationDeltaDegrees / 15.f), 1, 6);
	const int32 NumSubsteps = FMath::Clamp(FMath::Max(DistanceSteps, RotationSteps), 1, 6);

	for (int32 StepIndex = 1; StepIndex <= NumSubsteps; ++StepIndex)
	{
		const float PrevAlpha = static_cast<float>(StepIndex - 1) / static_cast<float>(NumSubsteps);
		const float CurrAlpha = static_cast<float>(StepIndex) / static_cast<float>(NumSubsteps);

		const FVector SegmentStartLocation = FMath::Lerp(StartLocation, EndLocation, PrevAlpha);
		const FVector SegmentEndLocation = FMath::Lerp(StartLocation, EndLocation, CurrAlpha);
		const FQuat SegmentRotation = FQuat::Slerp(StartRotation, EndRotation, CurrAlpha).GetNormalized();

		TArray<FHitResult> HitResults;
		World->SweepMultiByObjectType(
			HitResults,
			SegmentStartLocation,
			SegmentEndLocation,
			SegmentRotation,
			ObjectQueryParams,
			CollisionShape,
			QueryParams);

		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || HitActor == GetOwner())
			{
				continue;
			}

			const TWeakObjectPtr<AActor> WeakHitActor(HitActor);
			if (HitActorsThisWindow.Contains(WeakHitActor))
			{
				continue;
			}

			HitActorsThisWindow.Add(WeakHitActor);

			TryApplyHitGameplayEffects(HitActor, Hit);

			UE_LOG(LogPLCombatHitDetection, Warning,
				TEXT("[%s] Debug sweep hit %s | Socket=%s | Prev=%s | Curr=%s | Substeps=%d"),
				*GetNameSafe(GetOwner()),
				*GetNameSafe(HitActor),
				ActiveHitDebugSocketName.IsNone() ? TEXT("None") : *ActiveHitDebugSocketName.ToString(),
				*StartLocation.ToString(),
				*EndLocation.ToString(),
				NumSubsteps);
		}
	}
}

void UPL_CombatComponent::DebugSweepActiveHitWindow()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveHitDebugMesh)
	{
		ResetActiveHitDebugWindow();
		return;
	}

	const FTransform CurrentTransform = GetHitTraceWorldTransform(
		ActiveHitDebugMesh,
		ActiveHitDebugSocketName,
		ActiveHitShapeSettings);

	if (!bHasPreviousHitDebugLocation)
	{
		PreviousHitDebugTransform = CurrentTransform;
		bHasPreviousHitDebugLocation = true;
	}

	RunHitDebugQuery(PreviousHitDebugTransform, CurrentTransform, true);
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
	ActiveHitShapeSettings = FPLHitWindowShapeSettings();
	ActiveGameplayEffectsToApply.Reset();
	SetComponentTickEnabled(false);
}

FTransform UPL_CombatComponent::GetHitTraceWorldTransform(USkeletalMeshComponent* MeshComp, FName SocketName,
	const FPLHitWindowShapeSettings& HitShapeSettings) const
{
	if (!MeshComp)
	{
		return FTransform::Identity;
	}

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
	if (!AbilitySystemComponent || !HitActor || HitActor == GetOwner() || ActiveGameplayEffectsToApply.IsEmpty())
	{
		return;
	}
	
	UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC)
	{
		return;
	}

	FGameplayEffectContextHandle ContextHandle = AbilitySystemComponent->MakeEffectContext();
	ContextHandle.AddSourceObject(this);
	ContextHandle.AddHitResult(HitResult);

	for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : ActiveGameplayEffectsToApply)
	{
		if (!GameplayEffectToApply.GameplayEffectClass)
		{
			continue;
		}

		const FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(
			GameplayEffectToApply.GameplayEffectClass,
			GameplayEffectToApply.EffectLevel,
			ContextHandle);

		if (!SpecHandle.IsValid())
		{
			continue;
		}

		AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	}
}

bool UPL_CombatComponent::BeginHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp,
	FName DebugSocketName, const FPLHitWindowShapeSettings& HitShapeSettings,
	const TArray<FPLHitWindowGameplayEffect>& GameplayEffectsToApply)
{
	if (!NotifyState || !MeshComp) return false;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || MeshComp->GetOwner() != OwnerActor) return false;
	if (!OwnerActor->HasAuthority()) return false;
	
	const FObjectKey NotifyKey(NotifyState);
	ActiveHitDetectionWindows.FindOrAdd(NotifyKey) = DebugSocketName;

	int32& ActiveWindowCount = ActiveHitDetectionWindowCounts.FindOrAdd(NotifyKey);
	++ActiveWindowCount;
	++ActiveHitDebugWindowDepth;

	const UPL_CharacterMovementComponent* MoveComp =
		Cast<UPL_CharacterMovementComponent>(OwningCharacter ? OwningCharacter->GetCharacterMovement() : nullptr);

	const FString SocketName = DebugSocketName.IsNone() ? TEXT("None") : DebugSocketName.ToString();
	
	if (!DebugSocketName.IsNone() && !MeshComp->DoesSocketExist(DebugSocketName))
	{
		UE_LOG(LogPLCombatHitDetection, Warning,
			TEXT("[%s] Socket %s was not found. Falling back to mesh location."),
			*GetNameSafe(OwnerActor),
			*DebugSocketName.ToString());
	}

	ActiveHitDebugMesh = MeshComp;
	ActiveHitDebugSocketName = DebugSocketName;
	ActiveHitShapeSettings = HitShapeSettings;
	ActiveGameplayEffectsToApply = GameplayEffectsToApply;
	HitActorsThisWindow.Reset();

	const FTransform InitialTransform = GetHitTraceWorldTransform(MeshComp, DebugSocketName, HitShapeSettings);
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
	if (!OwnerActor->HasAuthority()) return;

	if (bHitDebugWindowActive && ActiveHitDebugMesh && bHasPreviousHitDebugLocation)
	{
		const FTransform CurrentTransform = GetHitTraceWorldTransform(
			ActiveHitDebugMesh,
			ActiveHitDebugSocketName,
			ActiveHitShapeSettings);

		RunHitDebugQuery(PreviousHitDebugTransform, CurrentTransform, false);
		PreviousHitDebugTransform = CurrentTransform;
	}

	const FObjectKey NotifyKey(NotifyState);
	const FName SocketName = ActiveHitDetectionWindows.Contains(NotifyKey)
		? ActiveHitDetectionWindows.FindChecked(NotifyKey)
		: NAME_None;

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

	const UPL_CharacterMovementComponent* MoveComp =
		Cast<UPL_CharacterMovementComponent>(OwningCharacter ? OwningCharacter->GetCharacterMovement() : nullptr);

	const FString SocketNameString = SocketName.IsNone() ? TEXT("None") : SocketName.ToString();
	
	if (ActiveHitDebugWindowDepth == 0)
	{
		ResetActiveHitDebugWindow();
	}
}
