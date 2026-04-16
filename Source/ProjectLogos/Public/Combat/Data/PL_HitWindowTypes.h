// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PL_HitWindowTypes.generated.h"

class UGameplayEffect;

UENUM(BlueprintType)
enum class EPLHitDetectionShapeType : uint8
{
	Sphere UMETA(DisplayName="Sphere"),
	Capsule UMETA(DisplayName="Capsule"),
	Box UMETA(DisplayName="Box")
};

USTRUCT(BlueprintType)
struct FPLHitWindowShapeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape")
	EPLHitDetectionShapeType ShapeType = EPLHitDetectionShapeType::Capsule;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape", meta=(ClampMin="0.0"))
	float SphereRadius = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape", meta=(ClampMin="0.0"))
	float CapsuleRadius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape", meta=(ClampMin="0.0"))
	float CapsuleHalfHeight = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape")
	FVector BoxHalfExtent = FVector(10.f, 10.f, 40.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape")
	FRotator LocalRotation = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct FPLHitStopSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|HitStop",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float Duration = 0.f;

	// 0.0 = full freeze, 1.0 = no slowdown.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|HitStop",
		meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
	float TimeScale = 0.f;

	bool IsEnabled() const
	{
		return Duration > 0.f;
	}
};

UENUM(BlueprintType)
enum class EPLHitWindowMoveDirection : uint8
{
	None UMETA(DisplayName="None"),
	MoveCloser UMETA(DisplayName="Move Closer"),
	MoveAway UMETA(DisplayName="Move Away"),
	SnapToDistance UMETA(DisplayName="Snap To Distance")
};

UENUM(BlueprintType)
enum class EPLHitWindowTeleportType : uint8
{
	None UMETA(DisplayName="None"),
	ResetPhysics UMETA(DisplayName="Reset Physics")
};

static FORCEINLINE ETeleportType ToTeleportType(const EPLHitWindowTeleportType InTeleportType)
{
	return InTeleportType == EPLHitWindowTeleportType::ResetPhysics
		? ETeleportType::ResetPhysics
		: ETeleportType::None;
}

USTRUCT(BlueprintType)
struct FPLHitWindowMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement")
	EPLHitWindowMoveDirection MoveDirection = EPLHitWindowMoveDirection::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides, ClampMin="0.0"))
	float MoveDistance = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides))
	bool bSweep = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides))
	EPLHitWindowTeleportType TeleportType = EPLHitWindowTeleportType::ResetPhysics;
};

UENUM(BlueprintType)
enum class EPLHitWindowRotationDirection : uint8
{
	None UMETA(DisplayName="None"),
	FaceToFace UMETA(DisplayName="Face Instigator"),
	FaceAway UMETA(DisplayName="Face Away"),
	FaceOppositeInstigatorForward UMETA(DisplayName="Face Opposite Instigator Forward"),
	FaceDirection UMETA(DisplayName="Face Direction")
};

USTRUCT(BlueprintType)
struct FPLHitWindowRotationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation")
	EPLHitWindowRotationDirection RotationDirection = EPLHitWindowRotationDirection::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation",
		meta=(EditCondition="RotationDirection == EPLHitWindowRotationDirection::FaceDirection", EditConditionHides))
	FRotator DirectionToFace = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation",
		meta=(EditCondition="RotationDirection != EPLHitWindowRotationDirection::None", EditConditionHides))
	EPLHitWindowTeleportType TeleportType = EPLHitWindowTeleportType::ResetPhysics;
};

USTRUCT(BlueprintType)
struct FPLHitWindowBlockSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Block")
	bool bBlockable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Block",
		meta=(EditCondition="bBlockable", EditConditionHides, ClampMin="0.0", ClampMax="180.0"))
	float BlockAngleDegrees = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Block",
		meta=(EditCondition="bBlockable", EditConditionHides))
	bool bAllowMovementWhenBlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Block",
		meta=(EditCondition="bBlockable", EditConditionHides))
	bool bAllowRotationWhenBlocked = false;
};

USTRUCT(BlueprintType)
struct FPLHitWindowDodgeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Dodge")
	bool bDodgeable = false;
};

UENUM(BlueprintType)
enum class EPLHitWindowSuperArmorLevel : uint8
{
	None UMETA(DisplayName="None"),
	SuperArmor1 UMETA(DisplayName="Super Armor 1"),
	SuperArmor2 UMETA(DisplayName="Super Armor 2"),
	SuperArmor3 UMETA(DisplayName="Super Armor 3")
};

UENUM(BlueprintType)
enum class EPLHitWindowCueSpawnPoint : uint8
{
	OwnerLocation UMETA(DisplayName="Owner Location"),
	HitImpactPoint UMETA(DisplayName="Hit Impact Point"),
	HitLocation UMETA(DisplayName="Hit Location")
};

UENUM(BlueprintType)
enum class EPLHitWindowCueRecipient : uint8
{
	Instigator UMETA(DisplayName="Instigator"),
	Target UMETA(DisplayName="Target"),
	Both UMETA(DisplayName="Both")
};

UENUM(BlueprintType)
enum class EPLHitWindowCueTriggerTiming : uint8
{
	OnActivation UMETA(DisplayName="On Activation"),
	OnHit UMETA(DisplayName="On Hit")
};

USTRUCT(BlueprintType)
struct FPLHitWindowGameplayEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects")
	TSubclassOf<UGameplayEffect> GameplayEffectClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects", meta=(ClampMin="0.0"))
	float EffectLevel = 1.f;
};

USTRUCT(BlueprintType)
struct FPLHitWindowGameplayCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues",
		meta=(Categories="GameplayCue", DisplayName="Gameplay Cue Tag"))
	FGameplayTag CueTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues")
	EPLHitWindowCueSpawnPoint SpawnPoint = EPLHitWindowCueSpawnPoint::HitImpactPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues")
	EPLHitWindowCueTriggerTiming TriggerTiming = EPLHitWindowCueTriggerTiming::OnHit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues")
	bool bAttachToTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues")
	EPLHitWindowCueRecipient Recipient = EPLHitWindowCueRecipient::Target;

	bool HasValidCueTag() const
	{
		return CueTag.IsValid();
	}
};
