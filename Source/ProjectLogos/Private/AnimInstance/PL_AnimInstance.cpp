#include "AnimInstance/PL_AnimInstance.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Component/PL_CharacterMovementComponent.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/Ability/PL_GameplayAbility.h"
#include "Kismet/KismetMathLibrary.h"

void UPL_AnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	APawn* PawnOwner = TryGetPawnOwner();
	if (!PawnOwner) return;

	Character = Cast<APL_BaseCharacter>(PawnOwner);
	if (!Character) return;

	// Cache the common runtime references used every anim tick.
	CharacterMovementComponent = Character->GetCharacterMovement();
	AbilitySystemComponent = Character->GetAbilitySystemComponent();
}

void UPL_AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Character || !CharacterMovementComponent) return;

	// Movement values consumed by the AnimBP.
	bIsAccelerating = CharacterMovementComponent->GetCurrentAcceleration().Size() > 0.f;
	GroundSpeed = UKismetMathLibrary::VSizeXY(CharacterMovementComponent->Velocity);
	IsAirBorne = CharacterMovementComponent->IsFalling();

	// Rotation offsets for aim/movement blending.
	AimRotation = Character->GetBaseAimRotation();

	if (!Character->GetVelocity().IsNearlyZero())
	{
		MovementRotation = UKismetMathLibrary::MakeRotFromX(Character->GetVelocity());
		MovementOffsetYaw = UKismetMathLibrary::NormalizedDeltaRotator(MovementRotation, AimRotation).Yaw;
	}
	else
	{
		MovementOffsetYaw = 0.f;
	}

	SuppressDuplicatePredictedReactionMontage();

	// The owning client publishes ability-driven anim state for everyone else.
	UpdateAbilityAnimReplication();

	float Percent = 0.f;
	UPL_GameplayAbility* Ability = nullptr;
	const bool bHasAbilityContext = GetAbilityPercentMontagePlayed(Percent, Ability);

	// Root-motion abilities own facing until root motion is released or lower-body blending starts.
	const bool bAbilityOwnsRotation = bHasAbilityContext && bRootMotionEnabled && !bShouldBlendLowerBody;

	const bool bAllowControllerYaw = !bAbilityOwnsRotation && bIsAccelerating;

	Character->bUseControllerRotationYaw = bAllowControllerYaw;
	bUseControllerRotationYaw = bAllowControllerYaw;
}

void UPL_AnimInstance::SuppressDuplicatePredictedReactionMontage()
{
	if (!Character || Character->HasAuthority()) return;

	UPL_CombatComponent* CombatComponent = Character->GetCombatComponent();
	if (!CombatComponent) return;

	UAnimMontage* ActiveMontage = GetCurrentActiveMontage();
	if (!ActiveMontage) return;

	const float CurrentPositionSeconds = Montage_GetPosition(ActiveMontage);

	float CorrectedPositionSeconds = 0.f;
	bool bShouldStop = false;

	if (!CombatComponent->GetLocalHitFeedbackRuntime().TryCorrectPredictedReactionMontage(
		ActiveMontage,
		CurrentPositionSeconds,
		CorrectedPositionSeconds,
		bShouldStop))
	{
		return;
	}

	if (bShouldStop)
	{
		Montage_Stop(0.f, ActiveMontage);

		UE_LOG(LogTemp, Warning, TEXT("Stopped duplicate predicted reaction montage. Character=%s Montage=%s"),
			*GetNameSafe(Character),
			*GetNameSafe(ActiveMontage));

		return;
	}

	Montage_SetPosition(ActiveMontage, CorrectedPositionSeconds);

	UE_LOG(LogTemp, Warning, TEXT("Corrected duplicate predicted reaction montage. Character=%s Montage=%s Position=%.3f"),
		*GetNameSafe(Character),
		*GetNameSafe(ActiveMontage),
		CorrectedPositionSeconds);
}

