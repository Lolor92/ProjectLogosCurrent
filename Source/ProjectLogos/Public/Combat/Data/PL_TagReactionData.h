// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PL_TagReactionData.generated.h"

class UAnimMontage;
class UGameplayEffect;

UENUM(BlueprintType)
enum class EPL_TagReactionPolicy : uint8
{
	OnAdd    UMETA(DisplayName="On Add"),
	OnRemove UMETA(DisplayName="On Remove"),
	Both     UMETA(DisplayName="On Both")
};

UENUM(BlueprintType)
enum class EPLPredictedReactionMovementMode : uint8
{
	MontageOnly UMETA(DisplayName="Montage Only"),

	// Client-side visual knockback. Does not move the replicated capsule.
	VisualRootMotionOffset UMETA(DisplayName="Visual Root Motion Offset")
};

USTRUCT(BlueprintType)
struct FPL_TagReactionAbility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prediction")
	TObjectPtr<UAnimMontage> PredictedReactionMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prediction")
	EPLPredictedReactionMovementMode PredictedMovementMode = EPLPredictedReactionMovementMode::MontageOnly;

	// Mesh-only local offset used for predicted knockback/pushback.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prediction", meta=(ClampMin="0.0", UIMin="0.0", Units="cm"))
	float PredictedVisualRootMotionDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prediction", meta=(ClampMin="0.01", UIMin="0.01", Units="Seconds"))
	float PredictedVisualRootMotionDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prediction", meta=(ClampMin="0.01", UIMin="0.01", Units="Seconds"))
	float PredictedVisualBlendOutTime = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(ClampMin="0.0", UIMin="0.0", Units="Seconds"))
	float DelaySeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FPL_TagReactionEffects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects")
	TArray<TSubclassOf<UGameplayEffect>> Apply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects", meta=(ClampMin="0.0", UIMin="0.0", Units="Seconds"))
	float ApplyDelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects")
	TArray<TSubclassOf<UGameplayEffect>> Remove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects", meta=(ClampMin="0.0", UIMin="0.0", Units="Seconds"))
	float RemoveDelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects")
	FName RemoveTimerKey;
};

USTRUCT(BlueprintType)
struct FPL_TagReactionBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trigger")
	FGameplayTag TriggerTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trigger")
	EPL_TagReactionPolicy Policy = EPL_TagReactionPolicy::OnAdd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ability", meta=(ShowOnlyInnerProperties))
	FPL_TagReactionAbility Ability;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects", meta=(ShowOnlyInnerProperties))
	FPL_TagReactionEffects Effects;
};

UCLASS(BlueprintType)
class PROJECTLOGOS_API UPL_TagReactionData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Tag Reactions", meta=(TitleProperty="TriggerTag"))
	TArray<FPL_TagReactionBinding> Reactions;
};
