// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "PL_InputConfig.h"
#include "PL_InputComponent.generated.h"

class UGameplayAbility;
struct FGameplayAbilitySpec;
struct FInputActionValue;
class UAbilitySystemComponent;
class APlayerController;
class APawn;
class UEnhancedInputComponent;
class AController;
class USpringArmComponent;
class UCameraComponent;

// Input-owned combo window for one starter ability.
struct FPLActiveComboChain
{
	// Last ability that successfully advanced this chain.
	FGameplayAbilitySpecHandle CurrentAbilityHandle;

	// Next ability to trigger when the starter ability is pressed again.
	TSubclassOf<UGameplayAbility> NextAbilityClass = nullptr;

	// Separate timeout per starter ability.
	FTimerHandle TimerHandle;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTLOGOS_API UPL_InputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPL_InputComponent();

	// Input config.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	TObjectPtr<UPL_InputConfig> InputConfig = nullptr;

protected:
	// Component lifecycle.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Zoom settings.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom")
	bool bEnableZoom = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom", meta=(ClampMin="0"))
	int32 MinZoomLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom", meta=(ClampMin="0"))
	int32 MaxZoomLevel = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom", meta=(ClampMin="0"))
	int32 ZoomLevel = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom", meta=(ClampMin="0.0"))
	float ZoomStepDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom", meta=(ClampMin="0.0"))
	float ZoomInterpSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom")
	FVector DefaultCameraOffset = FVector(0.f, 0.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input|Zoom")
	FVector ClosestZoomCameraOffset = FVector(50.f, 0.f, 70.f);

private:
	// Possession and controller tracking.
	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	void HandleNewPawn(APawn* NewPawn);
	void BindToPlayerController(APlayerController* PlayerController);
	void UnbindFromPlayerController();
	void InstallForPawn(APawn* Pawn);
	void UninstallFromPawn();

	// Enhanced Input setup.
	void AddMappingContextsForLocalPlayer() const;
	void RemoveMappingContextsForLocalPlayer() const;
	void BindActionsFromConfig();

	// Owner lookups.
	bool IsLocallyControlledOwner() const;
	APlayerController* GetOwningPlayerController() const;
	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	// Input forwarding.
	void HandleActionPressed(FGameplayTag InputTag);
	void HandleActionReleased(FGameplayTag InputTag);

	// Combo forwarding.
	bool TryActivateComboAbility(const FGameplayAbilitySpec& RequestedAbilitySpec);
	void UpdateComboChain(const FGameplayAbilitySpecHandle StarterHandle, const FGameplayAbilitySpec& CurrentAbilitySpec);
	void ClearComboChain(FGameplayAbilitySpecHandle StarterHandle);
	void ClearAllComboChains();

	// Active combo chains by starter ability.
	TMap<FGameplayAbilitySpecHandle, FPLActiveComboChain> ActiveComboChains;

	// Axis handlers.
	void Move(const FInputActionValue& Value);
	void StopMove(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void Zoom(const FInputActionValue& Value);
	void ApplyZoom();
	void SetStrafeFacingRotationActive(bool bActive) const;

	// Camera helpers.
	USpringArmComponent* FindSpringArm() const;
	UCameraComponent* FindCamera() const;

	// Runtime input state.
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> InjectedEnhancedInputComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> CachedSpringArm = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> CachedCamera = nullptr;

	UPROPERTY(Transient)
	float DesiredZoomArmLength = 0.f;

	// Active controller binding.
	FDelegateHandle NewPawnHandle;
	TWeakObjectPtr<APlayerController> CachedPlayerController;
};
