// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
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

USTRUCT(BlueprintType)
struct FPLHitWindowGameplayEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects")
	TSubclassOf<UGameplayEffect> GameplayEffectClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects", meta=(ClampMin="0.0"))
	float EffectLevel = 1.f;
};
