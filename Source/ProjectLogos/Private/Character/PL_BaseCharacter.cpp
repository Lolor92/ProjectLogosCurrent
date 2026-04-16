#include "Character/PL_BaseCharacter.h"
#include "AbilitySystemComponent.h"
#include "AnimInstance/PL_AnimInstance.h"
#include "Component/PL_CharacterMovementComponent.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Net/UnrealNetwork.h"
#include "Player/PL_PlayerState.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"

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
	GetCharacterMovement()->JumpZVelocity = 850.f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->GravityScale = 2.5f;

	// Ability montages manage rotation when needed.
	bUseControllerRotationYaw = false;

	AbilityAnimState = FRepAbilityAnimState();
}

void APL_BaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APL_BaseCharacter, AbilityAnimState);
}

void APL_BaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CombatComponent)
	{
		CombatComponent->DeinitializeCombat();
	}

	Super::EndPlay(EndPlayReason);
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

void APL_BaseCharacter::ApplyHitStop(float Duration, float TimeScale)
{
	UWorld* World = GetWorld();
	if (!World) return;

	Duration = FMath::Max(0.f, Duration);
	if (Duration <= 0.f) return;
	
	UCharacterMovementComponent* MovementComp = nullptr;
	AController* OwnerController = nullptr;
	EMovementMode PreviousMovementMode = MOVE_None;
	uint8 PreviousCustomMovementMode = 0;
	bool bChangedControllerMoveInput = false;
	
	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		MovementComp = CharacterOwner->GetCharacterMovement();
		if (MovementComp)
		{
			PreviousMovementMode = MovementComp->MovementMode;
			PreviousCustomMovementMode = MovementComp->CustomMovementMode;

			// Stop and hard-lock movement during hit-stop.
			MovementComp->StopMovementImmediately();
			MovementComp->DisableMovement();
		}

		OwnerController = CharacterOwner->GetController();
		if (OwnerController && !OwnerController->IsMoveInputIgnored())
		{
			OwnerController->SetIgnoreMoveInput(true);
			bChangedControllerMoveInput = true;
		}
	}

	USkeletalMeshComponent* HitStopMesh = GetMesh();
	if (!HitStopMesh) return;

	bHasHitStopped = true;

	const float PreviousAnimRate = HitStopMesh->GlobalAnimRateScale;
	const float NewAnimRate = FMath::Max(0.f, TimeScale);
	HitStopMesh->GlobalAnimRateScale = NewAnimRate;
	
	if (!World)
	{
		HitStopMesh->GlobalAnimRateScale = PreviousAnimRate;
		bHasHitStopped = false;
		return;
	}

	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(
		TimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, HitStopMesh, PreviousAnimRate, MovementComp, PreviousMovementMode, PreviousCustomMovementMode, OwnerController, bChangedControllerMoveInput]()
		{
			if (IsValid(HitStopMesh))
			{
				HitStopMesh->GlobalAnimRateScale = PreviousAnimRate;
			}

			// Restore movement mode so root motion can continue as normal.
			if (IsValid(MovementComp))
			{
				if (PreviousMovementMode == MOVE_Custom)
				{
					MovementComp->SetMovementMode(MOVE_Custom, PreviousCustomMovementMode);
				}
				else
				{
					MovementComp->SetMovementMode(PreviousMovementMode);
				}
			}

			if (IsValid(OwnerController) && bChangedControllerMoveInput)
			{
				OwnerController->SetIgnoreMoveInput(false);
			}

			bHasHitStopped = false;
		}),
		Duration,
		false);
}

void APL_BaseCharacter::InitializeDefaultAttributes()
{
	for (const TSubclassOf<UGameplayEffect>& EffectClass : DefaultAttributeEffects)
	{
		ApplyEffectToSelf(EffectClass, 1.f);
	}
}

FActiveGameplayEffectHandle APL_BaseCharacter::ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass,
	float Level) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC || !GameplayEffectClass)
	{
		return FActiveGameplayEffectHandle();
	}

	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddSourceObject(this);

	const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(GameplayEffectClass, Level, ContextHandle);
	if (!SpecHandle.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	return ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
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
