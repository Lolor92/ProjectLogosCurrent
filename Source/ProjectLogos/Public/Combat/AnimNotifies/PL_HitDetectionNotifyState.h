// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Combat/Data/PL_HitWindowTypes.h"
#include "PL_HitDetectionNotifyState.generated.h"

class UPL_CombatComponent;

UCLASS(meta=(DisplayName="HitWindow"))
class PROJECTLOGOS_API UPL_HitDetectionNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

private:
	static UPL_CombatComponent* ResolveCombatComponent(USkeletalMeshComponent* MeshComp);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection")
	FName TraceSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Detection",
		meta=(ShowOnlyInnerProperties))
	FPLHitWindowSettings HitWindowSettings;
};
