// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Combat/Data/PL_HitWindowTypes.h"
#include "PL_HitDetectionNotifyState.generated.h"

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection", meta=(ShowOnlyInnerProperties))
	FPLHitWindowShapeSettings HitShapeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|HitStop",
		meta=(ShowOnlyInnerProperties))
	FPLHitStopSettings HitStopSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection|Effects",
		meta=(TitleProperty="GameplayEffectClass"))
	TArray<FPLHitWindowGameplayEffect> GameplayEffectsToApply;
};
