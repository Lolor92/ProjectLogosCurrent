#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameFramework/Character.h"
#include "PL_BaseCharacter.generated.h"

class UPL_CombatComponent;
class UAbilitySystemComponent;
class UAttributeSet;
class UPL_AttributeSet;
class UAnimMontage;
class UGameplayEffect;
struct FTimerHandle;

// Replicated ability animation state shared by montage abilities and animation code.
USTRUCT(BlueprintType)
struct FRepAbilityAnimState
{
	GENERATED_BODY()

	// Montage blend gates.
	UPROPERTY()
	uint8 bCanBlendMontage : 1;

	UPROPERTY()
	uint8 bShouldBlendLowerBody : 1;

	// Predicted movement state.
	UPROPERTY()
	uint8 bRootMotionEnabled : 1;

	UPROPERTY()
	uint8 bMovementInputSuppressed : 1;

	FRepAbilityAnimState()
		: bCanBlendMontage(false)
		, bShouldBlendLowerBody(false)
		, bRootMotionEnabled(true)
		, bMovementInputSuppressed(false)
	{
	}

	bool operator==(const FRepAbilityAnimState& Other) const
	{
		return bCanBlendMontage == Other.bCanBlendMontage
			&& bShouldBlendLowerBody == Other.bShouldBlendLowerBody
			&& bRootMotionEnabled == Other.bRootMotionEnabled
			&& bMovementInputSuppressed == Other.bMovementInputSuppressed;
	}

	bool operator!=(const FRepAbilityAnimState& Other) const
	{
		return !(*this == Other);
	}
};

UCLASS()
class PROJECTLOGOS_API APL_BaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	APL_BaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// GAS can live on this character or on the owning player state.
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UAttributeSet* GetAttributeSet() const;
	UPL_AttributeSet* GetPLAttributeSet() const;

	UFUNCTION(BlueprintPure, Category="Combat|Faction")
	FName GetFactionId() const { return FactionId; }

	UFUNCTION(BlueprintPure, Category="Combat|Faction")
	bool IsFriendlyTo(const APL_BaseCharacter* OtherCharacter) const;

	UFUNCTION(BlueprintPure, Category="Combat|Faction")
	bool IsHostileTo(const APL_BaseCharacter* OtherCharacter) const;

	UFUNCTION(BlueprintPure, Category="Combat|Targeting")
	bool IsAlive() const;

	UFUNCTION(BlueprintPure, Category="Combat|Targeting")
	bool CanBeTargeted() const { return bCanBeTargeted; }

	// Ability animation state.
	UFUNCTION(BlueprintCallable, Category="Ability|Animation")
	void SetAbilityAnimState(const FRepAbilityAnimState& NewState);

	UFUNCTION(BlueprintCallable, Category="Ability|Animation")
	void ResetAbilityAnimState();

	UFUNCTION(BlueprintCallable, Category="Combat|Rotation")
	void RotateToFaceActor(AActor* TargetActor);
	
	const FRepAbilityAnimState& GetAbilityAnimState() const { return AbilityAnimState; }
	UPL_CombatComponent* GetCombatComponent() const { return CombatComponent; }
	
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	
	FActiveGameplayEffectHandle ApplyEffectToSelf(const TSubclassOf<UGameplayEffect>& GameplayEffectClass, float Level = 1.f) const;

protected:
	// Character setup.
	virtual void InitializeDefaultAttributes();
	
	
	// Replicated state.
	UPROPERTY(ReplicatedUsing=OnRep_AbilityAnimState)
	FRepAbilityAnimState AbilityAnimState;

	UFUNCTION(Server, Reliable)
	void ServerSetAbilityAnimState(const FRepAbilityAnimState& NewState);
	
	UFUNCTION()
	void OnRep_AbilityAnimState();

	void ApplyAbilityAnimState(const FRepAbilityAnimState& NewState);
	
	// GAS references. Player characters receive these from PlayerState.
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UAttributeSet> AttributeSet;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attributes")
	TArray<TSubclassOf<UGameplayEffect>> DefaultAttributeEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Faction")
	FName FactionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Targeting")
	bool bCanBeTargeted = true;
	
	// Combat setup and reactions.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UPL_CombatComponent> CombatComponent = nullptr;
};
