// Copyright ProjectLogos

#include "Combat/Runtime/PL_CombatHitWindowRuntime.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Combat/Utilities/PL_CombatFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Tag/PL_NativeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogPLCombatHitDetectionRuntime, Log, All);

FPLCombatHitWindowRuntime::FPLCombatHitWindowRuntime(UPL_CombatComponent& InCombatComponent)
	: CombatComponent(InCombatComponent)
{
}

void FPLCombatHitWindowRuntime::Deinitialize()
{
	ActiveHitDetectionWindows.Reset();
	ActiveHitDetectionWindowCounts.Reset();
	LastCombatReferenceActor.Reset();
	ResetActiveHitDebugWindow();
}

void FPLCombatHitWindowRuntime::Tick()
{
	if (!bHitDebugWindowActive) return;
	DebugSweepActiveHitWindow();
}

void FPLCombatHitWindowRuntime::SetLastCombatReferenceActor(AActor* InActor)
{
	LastCombatReferenceActor = InActor && InActor != CombatComponent.GetOwner() ? InActor : nullptr;
}

bool FPLCombatHitWindowRuntime::IsWithinBlockAngle(const AActor* DefenderActor, const AActor* AttackerActor,
	const float BlockAngleDegrees)
{
	if (!DefenderActor || !AttackerActor) return false;

	const FVector ToAttacker = (AttackerActor->GetActorLocation() - DefenderActor->GetActorLocation()).GetSafeNormal();
	const FVector DefenderForward = DefenderActor->GetActorForwardVector().GetSafeNormal();
	const float Dot = FVector::DotProduct(DefenderForward, ToAttacker);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
	return AngleDegrees < BlockAngleDegrees;
}

bool FPLCombatHitWindowRuntime::DoesTransformTimingMatch(const EPLHitWindowTransformTriggerTiming ConfiguredTiming,
	const EPLHitWindowTransformTriggerTiming InvocationTiming)
{
	return ConfiguredTiming == EPLHitWindowTransformTriggerTiming::Both || ConfiguredTiming == InvocationTiming;
}

UPL_CombatComponent* FPLCombatHitWindowRuntime::FindCombatComponent(AActor* Actor)
{
	if (!Actor) return nullptr;

	if (APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(Actor))
	{
		return Character->GetCombatComponent();
	}

	return Actor->FindComponentByClass<UPL_CombatComponent>();
}

void FPLCombatHitWindowRuntime::RunHitDebugQuery(const FTransform& StartTransform, const FTransform& EndTransform,
	const bool bDrawDebug)
{
	UWorld* World = CombatComponent.GetWorld();
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
				FColor::Blue, false, 0.1f, 0, 1.5f);
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

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PLHitDebugSweep), false, CombatComponent.GetOwner());
	QueryParams.AddIgnoredActor(CombatComponent.GetOwner());

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
			if (!HitActor || HitActor == CombatComponent.GetOwner()) continue;

			const TWeakObjectPtr<AActor> WeakHitActor(HitActor);
			if (HitActorsThisWindow.Contains(WeakHitActor)) continue;

			HitActorsThisWindow.Add(WeakHitActor);
			TryApplyHitGameplayEffects(HitActor, Hit);
		}
	}
}

