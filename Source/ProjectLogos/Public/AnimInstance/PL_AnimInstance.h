// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PL_AnimInstance.generated.h"

class UAbilitySystemComponent;
class UPL_GameplayAbility;
class UCharacterMovementComponent;
class APL_BaseCharacter;
class UAnimMontage;

UCLASS()
class PROJECTLOGOS_API UPL_AnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Anim instance lifecycle.
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// Ability-driven animation flags.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bCanBlendMontage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bShouldBlendLowerBody = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bRootMotionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|Movement")
	bool bUseControllerRotationYaw = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsBlocking = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsKnockdown = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsFlinching = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsFrozen = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Anim|State");
	bool bIsStunned = false;

private:
	// Ability montage state.
	void UpdateAbilityAnimReplication();
	bool GetAbilityPercentMontagePlayed(float& OutPercent, UPL_GameplayAbility*& OutAbility);
	UAbilitySystemComponent* GetAbilitySystemComponentSafe();

	// Cached references.
	UPROPERTY()
	APL_BaseCharacter* Character = nullptr;

	UPROPERTY()
	UCharacterMovementComponent* CharacterMovementComponent = nullptr;

	UPROPERTY()
	UAbilitySystemComponent* AbilitySystemComponent = nullptr;

	// Current montage tracking.
	UPROPERTY()
	TObjectPtr<const UPL_GameplayAbility> LastTrackedAbility = nullptr;

	UPROPERTY()
	uint32 LastTrackedAbilityActivationSequenceId = 0;

	UPROPERTY()
	TObjectPtr<const UAnimMontage> LastTrackedMontage = nullptr;

	UPROPERTY()
	bool bReleasedRootMotionThisMontage = false;

	// Movement values for the AnimBP.
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	float GroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	bool bIsAccelerating = false;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	bool IsAirBorne = false;

	// Rotation values for the AnimBP.
	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	FRotator AimRotation;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	FRotator MovementRotation;

	UPROPERTY(BlueprintReadOnly, Category="Anim|Movement", meta=(AllowPrivateAccess="true"))
	float MovementOffsetYaw = 0.f;
};
