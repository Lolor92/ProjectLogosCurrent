// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "PL_AbilitySystemComponent.generated.h"

class UAnimMontage;

UCLASS()
class PROJECTLOGOS_API UPL_AbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	UPL_AbilitySystemComponent();

protected:
	virtual void OnRep_ReplicatedAnimMontage() override;

private:
	bool ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage) const;
};