void FPLCombatHitWindowRuntime::DebugSweepActiveHitWindow()
{
	if (!CombatComponent.GetOwner() || !ActiveHitDebugMesh)
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

void FPLCombatHitWindowRuntime::ResetActiveHitDebugWindow()
{
	ActiveHitDebugMesh = nullptr;
	ActiveHitDebugSocketName = NAME_None;
	PreviousHitDebugTransform = FTransform::Identity;
	bHitDebugWindowActive = false;
	bHasPreviousHitDebugLocation = false;
	HitActorsThisWindow.Reset();
	ActiveHitDebugWindowDepth = 0;
	ActiveHitWindowSettings = FPLHitWindowSettings();

	CombatComponent.SetComponentTickEnabled(false);
}

FTransform FPLCombatHitWindowRuntime::GetHitTraceWorldTransform(USkeletalMeshComponent* MeshComp, const FName SocketName,
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

void FPLCombatHitWindowRuntime::TryApplyHitGameplayEffects(AActor* HitActor, const FHitResult& HitResult)
{
	if (!CombatComponent.AbilitySystemComponent || !HitActor || HitActor == CombatComponent.GetOwner()) return;

	const bool bIsAuthority = CombatComponent.GetOwner() && CombatComponent.GetOwner()->HasAuthority();
	if (!bIsAuthority)
	{
		return;
	}

	const bool bWasBlocked = IsAttackBlocked(HitActor);
	const bool bWasParried = bWasBlocked && IsAttackParried(HitActor);
	const bool bWasDodged = IsAttackDodged(HitActor);
	const bool bHasSuperArmor = HasRequiredSuperArmor(HitActor);
	const bool bHasDefenseOutcome = bWasBlocked || bWasParried || bWasDodged || bHasSuperArmor;

	ApplyHitWindowTransformEffects(HitActor, bWasBlocked, bWasDodged, bHasSuperArmor);

	if (bHasDefenseOutcome)
	{
		if (bWasParried)
		{
			LastCombatReferenceActor = HitActor;

			if (UPL_CombatComponent* TargetCombatComponent = FindCombatComponent(HitActor))
			{
				TargetCombatComponent->SetLastCombatReferenceActor(CombatComponent.GetOwner());
			}
		}

		ApplyDefenseGameplayEffects(HitActor, HitResult, bWasBlocked, bWasParried, bWasDodged, bHasSuperArmor);
		return;
	}

	if (!ActiveHitWindowSettings.GameplayEffectsToApply.IsEmpty() && bIsAuthority)
	{
		if (UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor))
		{
			FGameplayEffectContextHandle ContextHandle = CombatComponent.AbilitySystemComponent->MakeEffectContext();
			ContextHandle.AddSourceObject(&CombatComponent);
			ContextHandle.AddHitResult(HitResult);

			for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : ActiveHitWindowSettings.GameplayEffectsToApply)
			{
				if (!GameplayEffectToApply.GameplayEffectClass) continue;

				const FGameplayEffectSpecHandle SpecHandle = CombatComponent.AbilitySystemComponent->MakeOutgoingSpec(
					GameplayEffectToApply.GameplayEffectClass,
					GameplayEffectToApply.EffectLevel,
					ContextHandle);

				if (!SpecHandle.IsValid()) continue;

				CombatComponent.AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
			}
		}
	}

	ExecuteHitWindowGameplayCues(HitActor, &HitResult, EPLHitWindowCueTriggerTiming::OnHit);
}

void FPLCombatHitWindowRuntime::ApplyHitWindowTransformEffects(AActor* HitActor, const bool bWasBlocked,
	const bool bWasDodged, const bool bHasSuperArmor) const
{
	if (!HitActor || HitActor == CombatComponent.GetOwner()) return;
	ApplyHitWindowRotation(HitActor, EPLHitWindowTransformTriggerTiming::OnHit, bWasBlocked, bWasDodged, bHasSuperArmor);
	ApplyHitWindowMovement(HitActor, EPLHitWindowTransformTriggerTiming::OnHit, bWasBlocked, bWasDodged, bHasSuperArmor);
}

void FPLCombatHitWindowRuntime::ApplyActivationTransformEffects() const
{
	ApplyHitWindowRotation(nullptr, EPLHitWindowTransformTriggerTiming::OnActivation, false, false, false);
	ApplyHitWindowMovement(nullptr, EPLHitWindowTransformTriggerTiming::OnActivation, false, false, false);
}

void FPLCombatHitWindowRuntime::ApplyHitWindowMovement(AActor* HitActor,
	const EPLHitWindowTransformTriggerTiming InvocationTiming, const bool bWasBlocked,
	const bool bWasDodged, const bool bHasSuperArmor) const
{
	const FPLHitWindowMovementSettings& MovementSettings = ActiveHitWindowSettings.MovementSettings;
	if (MovementSettings.MoveDirection == EPLHitWindowMoveDirection::None)
	{
		return;
	}

	if (MovementSettings.MoveDirection != EPLHitWindowMoveDirection::KeepCurrentDistance
		&& MovementSettings.MoveDistance <= 0.f)
	{
		return;
	}

	if (!DoesTransformTimingMatch(MovementSettings.TriggerTiming, InvocationTiming))
	{
		return;
	}

	if (InvocationTiming == EPLHitWindowTransformTriggerTiming::OnHit)
	{
		if (bWasDodged || bHasSuperArmor) return;
		if (bWasBlocked && !ActiveHitWindowSettings.DefenseSettings.BlockSettings.bAllowMovementWhenBlocked) return;
	}

	AActor* const OwnerActor = CombatComponent.GetOwner();
	AActor* const TargetActor = HitActor ? HitActor : LastCombatReferenceActor.Get();
	AActor* const ReferenceActor = ResolveTransformReferenceActor(MovementSettings.ReferenceActorSource,
		HitActor, InvocationTiming);

	auto ApplyToRecipient = [this, &MovementSettings, ReferenceActor](AActor* RecipientActor)
	{
		if (!RecipientActor || !ReferenceActor || RecipientActor == ReferenceActor) return;
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
		if (TargetActor && TargetActor != OwnerActor) ApplyToRecipient(TargetActor);
		break;

	default:
		return;
	}
}

