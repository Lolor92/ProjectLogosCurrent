// Copyright ProjectLogos

#include "GAS/Ability/PL_GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Component/PL_CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

UPL_GameplayAbility::UPL_GameplayAbility()
{
	// Default setup for locally predicted, per-character ability instances.
	ReplicationPolicy  = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy  = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	bServerRespectsRemoteAbilityCancellation = false;
	bRetriggerInstancedAbility = true;
}

bool UPL_GameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// Keep activation checks side-effect free; ActivateAbility should only run after this passes.
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)
		&& CanUseAbility(ActorInfo);
}

void UPL_GameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Per-run combo/collision state.
	ActivationSequenceId = (ActivationSequenceId == MAX_uint32) ? 1u : (ActivationSequenceId + 1u);
	ResetComboWindow();
	
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	InterruptOtherActiveAbilities();
	
	bRootMotionStoppedByCollision = false;
	RotateAvatarToControllerYawOnActivate();

	// Collision binding is per activation and removed when the ability ends.
	BindRootMotionCollisionStop();
	OpenComboWindow();
}

void UPL_GameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Restore ability-driven animation and movement state.
	if (APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(GetAvatarActorFromActorInfo()))
		Character->ResetAbilityAnimState();

	// Avoid leaving the ability instance bound to an old capsule.
	if (CachedCapsule)
	{
		CachedCapsule->OnComponentHit.RemoveDynamic(this, &ThisClass::OnCapsuleHit);
		CachedCapsule = nullptr;
	}

	bRootMotionStoppedByCollision = false;

	ResetComboWindow();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UPL_GameplayAbility::RotateAvatarToControllerYawOnActivate() const
{
	if (!bRotateToControllerYawOnActivate) return;

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info) return;

	ACharacter* Character = Cast<ACharacter>(Info->AvatarActor.Get());
	if (!Character) return;

	if (!Info->IsLocallyControlled() && !Info->IsNetAuthority()) return;

	// GAS actor info may not have a PlayerController, depending on ASC ownership.
	AController* Controller = Info->PlayerController.IsValid()
		? Info->PlayerController.Get() : Character->GetController();
	if (!Controller) return;

	FRotator NewRot = Character->GetActorRotation();
	NewRot.Yaw = Controller->GetControlRotation().Yaw;
	Character->SetActorRotation(NewRot, ETeleportType::ResetPhysics);
}

void UPL_GameplayAbility::RotateAvatarToFaceActorOnActivate(AActor* TargetActor) const
{
	if (!TargetActor) return;

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info) return;

	APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(Info->AvatarActor.Get());
	if (!Character) return;

	if (!Info->IsLocallyControlled() && !Info->IsNetAuthority()) return;

	Character->RotateToFaceActor(TargetActor);
}

void UPL_GameplayAbility::BindRootMotionCollisionStop()
{
	if (!bStopRootMotionOnCollision) return;

	// Only the active ability listens for capsule hits.
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	CachedCapsule = Character->GetCapsuleComponent();
	if (!CachedCapsule) return;

	CachedCapsule->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnCapsuleHit);
}

bool UPL_GameplayAbility::CanUseAbility(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (bInterruptOtherAbilitiesOnActivate) return true;

	const APL_BaseCharacter* Character = ActorInfo ? Cast<APL_BaseCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character) return true;

	const UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystemComponent) return true;

	// The montage owner decides whether another activation can interrupt it.
	const UGameplayAbility* ActiveAbility = AbilitySystemComponent->GetAnimatingAbility();
	if (!ActiveAbility) return true;

	const UAnimMontage* ActiveMontage = ActiveAbility->GetCurrentMontage();
	if (!ActiveMontage) return true;

	// Non-PL abilities do not participate in this montage lockout rule.
	const UPL_GameplayAbility* ActivePLAbility = Cast<UPL_GameplayAbility>(ActiveAbility);
	if (!ActivePLAbility || !ActivePLAbility->MontageLockout.bUseMontageProgressLockout) return true;

	// Locked abilities can only be interrupted after their configured montage percent.
	USkeletalMeshComponent* MeshComp = Character->GetMesh();
	UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	const float MontageLength = ActiveMontage->GetPlayLength();
	if (!AnimInstance || MontageLength <= 0.f) return false;

	const float Percent = (AnimInstance->Montage_GetPosition(ActiveMontage) / MontageLength) * 100.f;
	return Percent >= ActivePLAbility->MontageLockout.MontageProgressBeforeInterrupt;
}

