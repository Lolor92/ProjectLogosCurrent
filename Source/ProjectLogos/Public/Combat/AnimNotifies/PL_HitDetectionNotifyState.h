// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "PL_HitDetectionNotifyState.generated.h"

class UGameplayEffect;

USTRUCT(BlueprintType)
struct FPLHitWindowGameplayEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects")
	TSubclassOf<UGameplayEffect> GameplayEffectClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects", meta=(ClampMin="0.0"))
	float EffectLevel = 1.f;
};

UCLASS(meta=(DisplayName="HitWindow"))
class PROJECTLOGOS_API UPL_HitDetectionNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection")
	FName DebugSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects",
		meta=(TitleProperty="GameplayEffectClass"))
	TArray<FPLHitWindowGameplayEffect> GameplayEffectsToApply;
};