void FPLCombatHitWindowRuntime::ApplyMovementToActor(AActor* RecipientActor, AActor* ReferenceActor,
	const FPLHitWindowMovementSettings& MovementSettings) const
{
	if (!RecipientActor || !ReferenceActor) return;

	const FVector ReferenceLocation = ReferenceActor->GetActorLocation();
	const FVector RecipientLocation = RecipientActor->GetActorLocation();

	FVector ReferenceForward = ReferenceActor->GetActorForwardVector();
	ReferenceForward.Z = 0.f;
	ReferenceForward = ReferenceForward.GetSafeNormal();

	if (ReferenceForward.IsNearlyZero()) return;

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
		return;

	FVector NewLocation = ReferenceLocation
		+ (ReferenceForward * TargetForwardProjection)
		+ (ReferenceRight * TargetLateralProjection);
	NewLocation.Z = RecipientLocation.Z;

	RecipientActor->SetActorLocation(NewLocation, MovementSettings.bSweep, nullptr,
		ToTeleportType(MovementSettings.TeleportType));
}

void FPLCombatHitWindowRuntime::ApplyHitWindowRotation(AActor* HitActor,
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
		if (bWasDodged || bHasSuperArmor) return;
		if (bWasBlocked && !ActiveHitWindowSettings.DefenseSettings.BlockSettings.bAllowRotationWhenBlocked) return;
	}

	AActor* const OwnerActor = CombatComponent.GetOwner();
	AActor* const TargetActor = HitActor ? HitActor : LastCombatReferenceActor.Get();
	AActor* const ReferenceActor = ResolveTransformReferenceActor(RotationSettings.ReferenceActorSource,
		HitActor, InvocationTiming);

	auto ApplyToRecipient = [this, &RotationSettings, ReferenceActor](AActor* RecipientActor)
	{
		if (!RecipientActor || !ReferenceActor || RecipientActor == ReferenceActor) return;
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
		if (TargetActor && TargetActor != OwnerActor) ApplyToRecipient(TargetActor);
		break;

	default:
		return;
	}
}

