// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "PL_AbilitySet.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;

USTRUCT()
struct FPLAbilitySetGrantedHandles
{
	GENERATED_BODY()

	void AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle);
	void TakeFromAbilitySystem(UAbilitySystemComponent* AbilitySystemComponent);

private:
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;
};

USTRUCT(BlueprintType)
struct FPLAbilityGrant
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TSubclassOf<UGameplayAbility> AbilityClass = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Ability", meta=(ClampMin="1"))
	int32 Level = 1;
};

UCLASS(BlueprintType)
class PROJECTLOGOS_API UPL_AbilitySet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="Abilities", meta=(TitleProperty="AbilityClass"))
	TArray<FPLAbilityGrant> Abilities;

	void GiveToAbilitySystem(UAbilitySystemComponent* AbilitySystemComponent,
		FPLAbilitySetGrantedHandles* OutGrantedHandles, UObject* SourceObject = nullptr) const;
};
