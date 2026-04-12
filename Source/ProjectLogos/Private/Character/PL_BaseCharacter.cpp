#include "Character/PL_BaseCharacter.h"
#include "AbilitySystemComponent.h"
#include "AnimInstance/PL_AnimInstance.h"
#include "Component/PL_CharacterMovementComponent.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/PL_PlayerState.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

namespace
{
	bool AreHitStopStatesEqual(const FRepHitStopState& Left, const FRepHitStopState& Right)
	{
		return Left.bActive == Right.bActive
			&& FMath::IsNearlyEqual(Left.TimeScale, Right.TimeScale);
	}
}

APL_BaseCharacter::APL_BaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPL_CharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CombatComponent = CreateDefaultSubobject<UPL_CombatComponent>(TEXT("CombatComponent"));

	// Collision defaults.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);

	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetGenerateOverlapEvents(true);

	// Animation and spawn defaults.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Base movement tuning.
	GetCharacterMovement()->MaxWalkSpeed = 400.0f;
	GetCharacterMovement()->MaxCustomMovementSpeed = 400.0f;

	GetCharacterMovement()->MaxJumpApexAttemptsPerSimulation = 1;
	GetCharacterMovement()->JumpZVelocity = 950.f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->GravityScale = 3.f;

	// Ability montages manage rotation when needed.
	bUseControllerRotationYaw = false;

	AbilityAnimState = FRepAbilityAnimState();
	HitStopState = FRepHitStopState();
}

void APL_BaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CombatComponent)
	{
		CombatComponent->DeinitializeCombat();
	}

	Super::EndPlay(EndPlayReason);
}

void APL_BaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APL_BaseCharacter, AbilityAnimState);
	DOREPLIFETIME(APL_BaseCharacter, HitStopState);
}

UAbilitySystemComponent* APL_BaseCharacter::GetAbilitySystemComponent() const
{
	if (AbilitySystemComponent) return AbilitySystemComponent;

	// Fallback for actors that still own an ASC component directly.
	if (UAbilitySystemComponent* CharacterAbilitySystem = FindComponentByClass<UAbilitySystemComponent>())
	{
		return CharacterAbilitySystem;
	}

	const APL_PlayerState* PL_PlayerState = GetPlayerState<APL_PlayerState>();
	return PL_PlayerState ? PL_PlayerState->GetAbilitySystemComponent() : nullptr;
}

UAttributeSet* APL_BaseCharacter::GetAttributeSet() const
{
	if (AttributeSet) return AttributeSet;

	const APL_PlayerState* PL_PlayerState = GetPlayerState<APL_PlayerState>();
	return PL_PlayerState ? PL_PlayerState->GetAttributeSet() : nullptr;
}

void APL_BaseCharacter::SetAbilityAnimState(const FRepAbilityAnimState& NewState)
{
	if (AbilityAnimState == NewState) return;

	AbilityAnimState = NewState;
	ApplyAbilityAnimState(NewState);

	UWorld* World = GetWorld();
	if (!HasAuthority() && World && !World->bIsTearingDown)
	{
		ServerSetAbilityAnimState(NewState);
	}
}

void APL_BaseCharacter::ServerSetAbilityAnimState_Implementation(const FRepAbilityAnimState& NewState)
{
	if (AbilityAnimState == NewState) return;

	AbilityAnimState = NewState;
	ApplyAbilityAnimState(NewState);
}

void APL_BaseCharacter::ResetAbilityAnimState()
{
	// Clear predicted movement state before publishing the replicated default.
	if (UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(GetCharacterMovement()))
	{
		MoveComp->SetAbilityRootMotionSuppressed(false);
		MoveComp->SetAbilityMovementInputSuppressed(false);
	}

	FRepAbilityAnimState DefaultState;
	DefaultState.bCanBlendMontage = false;
	DefaultState.bShouldBlendLowerBody = false;
	DefaultState.bRootMotionEnabled = true;
	DefaultState.bMovementInputSuppressed = false;

	SetAbilityAnimState(DefaultState);
}

void APL_BaseCharacter::SetHitStopState(const FRepHitStopState& NewState)
{
	if (AreHitStopStatesEqual(HitStopState, NewState)) return;

	HitStopState = NewState;
	ApplyHitStopState(NewState);

	UWorld* World = GetWorld();
	if (!HasAuthority() && World && !World->bIsTearingDown)
	{
		ServerSetHitStopState(NewState);
	}
}

void APL_BaseCharacter::ServerSetHitStopState_Implementation(const FRepHitStopState& NewState)
{
	if (AreHitStopStatesEqual(HitStopState, NewState)) return;

	HitStopState = NewState;
	ApplyHitStopState(NewState);
}

void APL_BaseCharacter::ClearHitStopState()
{
	FRepHitStopState DefaultState;
	DefaultState.bActive = false;
	DefaultState.TimeScale = 0.f;
	SetHitStopState(DefaultState);
}

void APL_BaseCharacter::InitializeDefaultAttributes()
{
	// Apply stuff later
}

void APL_BaseCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (CombatComponent)
	{
		CombatComponent->HandleMovementModeChanged(GetCharacterMovement()->MovementMode);
	}
}

void APL_BaseCharacter::OnRep_AbilityAnimState()
{
	ApplyAbilityAnimState(AbilityAnimState);
}

void APL_BaseCharacter::OnRep_HitStopState()
{
	ApplyHitStopState(HitStopState);
}

void APL_BaseCharacter::ApplyAbilityAnimState(const FRepAbilityAnimState& NewState)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) return;

	UPL_AnimInstance* AnimInstance = Cast<UPL_AnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	AnimInstance->bCanBlendMontage      = NewState.bCanBlendMontage;
	AnimInstance->bShouldBlendLowerBody = NewState.bShouldBlendLowerBody;

	// Authority and owning clients drive predicted root-motion flags through the CMC.
	UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(GetCharacterMovement());
	const bool bUsePredictedRootMotionState = MoveComp && (HasAuthority() || IsLocallyControlled());
	if (bUsePredictedRootMotionState)
	{
		MoveComp->SetAbilityRootMotionSuppressed(!NewState.bRootMotionEnabled);
		MoveComp->SetAbilityMovementInputSuppressed(NewState.bMovementInputSuppressed);
		MoveComp->RefreshAbilityRootMotionMode();
		return;
	}

	// Simulated proxies consume the replicated anim state directly.
	AnimInstance->bRootMotionEnabled = NewState.bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(
		NewState.bRootMotionEnabled
			? ERootMotionMode::RootMotionFromMontagesOnly
			: ERootMotionMode::IgnoreRootMotion);
}

void APL_BaseCharacter::ApplyHitStopState(const FRepHitStopState& NewState)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) return;

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance) return;

	if (UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(GetCharacterMovement()))
	{
		MoveComp->SetHitStopRootMotionSuppressed(NewState.bActive);
	}

	if (NewState.bActive)
	{
		if (!HitStopPausedMontage)
		{
			if (UAnimMontage* ActiveMontage = AnimInstance->GetCurrentActiveMontage())
			{
				HitStopPausedMontage = ActiveMontage;
				AnimInstance->Montage_Pause(HitStopPausedMontage);
			}
		}

		return;
	}

	if (HitStopPausedMontage)
	{
		if (AnimInstance->Montage_IsActive(HitStopPausedMontage))
		{
			AnimInstance->Montage_Resume(HitStopPausedMontage);
		}

		HitStopPausedMontage = nullptr;
	}
}
