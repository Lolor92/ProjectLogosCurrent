#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "PL_BaseCharacter.generated.h"

class UPL_CombatComponent;
class UAbilitySystemComponent;
class UAttributeSet;

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

USTRUCT(BlueprintType)
struct FRepHitStopState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TimeScale = 0.f;
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

	// Ability animation state.
	UFUNCTION(BlueprintCallable, Category="Ability|Animation")
	void SetAbilityAnimState(const FRepAbilityAnimState& NewState);

	UFUNCTION(BlueprintCallable, Category="Ability|Animation")
	void ResetAbilityAnimState();

	UFUNCTION(BlueprintCallable, Category="HitStop")
	void SetHitStopState(const FRepHitStopState& NewState);

	UFUNCTION(BlueprintCallable, Category="HitStop")
	void ClearHitStopState();

	const FRepAbilityAnimState& GetAbilityAnimState() const { return AbilityAnimState; }
	const FRepHitStopState& GetHitStopState() const { return HitStopState; }
	UPL_CombatComponent* GetCombatComponent() const { return CombatComponent; }

protected:
	// Character setup.
	virtual void InitializeDefaultAttributes();
	
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	
	// Replicated state.
	UPROPERTY(ReplicatedUsing=OnRep_AbilityAnimState)
	FRepAbilityAnimState AbilityAnimState;

	UPROPERTY(ReplicatedUsing=OnRep_HitStopState)
	FRepHitStopState HitStopState;

	UFUNCTION(Server, Reliable)
	void ServerSetAbilityAnimState(const FRepAbilityAnimState& NewState);

	UFUNCTION(Server, Reliable)
	void ServerSetHitStopState(const FRepHitStopState& NewState);

	UFUNCTION()
	void OnRep_AbilityAnimState();

	UFUNCTION()
	void OnRep_HitStopState();

	void ApplyAbilityAnimState(const FRepAbilityAnimState& NewState);
	void ApplyHitStopState(const FRepHitStopState& NewState);
	
	// GAS references. Player characters receive these from PlayerState.
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UAttributeSet> AttributeSet;
	
	// Combat setup and reactions.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UPL_CombatComponent> CombatComponent = nullptr;
};