void UPL_AnimInstance::UpdateAbilityAnimReplication()
{
	if (!Character || !Character->IsLocallyControlled()) return;

	float Percent = 0.f;
	UPL_GameplayAbility* Ability = nullptr;
	const bool bHasAbilityContext = GetAbilityPercentMontagePlayed(Percent, Ability);

	// No active montage means no ability override should remain.
	if (!bHasAbilityContext || !Ability)
	{
		LastTrackedAbility = nullptr;
		LastTrackedAbilityActivationSequenceId = 0;
		LastTrackedMontage = nullptr;
		bReleasedRootMotionThisMontage = false;
		Character->ResetAbilityAnimState();
		return;
	}

	const UAnimMontage* CurrentMontage = Ability->GetCurrentMontage();
	const uint32 CurrentActivationSequenceId = Ability->GetActivationSequenceId();

	// Retriggering the same instanced ability can replay the same montage asset, so
	// reset per-run state when either the montage or the activation sequence changes.
	if (Ability != LastTrackedAbility || CurrentActivationSequenceId != LastTrackedAbilityActivationSequenceId
		|| CurrentMontage != LastTrackedMontage)
	{
		LastTrackedAbility = Ability;
		LastTrackedAbilityActivationSequenceId = CurrentActivationSequenceId;
		LastTrackedMontage = CurrentMontage;
		bReleasedRootMotionThisMontage = false;
	}

	const bool bReachedReleasePoint =
		!Ability->MontageLockout.bUseMontageProgressLockout ||
		(Percent >= Ability->MontageLockout.MontageProgressBeforeInterrupt);

	// Movement input after the release point allows lower-body blending and stops root motion.
	const bool bHasMovementInput = CharacterMovementComponent &&
		!CharacterMovementComponent->GetCurrentAcceleration().IsNearlyZero(0.01f);

	if (bReachedReleasePoint && bHasMovementInput)
	{
		bReleasedRootMotionThisMontage = true;
	}
	
	// Collision can stop displacement early, but movement stays locked until release.
	if (Ability->IsRootMotionStoppedByCollision())
	{
		bReleasedRootMotionThisMontage = true;
	}
	
	FRepAbilityAnimState DesiredState;
	DesiredState.bCanBlendMontage = bReachedReleasePoint;
	DesiredState.bShouldBlendLowerBody = bReachedReleasePoint && bHasMovementInput;
	DesiredState.bRootMotionEnabled = !bReleasedRootMotionThisMontage;
	DesiredState.bMovementInputSuppressed = !bReachedReleasePoint;

	if (UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(CharacterMovementComponent))
	{
		// Apply locally now so the owning client predicts the same state it replicates.
		MoveComp->SetAbilityRootMotionSuppressed(!DesiredState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(DesiredState.bMovementInputSuppressed);
	}

	if (Character->GetAbilityAnimState() == DesiredState) return;

	Character->SetAbilityAnimState(DesiredState);
}

bool UPL_AnimInstance::GetAbilityPercentMontagePlayed(float& OutPercent, UPL_GameplayAbility*& OutAbility)
{
	OutPercent = 0.f;
	OutAbility = nullptr;

	if (!Character) return false;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentSafe();
	if (!ASC) return false;

	// GAS tracks which ability owns the currently playing montage.
	UGameplayAbility* ActiveAbility = ASC->GetAnimatingAbility();
	if (!ActiveAbility) return false;

	const UAnimMontage* CurrentMontage = ActiveAbility->GetCurrentMontage();
	if (!CurrentMontage) return false;

	UPL_GameplayAbility* Ability = Cast<UPL_GameplayAbility>(ActiveAbility);
	if (!Ability) return false;

	const float MontageLength = CurrentMontage->GetPlayLength();
	if (MontageLength <= 0.f) return false;

	const float CurrentPosition = Montage_GetPosition(CurrentMontage);
	OutPercent = (CurrentPosition / MontageLength) * 100.f;
	OutAbility = Ability;

	return true;
}

UAbilitySystemComponent* UPL_AnimInstance::GetAbilitySystemComponentSafe()
{
	if (!AbilitySystemComponent && Character)
	{
		AbilitySystemComponent = Character->GetAbilitySystemComponent();
	}
	
	return AbilitySystemComponent;
}
