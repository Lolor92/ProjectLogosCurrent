// Copyright ProjectLogos

#include "Component/PL_CharacterMovementComponent.h"
#include "AnimInstance/PL_AnimInstance.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GameFramework/Character.h"

namespace PLAbilityRootMotionFlags
{
	// Custom compressed flags ride along with client saved moves.
	constexpr uint8 SuppressAbilityRootMotion = FSavedMove_Character::FLAG_Custom_0;
	constexpr uint8 SuppressAbilityMovementInput = FSavedMove_Character::FLAG_Custom_1;
}

// Saved moves preserve ability-driven movement state for client prediction and replay.
class FSavedMove_PLCharacter final : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	virtual void Clear() override
	{
		Super::Clear();

		// Saved moves are pooled, so reset any state captured from the previous use.
		bSavedAbilityRootMotionSuppressed = false;
		bSavedAbilityMovementInputSuppressed = false;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = Super::GetCompressedFlags();

		// Pack our transient ability state into the bits sent with the movement update.
		if (bSavedAbilityRootMotionSuppressed) Result |= PLAbilityRootMotionFlags::SuppressAbilityRootMotion;
		if (bSavedAbilityMovementInputSuppressed) Result |= PLAbilityRootMotionFlags::SuppressAbilityMovementInput;

		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const FSavedMove_PLCharacter* NewPLMove = static_cast<const FSavedMove_PLCharacter*>(NewMove.Get());

		// Do not merge moves across ability state changes or the flag transition can be lost.
		if (bSavedAbilityRootMotionSuppressed != NewPLMove->bSavedAbilityRootMotionSuppressed) return false;
		if (bSavedAbilityMovementInputSuppressed != NewPLMove->bSavedAbilityMovementInputSuppressed) return false;

		return Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

		// Capture the component state that should be replayed with this move.
		if (const UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			bSavedAbilityRootMotionSuppressed = MoveComp->IsAbilityRootMotionSuppressed();
			bSavedAbilityMovementInputSuppressed = MoveComp->IsAbilityMovementInputSuppressed();
		}
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		Super::PrepMoveFor(Character);

		// Restore saved state before prediction replays this move.
		if (UPL_CharacterMovementComponent* MoveComp = Cast<UPL_CharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			MoveComp->SetAbilityRootMotionSuppressed(bSavedAbilityRootMotionSuppressed);
			MoveComp->SetAbilityMovementInputSuppressed(bSavedAbilityMovementInputSuppressed);
		}
	}

private:
	uint8 bSavedAbilityRootMotionSuppressed : 1 = false;
	uint8 bSavedAbilityMovementInputSuppressed : 1 = false;
};

// Client prediction data is only customized so it can allocate our saved move type.
class FNetworkPredictionData_Client_PLCharacter final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_PLCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_PLCharacter());
	}
};

void UPL_CharacterMovementComponent::SetAbilityRootMotionSuppressed(bool bInSuppressed)
{
	if (bAbilityRootMotionSuppressed == bInSuppressed) return;

	// Root motion affects the anim instance immediately and is also saved into movement prediction.
	bAbilityRootMotionSuppressed = bInSuppressed;
	RefreshAbilityRootMotionMode();
}

void UPL_CharacterMovementComponent::SetAbilityMovementInputSuppressed(bool bInSuppressed)
{
	// Input suppression is separate so collision can stop root motion without unlocking movement.
	bAbilityMovementInputSuppressed = bInSuppressed;
}

void UPL_CharacterMovementComponent::RefreshAbilityRootMotionMode()
{
	if (!CharacterOwner) return;

	USkeletalMeshComponent* MeshComp = CharacterOwner->GetMesh();
	if (!MeshComp) return;

	UPL_AnimInstance* AnimInstance = Cast<UPL_AnimInstance>(MeshComp->GetAnimInstance());
	if (!AnimInstance) return;

	// Keep the anim instance in the same root-motion mode as the predicted CMC flag.
	const bool bRootMotionEnabled = !bAbilityRootMotionSuppressed;
	AnimInstance->bRootMotionEnabled = bRootMotionEnabled;
	AnimInstance->SetRootMotionMode(bRootMotionEnabled
			? ERootMotionMode::RootMotionFromMontagesOnly
			: ERootMotionMode::IgnoreRootMotion);
}

float UPL_CharacterMovementComponent::GetMaxSpeed() const
{
	float MaxSpeed = Super::GetMaxSpeed();
	if (MaxSpeed <= 0.f) return MaxSpeed;

	const APawn* PawnOwnerPtr = PawnOwner;
	if (!PawnOwnerPtr) return MaxSpeed;

	if (const APL_BaseCharacter* CharacterOwnerPtr = Cast<APL_BaseCharacter>(PawnOwnerPtr))
	{
		if (const UPL_CombatComponent* CombatComponent = CharacterOwnerPtr->GetCombatComponent())
		{
			if (CombatComponent->IsBlockingActive())
			{
				return MaxSpeed * BlockingSpeedMultiplier;
			}
		}
	}

	const FVector CurrentAcceleration = Acceleration.GetSafeNormal2D();
	if (CurrentAcceleration.IsNearlyZero()) return MaxSpeed;

	const FVector Forward = PawnOwnerPtr->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero()) return MaxSpeed;

	const float ForwardDot = FVector::DotProduct(Forward, CurrentAcceleration);
	if (ForwardDot <= BackwardDotThreshold)
	{
		return MaxSpeed * BackwardSpeedMultiplier;
	}

	return MaxSpeed;
}

void UPL_CharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// Movement prediction decodes these bits from the compressed move.
	bAbilityRootMotionSuppressed = (Flags & PLAbilityRootMotionFlags::SuppressAbilityRootMotion) != 0;
	bAbilityMovementInputSuppressed = (Flags & PLAbilityRootMotionFlags::SuppressAbilityMovementInput) != 0;
	RefreshAbilityRootMotionMode();
}

FVector UPL_CharacterMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration) const
{
	// Movement stays locked until the montage release point even if root motion was stopped.
	if (bAbilityMovementInputSuppressed) return FVector::ZeroVector;

	return Super::ScaleInputAcceleration(InputAcceleration);
}

FNetworkPredictionData_Client* UPL_CharacterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);

	if (ClientPredictionData == nullptr)
	{
		// Lazily replace the default prediction data so new moves use FSavedMove_PLCharacter.
		UPL_CharacterMovementComponent* MutableThis = const_cast<UPL_CharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_PLCharacter(*this);
	}

	return ClientPredictionData;
}
