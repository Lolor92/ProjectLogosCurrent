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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape",
		meta=(EditCondition="ShapeType == EPLHitDetectionShapeType::Sphere", EditConditionHides, ClampMin="0.0"))
	float SphereRadius = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape",
		meta=(EditCondition="ShapeType == EPLHitDetectionShapeType::Capsule", EditConditionHides, ClampMin="0.0"))
	float CapsuleRadius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape",
		meta=(EditCondition="ShapeType == EPLHitDetectionShapeType::Capsule", EditConditionHides, ClampMin="0.0"))
	float CapsuleHalfHeight = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape",
		meta=(EditCondition="ShapeType == EPLHitDetectionShapeType::Box", EditConditionHides))
	FVector BoxHalfExtent = FVector(10.f, 10.f, 40.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape")
	FRotator LocalRotation = FRotator::ZeroRotator;
};

UENUM(BlueprintType)
enum class EPLHitWindowMoveDirection : uint8
{
	None UMETA(DisplayName="None"),
	KeepCurrentDistance UMETA(DisplayName="Keep Current Distance"),
	MoveCloser UMETA(DisplayName="Move Closer"),
	MoveAway UMETA(DisplayName="Move Away"),
	SnapToDistance UMETA(DisplayName="Snap To Distance")
};

UENUM(BlueprintType)
enum class EPLHitWindowLateralOffsetMode : uint8
{
	KeepCurrent UMETA(DisplayName="Keep Current"),
	AddOffset UMETA(DisplayName="Add Offset"),
	SnapToOffset UMETA(DisplayName="Snap To Offset")
};

UENUM(BlueprintType)
enum class EPLHitWindowTransformTriggerTiming : uint8
{
	OnHit UMETA(DisplayName="On Hit"),
	OnActivation UMETA(DisplayName="On Activation"),
	Both UMETA(DisplayName="Both")
};

UENUM(BlueprintType)
enum class EPLHitWindowTransformRecipient : uint8
{
	Instigator UMETA(DisplayName="Instigator"),
	Target UMETA(DisplayName="Target"),
	Both UMETA(DisplayName="Both")
};

UENUM(BlueprintType)
enum class EPLHitWindowReferenceActorSource : uint8
{
	Instigator UMETA(DisplayName="Instigator"),
	Target UMETA(DisplayName="Target"),
	LastCombatReferenceActor UMETA(DisplayName="Last Combat Reference Actor")
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
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides))
	EPLHitWindowTransformTriggerTiming TriggerTiming = EPLHitWindowTransformTriggerTiming::OnHit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides))
	EPLHitWindowTransformRecipient Recipient = EPLHitWindowTransformRecipient::Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides))
	EPLHitWindowReferenceActorSource ReferenceActorSource = EPLHitWindowReferenceActorSource::Instigator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None && MoveDirection != EPLHitWindowMoveDirection::KeepCurrentDistance", EditConditionHides, ClampMin="0.0"))
	float MoveDistance = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None", EditConditionHides))
	EPLHitWindowLateralOffsetMode LateralOffsetMode = EPLHitWindowLateralOffsetMode::KeepCurrent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(EditCondition="MoveDirection != EPLHitWindowMoveDirection::None && LateralOffsetMode != EPLHitWindowLateralOffsetMode::KeepCurrent", EditConditionHides))
	float LateralOffset = 0.f;

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
	FaceToFace UMETA(DisplayName="Face Reference Actor"),
	FaceAway UMETA(DisplayName="Face Away From Reference"),
	FaceOppositeInstigatorForward UMETA(DisplayName="Face Opposite Reference Forward"),
	FaceDirection UMETA(DisplayName="Face Direction")
};

USTRUCT(BlueprintType)
struct FPLHitWindowRotationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation")
	EPLHitWindowRotationDirection RotationDirection = EPLHitWindowRotationDirection::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation",
		meta=(EditCondition="RotationDirection != EPLHitWindowRotationDirection::None", EditConditionHides))
	EPLHitWindowTransformTriggerTiming TriggerTiming = EPLHitWindowTransformTriggerTiming::OnHit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation",
		meta=(EditCondition="RotationDirection != EPLHitWindowRotationDirection::None", EditConditionHides))
	EPLHitWindowTransformRecipient Recipient = EPLHitWindowTransformRecipient::Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation",
		meta=(EditCondition="RotationDirection != EPLHitWindowRotationDirection::None", EditConditionHides))
	EPLHitWindowReferenceActorSource ReferenceActorSource = EPLHitWindowReferenceActorSource::Instigator;

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

USTRUCT(BlueprintType)
struct FPLHitWindowDefenseSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Defense",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowBlockSettings BlockSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Defense",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowDodgeSettings DodgeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Defense")
	EPLHitWindowSuperArmorLevel RequiredSuperArmor = EPLHitWindowSuperArmorLevel::None;
};

USTRUCT(BlueprintType)
struct FPLHitWindowDebugSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Debug")
	bool bDrawDebugTrace = false;
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

USTRUCT(BlueprintType)
struct FPLHitWindowSettings
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Debug",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowDebugSettings DebugSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Shape",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowShapeSettings ShapeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Movement",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowMovementSettings MovementSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Rotation",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowRotationSettings RotationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Defense",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowDefenseSettings DefenseSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects",
		meta=(TitleProperty="GameplayEffectClass"))
	TArray<FPLHitWindowGameplayEffect> GameplayEffectsToApply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Gameplay Cues",
		meta=(TitleProperty="CueTag"))
	TArray<FPLHitWindowGameplayCue> GameplayCuesToExecute;
};