void FPLCombatHitWindowRuntime::ApplyRotationToActor(AActor* RecipientActor, AActor* ReferenceActor,
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

AActor* FPLCombatHitWindowRuntime::ResolveTransformReferenceActor(
	const EPLHitWindowReferenceActorSource ReferenceSource, AActor* HitActor,
	const EPLHitWindowTransformTriggerTiming InvocationTiming) const
{
	switch (ReferenceSource)
	{
	case EPLHitWindowReferenceActorSource::Instigator:
		return CombatComponent.GetOwner();

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

bool FPLCombatHitWindowRuntime::IsAttackBlocked(AActor* HitActor) const
{
	const FPLHitWindowBlockSettings& BlockSettings = ActiveHitWindowSettings.DefenseSettings.BlockSettings;
	if (!BlockSettings.bBlockable || !HitActor) return false;
	if (!CombatComponent.BlockingTag.IsValid()) return false;

	const UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC || !TargetASC->HasMatchingGameplayTag(CombatComponent.BlockingTag))
	{
		return false;
	}

	return IsWithinBlockAngle(HitActor, CombatComponent.GetOwner(), BlockSettings.BlockAngleDegrees);
}

bool FPLCombatHitWindowRuntime::IsAttackParried(AActor* HitActor) const
{
	if (!HitActor) return false;

	const UPL_CombatComponent* TargetCombatComponent = FindCombatComponent(HitActor);
	return TargetCombatComponent && TargetCombatComponent->IsParryingActive();
}

bool FPLCombatHitWindowRuntime::IsAttackDodged(AActor* HitActor) const
{
	if (!ActiveHitWindowSettings.DefenseSettings.DodgeSettings.bDodgeable || !HitActor) return false;
	if (!CombatComponent.DodgingTag.IsValid()) return false;

	const UAbilitySystemComponent* TargetASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(HitActor);
	return TargetASC && TargetASC->HasMatchingGameplayTag(CombatComponent.DodgingTag);
}

bool FPLCombatHitWindowRuntime::HasRequiredSuperArmor(AActor* HitActor) const
{
	const EPLHitWindowSuperArmorLevel RequiredSuperArmor = ActiveHitWindowSettings.DefenseSettings.RequiredSuperArmor;
	if (RequiredSuperArmor == EPLHitWindowSuperArmorLevel::None || !HitActor) return false;

	const UPL_CombatComponent* TargetCombatComponent = FindCombatComponent(HitActor);
	return TargetCombatComponent && TargetCombatComponent->HasSuperArmorAtOrAbove(RequiredSuperArmor);
}

void FPLCombatHitWindowRuntime::ApplyDefenseGameplayEffects(AActor* HitActor, const FHitResult& HitResult,
	const bool bWasBlocked, const bool bWasParried, const bool bWasDodged, const bool bHasSuperArmor) const
{
	if (bWasParried)
	{
		ApplyGameplayEffectToActor(CombatComponent.GetOwner(), CombatComponent.AttackerParriedEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, CombatComponent.DefenderParrySuccessEffectClass, 1.f, &HitResult);
	}
	else if (bWasBlocked)
	{
		ApplyGameplayEffectToActor(CombatComponent.GetOwner(), CombatComponent.AttackerBlockedEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, CombatComponent.DefenderBlockedEffectClass, 1.f, &HitResult);
	}

	if (bWasDodged)
	{
		ApplyGameplayEffectToActor(CombatComponent.GetOwner(), CombatComponent.AttackerDodgedEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, CombatComponent.DefenderDodgedEffectClass, 1.f, &HitResult);
	}

	if (bHasSuperArmor)
	{
		ApplyGameplayEffectToActor(CombatComponent.GetOwner(), CombatComponent.AttackerSuperArmoredEffectClass, 1.f, &HitResult);
		ApplyGameplayEffectToActor(HitActor, CombatComponent.DefenderSuperArmoredEffectClass, 1.f, &HitResult);
	}
}

void FPLCombatHitWindowRuntime::ApplyGameplayEffectToActor(AActor* RecipientActor,
	const TSubclassOf<UGameplayEffect>& GameplayEffectClass, const float EffectLevel, const FHitResult* HitResult) const
{
	if (!RecipientActor || !GameplayEffectClass) return;

	UAbilitySystemComponent* RecipientASC = UPL_CombatFunctionLibrary::GetAbilitySystemComponent(RecipientActor);
	if (!RecipientASC) return;

	FGameplayEffectContextHandle ContextHandle = RecipientASC->MakeEffectContext();
	ContextHandle.AddSourceObject(CombatComponent.GetOwner()
		? static_cast<const UObject*>(CombatComponent.GetOwner())
		: static_cast<const UObject*>(&CombatComponent));

	if (AActor* OwnerActor = CombatComponent.GetOwner())
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

void FPLCombatHitWindowRuntime::ExecuteHitWindowGameplayCues(AActor* HitActor, const FHitResult* HitResult,
	const EPLHitWindowCueTriggerTiming TriggerTiming) const
{
	if (!CombatComponent.AbilitySystemComponent || ActiveHitWindowSettings.GameplayCuesToExecute.IsEmpty()) return;

	AActor* const OwnerActor = CombatComponent.GetOwner();
	UAbilitySystemComponent* const InstigatorASC = CombatComponent.AbilitySystemComponent;
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

void FPLCombatHitWindowRuntime::ExecuteGameplayCueOnASC(UAbilitySystemComponent* ASC, UAbilitySystemComponent* TargetASC,
	const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const
{
	if (!ASC || !Cue.HasValidCueTag()) return;

	AActor* const OwnerActor = CombatComponent.GetOwner();

	FGameplayCueParameters Params;
	Params.Instigator = OwnerActor;
	Params.EffectCauser = OwnerActor;
	Params.SourceObject = const_cast<UPL_CombatComponent*>(&CombatComponent);
	Params.Location = GetGameplayCueSpawnLocation(Cue, HitResult);
	Params.Normal = HitResult
		? FVector(HitResult->ImpactNormal)
		: (OwnerActor ? OwnerActor->GetActorForwardVector() : FVector::ForwardVector);
	Params.TargetAttachComponent = GetGameplayCueAttachComponent(ASC, TargetASC, Cue, HitResult);

	ASC->ExecuteGameplayCue(Cue.CueTag, Params);
}

void FPLCombatHitWindowRuntime::ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue,
	const FHitResult* HitResult) const
{
	if (!ShouldExecuteLocalCameraShakeCue()) return;

	AActor* const OwnerActor = CombatComponent.GetOwner();

	FGameplayCueParameters Params;
	Params.Instigator = OwnerActor;
	Params.EffectCauser = OwnerActor;
	Params.SourceObject = const_cast<UPL_CombatComponent*>(&CombatComponent);
	Params.Location = GetGameplayCueSpawnLocation(Cue, HitResult);
	Params.Normal = HitResult
		? FVector(HitResult->ImpactNormal)
		: (OwnerActor ? OwnerActor->GetActorForwardVector() : FVector::ForwardVector);
	Params.TargetAttachComponent = GetGameplayCueAttachComponent(CombatComponent.AbilitySystemComponent, nullptr, Cue, HitResult);

	CombatComponent.AbilitySystemComponent->InvokeGameplayCueEvent(Cue.CueTag, EGameplayCueEvent::Executed, Params);
}

bool FPLCombatHitWindowRuntime::ShouldExecuteLocalCameraShakeCue() const
{
	if (CombatComponent.GetNetMode() == NM_DedicatedServer || !CombatComponent.AbilitySystemComponent) return false;

	const AActor* LocalCueActor = CombatComponent.AbilitySystemComponent->GetAvatarActor_Direct();
	if (!LocalCueActor)
	{
		LocalCueActor = CombatComponent.AbilitySystemComponent->GetOwnerActor();
	}

	const APawn* LocalCuePawn = Cast<APawn>(LocalCueActor);
	if (!LocalCuePawn) return false;

	return LocalCuePawn->IsLocallyControlled();
}

FVector FPLCombatHitWindowRuntime::GetGameplayCueSpawnLocation(const FPLHitWindowGameplayCue& Cue,
	const FHitResult* HitResult) const
{
	AActor* const OwnerActor = CombatComponent.GetOwner();
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

USceneComponent* FPLCombatHitWindowRuntime::GetGameplayCueAttachComponent(UAbilitySystemComponent* ASC,
	UAbilitySystemComponent* TargetASC, const FPLHitWindowGameplayCue& Cue, const FHitResult* HitResult) const
{
	if (!Cue.bAttachToTarget) return nullptr;

	if (ASC == TargetASC && HitResult)
	{
		return HitResult->GetComponent();
	}

	AActor* const OwnerActor = CombatComponent.GetOwner();
	return OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
}

bool FPLCombatHitWindowRuntime::BeginHitDetectionWindow(const UAnimNotifyState* NotifyState,
	USkeletalMeshComponent* MeshComp, const FName TraceSocketName, const FPLHitWindowSettings& HitWindowSettings)
{
	if (!NotifyState || !MeshComp) return false;

	AActor* OwnerActor = CombatComponent.GetOwner();
	if (!OwnerActor || MeshComp->GetOwner() != OwnerActor) return false;

	const FObjectKey NotifyKey(NotifyState);
	ActiveHitDetectionWindows.FindOrAdd(NotifyKey) = TraceSocketName;

	int32& ActiveWindowCount = ActiveHitDetectionWindowCounts.FindOrAdd(NotifyKey);
	++ActiveWindowCount;
	++ActiveHitDebugWindowDepth;

	if (!TraceSocketName.IsNone() && !MeshComp->DoesSocketExist(TraceSocketName))
	{
		UE_LOG(LogPLCombatHitDetectionRuntime, Warning,
			TEXT("[%s] Socket %s was not found. Falling back to mesh location."),
			*GetNameSafe(OwnerActor),
			*TraceSocketName.ToString());
	}

	ActiveHitDebugMesh = MeshComp;
	ActiveHitDebugSocketName = TraceSocketName;
	ActiveHitWindowSettings = HitWindowSettings;
	HitActorsThisWindow.Reset();

	ApplyActivationTransformEffects();
	ExecuteHitWindowGameplayCues(nullptr, nullptr, EPLHitWindowCueTriggerTiming::OnActivation);

	const FTransform InitialTransform = GetHitTraceWorldTransform(MeshComp,
		TraceSocketName, HitWindowSettings.ShapeSettings);

	RunHitDebugQuery(InitialTransform, InitialTransform, false);

	PreviousHitDebugTransform = InitialTransform;
	bHasPreviousHitDebugLocation = true;
	bHitDebugWindowActive = true;

	CombatComponent.SetComponentTickEnabled(true);
	return true;
}

void FPLCombatHitWindowRuntime::EndHitDetectionWindow(const UAnimNotifyState* NotifyState, USkeletalMeshComponent* MeshComp)
{
	if (!NotifyState || !MeshComp) return;

	AActor* OwnerActor = CombatComponent.GetOwner();
	if (!OwnerActor || MeshComp->GetOwner() != OwnerActor) return;

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