void UPL_GameplayAbility::InterruptOtherActiveAbilities() const
{
	if (!bInterruptOtherAbilitiesOnActivate) return;

	UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo();
	if (!AbilitySystemComponent) return;

	AbilitySystemComponent->CancelAllAbilities(const_cast<ThisClass*>(this));
}

bool UPL_GameplayAbility::ShouldStopRootMotionFromCapsuleHit(
	const ACharacter* Character,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	const FHitResult& Hit
) const
{
	if (!Character || !OtherActor || OtherActor == Character)
	{
		return false;
	}

	// Keep this focused on character/pawn body contact.
	// This prevents random side brushes against non-pawn props from killing root motion.
	if (!OtherActor->IsA<APawn>())
	{
		return false;
	}

	// If we have a component, prefer capsule/mesh body contact.
	// If OtherComp is null, still allow the angle test as fallback.
	if (OtherComp)
	{
		const bool bIsCapsule = OtherComp->IsA<UCapsuleComponent>();
		const bool bIsMesh = OtherComp->IsA<USkeletalMeshComponent>();

		if (!bIsCapsule && !bIsMesh)
		{
			return false;
		}
	}

	FVector Forward = Character->GetActorForwardVector();
	Forward.Z = 0.f;

	if (!Forward.Normalize())
	{
		return false;
	}

	const FVector Start = Character->GetActorLocation();

	// Normal blocking hits usually have a good impact point.
	// Initial overlaps / penetration can give a bad point, so fall back to actor location.
	FVector HitPoint = Hit.ImpactPoint;

	if (Hit.bStartPenetrating || HitPoint.IsNearlyZero())
	{
		HitPoint = OtherActor->GetActorLocation();
	}

	FVector ToHit = HitPoint - Start;
	ToHit.Z = 0.f;

	if (!ToHit.Normalize())
	{
		return false;
	}

	const float ClampedAngleDegrees = FMath::Clamp(
		RootMotionCollisionForwardAngleDegrees,
		0.f,
		180.f
	);

	const float MinForwardDot = FMath::Cos(FMath::DegreesToRadians(ClampedAngleDegrees));
	const float ForwardDot = FVector::DotProduct(Forward, ToHit);

	return ForwardDot >= MinForwardDot;
}

void UPL_GameplayAbility::OnCapsuleHit(
	UPrimitiveComponent* HitComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit
)
{
	if (!OtherActor || OtherActor == GetAvatarActorFromActorInfo())
	{
		return;
	}

	APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	if (!ShouldStopRootMotionFromCapsuleHit(Character, OtherActor, OtherComp, Hit))
	{
		return;
	}

	bRootMotionStoppedByCollision = true;

	// Stop montage displacement immediately, but keep input locked until release.
	if (UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		MoveComp->SetAbilityRootMotionSuppressed(true);
	}

	FRepAbilityAnimState AbilityAnimState = Character->GetAbilityAnimState();
	AbilityAnimState.bRootMotionEnabled = false;
	AbilityAnimState.bMovementInputSuppressed = MontageLockout.bUseMontageProgressLockout;

	// Collision should not skip the configured movement release point.
	if (AbilityAnimState.bMovementInputSuppressed)
	{
		const UAnimMontage* ActiveMontage = GetCurrentMontage();
		USkeletalMeshComponent* MeshComp = Character->GetMesh();
		UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
		const float MontageLength = ActiveMontage ? ActiveMontage->GetPlayLength() : 0.f;

		if (AnimInstance && MontageLength > 0.f)
		{
			const float Percent =
				(AnimInstance->Montage_GetPosition(ActiveMontage) / MontageLength) * 100.f;

			AbilityAnimState.bMovementInputSuppressed =
				Percent < MontageLockout.MontageProgressBeforeInterrupt;
		}
	}

	// Publish the state through the character so prediction and replicated anim state stay aligned.
	Character->SetAbilityAnimState(AbilityAnimState);
}

void UPL_GameplayAbility::OpenComboWindow()
{
	CloseComboWindow();

	if (!ComboAbilityClass || ComboWindowDuration <= 0.f) return;

	bComboWindowOpen = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ComboWindowTimerHandle, this,
			&ThisClass::CloseComboWindow, ComboWindowDuration, false);
	}
}

void UPL_GameplayAbility::CloseComboWindow()
{
	bComboWindowOpen = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboWindowTimerHandle);
	}
}

void UPL_GameplayAbility::ResetComboWindow()
{
	CloseComboWindow();
}
