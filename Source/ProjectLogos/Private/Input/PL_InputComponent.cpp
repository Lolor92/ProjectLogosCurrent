// Copyright ProjectLogos

#include "Input/PL_InputComponent.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Combat/Utilities/PL_CombatFunctionLibrary.h"
#include "Component/PL_CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameplayAbilitySpec.h"
#include "GAS/Ability/PL_GameplayAbility.h"
#include "Input/Tag/PL_InputTags.h"
#include "InputAction.h"
#include "InputActionValue.h"

UPL_InputComponent::UPL_InputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPL_InputComponent::BeginPlay()
{
	Super::BeginPlay();

	// Possession can happen before or after this component begins play.
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UPL_InputComponent::HandleControllerChanged);
	}

	// If a local controller already owns this pawn, install input immediately.
	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		BindToPlayerController(PlayerController);

		APawn* InitialPawn = PlayerController->GetPawn();
		HandleNewPawn(InitialPawn ? InitialPawn : Cast<APawn>(GetOwner()));
	}
}

void UPL_InputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Stop listening for controller changes on the owning pawn.
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UPL_InputComponent::HandleControllerChanged);
	}

	// Tear down any input state we installed.
	UninstallFromPawn();
	UnbindFromPlayerController();

	Super::EndPlay(EndPlayReason);
}

void UPL_InputComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Tick only while zoom is interpolating.
	USpringArmComponent* SpringArm = FindSpringArm();
	if (!SpringArm)
	{
		SetComponentTickEnabled(false);
		return;
	}

	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength,
		DesiredZoomArmLength,
		DeltaTime,
		ZoomInterpSpeed);

	if (FMath::IsNearlyEqual(SpringArm->TargetArmLength, DesiredZoomArmLength, 1.f))
	{
		SpringArm->TargetArmLength = DesiredZoomArmLength;
		SetComponentTickEnabled(false);
	}
}

void UPL_InputComponent::HandleControllerChanged(APawn* Pawn, AController* OldController,
	AController* NewController)
{
	if (Pawn != GetOwner()) return;

	// Reset current input wiring before reacting to the new controller.
	UninstallFromPawn();
	UnbindFromPlayerController();

	APlayerController* PlayerController = Cast<APlayerController>(NewController);
	if (!PlayerController) return;

	BindToPlayerController(PlayerController);

	APawn* PossessedPawn = PlayerController->GetPawn();
	HandleNewPawn(PossessedPawn ? PossessedPawn : Pawn);
}

void UPL_InputComponent::HandleNewPawn(APawn* NewPawn)
{
	UninstallFromPawn();

	if (!NewPawn || NewPawn != GetOwner() || !IsLocallyControlledOwner()) return;

	InstallForPawn(NewPawn);
}

void UPL_InputComponent::BindToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController) return;

	if (CachedPlayerController.Get() == PlayerController && NewPawnHandle.IsValid()) return;

	// Clear any previous controller binding before attaching to the new one.
	UnbindFromPlayerController();

	CachedPlayerController = PlayerController;
	NewPawnHandle = PlayerController->GetOnNewPawnNotifier().AddUObject(this, &UPL_InputComponent::HandleNewPawn);
}

void UPL_InputComponent::UnbindFromPlayerController()
{
	if (APlayerController* PlayerController = CachedPlayerController.Get())
	{
		if (NewPawnHandle.IsValid())
			PlayerController->GetOnNewPawnNotifier().Remove(NewPawnHandle);
	}

	NewPawnHandle.Reset();
	CachedPlayerController = nullptr;
}

void UPL_InputComponent::InstallForPawn(APawn* Pawn)
{
	CachedPlayerController = GetOwningPlayerController();
	if (!CachedPlayerController.IsValid() || !CachedPlayerController->IsLocalController() || Pawn != GetOwner())
		return;

	// Cache camera pieces used by zoom and install the mapping contexts for this local player.
	CachedSpringArm = Pawn->FindComponentByClass<USpringArmComponent>();
	CachedCamera = Pawn->FindComponentByClass<UCameraComponent>();
	DesiredZoomArmLength = ZoomLevel * ZoomStepDistance;

	AddMappingContextsForLocalPlayer();

	if (APlayerController* PlayerController = CachedPlayerController.Get())
	{
		if (!InjectedEnhancedInputComponent)
		{
			// Inject a runtime input component so the pawn does not need a native InputComponent.
			InjectedEnhancedInputComponent = NewObject<UEnhancedInputComponent>(
				PlayerController,
				UEnhancedInputComponent::StaticClass(),
				TEXT("PL_InjectedInput"));
			InjectedEnhancedInputComponent->RegisterComponent();
			PlayerController->PushInputComponent(InjectedEnhancedInputComponent);
		}
	}

	BindActionsFromConfig();
}

void UPL_InputComponent::UninstallFromPawn()
{
	RemoveMappingContextsForLocalPlayer();
	ClearAllComboChains();

	if (APlayerController* PlayerController = CachedPlayerController.Get())
	{
		if (InjectedEnhancedInputComponent)
		{
			// Remove only the component we injected.
			PlayerController->PopInputComponent(InjectedEnhancedInputComponent);
			InjectedEnhancedInputComponent->DestroyComponent();
			InjectedEnhancedInputComponent = nullptr;
		}
	}

	CachedSpringArm = nullptr;
	CachedCamera = nullptr;
	DesiredZoomArmLength = 0.f;
	SetComponentTickEnabled(false);
}

void UPL_InputComponent::AddMappingContextsForLocalPlayer() const
{
	if (!InputConfig)
	{
		UE_LOG(LogTemp, Error, TEXT("PL_Input: InputConfig is null."));
		return;
	}

	const APlayerController* PlayerController = CachedPlayerController.IsValid()
		? CachedPlayerController.Get()
		: GetOwningPlayerController();

	if (!PlayerController) return;

	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (!LocalPlayer) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	if (!Subsystem) return;

	for (const FPLInputMappingContextEntry& MappingEntry : InputConfig->MappingContexts)
	{
		if (!MappingEntry.InputMappingContext) continue;

		Subsystem->AddMappingContext(MappingEntry.InputMappingContext, MappingEntry.Priority);
	}
}

void UPL_InputComponent::RemoveMappingContextsForLocalPlayer() const
{
	if (!InputConfig) return;

	const APlayerController* PlayerController = CachedPlayerController.IsValid()
		? CachedPlayerController.Get()
		: GetOwningPlayerController();

	if (!PlayerController) return;

	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (!LocalPlayer) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	if (!Subsystem) return;

	for (const FPLInputMappingContextEntry& MappingEntry : InputConfig->MappingContexts)
	{
		if (!MappingEntry.InputMappingContext) continue;

		Subsystem->RemoveMappingContext(MappingEntry.InputMappingContext);
	}
}

void UPL_InputComponent::BindActionsFromConfig()
{
	if (!InjectedEnhancedInputComponent || !InputConfig) return;

	if (InputConfig->InputActions.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("PL_Input: InputActions is empty on %s"), *GetNameSafe(InputConfig));
	}

	for (const FPLInputAction& InputActionRow : InputConfig->InputActions)
	{
		if (!InputActionRow.InputAction || !InputActionRow.InputTag.IsValid()) continue;

		// Axis-like actions stay local instead of forwarding as gameplay ability input.
		if (InputActionRow.InputTag.MatchesTagExact(TAG_Input_Move))
		{
			InjectedEnhancedInputComponent->BindAction(
				InputActionRow.InputAction,
				ETriggerEvent::Triggered,
				this,
				&UPL_InputComponent::Move
			);

			InjectedEnhancedInputComponent->BindAction(
				InputActionRow.InputAction,
				ETriggerEvent::Completed,
				this,
				&UPL_InputComponent::StopMove
			);

			InjectedEnhancedInputComponent->BindAction(
				InputActionRow.InputAction,
				ETriggerEvent::Canceled,
				this,
				&UPL_InputComponent::StopMove
			);

			continue;
		}

		if (InputActionRow.InputTag.MatchesTagExact(TAG_Input_Look))
		{
			InjectedEnhancedInputComponent->BindAction(InputActionRow.InputAction, ETriggerEvent::Triggered,
				this, &UPL_InputComponent::Look);

			continue;
		}

		if (InputActionRow.InputTag.MatchesTagExact(TAG_Input_Zoom))
		{
			InjectedEnhancedInputComponent->BindAction(InputActionRow.InputAction, ETriggerEvent::Triggered,
				this, &UPL_InputComponent::Zoom);

			continue;
		}

		InjectedEnhancedInputComponent->BindAction(InputActionRow.InputAction, ETriggerEvent::Started,
			this, &UPL_InputComponent::HandleActionPressed, InputActionRow.InputTag);

		InjectedEnhancedInputComponent->BindAction(InputActionRow.InputAction, ETriggerEvent::Completed,
			this, &UPL_InputComponent::HandleActionReleased, InputActionRow.InputTag);
	}
}

bool UPL_InputComponent::IsLocallyControlledOwner() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = OwnerPawn
		? Cast<APlayerController>(OwnerPawn->GetController())
		: nullptr;

	return PlayerController && PlayerController->IsLocalController();
}

APlayerController* UPL_InputComponent::GetOwningPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}

UAbilitySystemComponent* UPL_InputComponent::GetAbilitySystemComponent() const
{
	if (AbilitySystemComponent) return AbilitySystemComponent;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return nullptr;

	return UPL_CombatFunctionLibrary::GetAbilitySystemComponent(OwnerActor);
}

void UPL_InputComponent::HandleActionPressed(FGameplayTag InputTag)
{
	if (!AbilitySystemComponent) AbilitySystemComponent = GetAbilitySystemComponent();

	if (!AbilitySystemComponent) return;

	// Resolve the ability currently bound to this input tag.
	for (FGameplayAbilitySpec& AbilitySpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		const bool bMatchesInputTag = AbilitySpec.GetDynamicSpecSourceTags().HasTag(InputTag)
			|| (AbilitySpec.Ability && AbilitySpec.Ability->GetAssetTags().HasTag(InputTag));

		if (!bMatchesInputTag) continue;

		if (TryActivateComboAbility(AbilitySpec)) return;

		// Let GAS decide whether this can start, interrupt, or retrigger.
		const bool bActivated = AbilitySystemComponent->TryActivateAbility(AbilitySpec.Handle);
		if (bActivated)
		{
			UpdateComboChain(AbilitySpec.Handle, AbilitySpec);
		}

		AbilitySystemComponent->AbilitySpecInputPressed(AbilitySpec);

		FPredictionKey PredictionKey;
		if (UGameplayAbility* PrimaryInstance = AbilitySpec.GetPrimaryInstance())
		{
			PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
		}

		// Keep server-side ability input state in sync with local Enhanced Input.
		AbilitySystemComponent->InvokeReplicatedEvent(
			EAbilityGenericReplicatedEvent::InputPressed,
			AbilitySpec.Handle,
			PredictionKey);
	}
}

void UPL_InputComponent::HandleActionReleased(FGameplayTag InputTag)
{
	if (!AbilitySystemComponent) AbilitySystemComponent = GetAbilitySystemComponent();

	if (!AbilitySystemComponent) return;

	for (FGameplayAbilitySpec& AbilitySpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		const bool bMatchesInputTag = AbilitySpec.GetDynamicSpecSourceTags().HasTag(InputTag)
			|| (AbilitySpec.Ability && AbilitySpec.Ability->GetAssetTags().HasTag(InputTag));

		if (!bMatchesInputTag) continue;
		if (!AbilitySpec.IsActive()) continue;

		AbilitySystemComponent->AbilitySpecInputReleased(AbilitySpec);

		FPredictionKey PredictionKey;
		if (UGameplayAbility* PrimaryInstance = AbilitySpec.GetPrimaryInstance())
		{
			PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
		}

		// Keep server-side ability input state in sync with local Enhanced Input.
		AbilitySystemComponent->InvokeReplicatedEvent(
			EAbilityGenericReplicatedEvent::InputReleased,
			AbilitySpec.Handle,
			PredictionKey);
	}
}

bool UPL_InputComponent::TryActivateComboAbility(const FGameplayAbilitySpec& RequestedAbilitySpec)
{
	if (!AbilitySystemComponent) return false;

	// The pressed ability is the starter key for this chain.
	FPLActiveComboChain* ComboChain = ActiveComboChains.Find(RequestedAbilitySpec.Handle);
	if (!ComboChain || !ComboChain->NextAbilityClass) return false;

	for (FGameplayAbilitySpec& ComboSpec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!ComboSpec.Ability || !ComboSpec.Ability->GetClass()->IsChildOf(ComboChain->NextAbilityClass)) continue;

		const bool bActivated = AbilitySystemComponent->TryActivateAbility(ComboSpec.Handle);
		if (bActivated)
		{
			UpdateComboChain(RequestedAbilitySpec.Handle, ComboSpec);
		}

		// Consume the input even if cooldown/cost blocks the combo target.
		return true;
	}

	ClearComboChain(RequestedAbilitySpec.Handle);
	return true;
}

void UPL_InputComponent::UpdateComboChain(const FGameplayAbilitySpecHandle StarterHandle,
	const FGameplayAbilitySpec& CurrentAbilitySpec)
{
	// Prefer the live instance, then fall back to the class default object.
	UPL_GameplayAbility* CurrentAbility = Cast<UPL_GameplayAbility>(CurrentAbilitySpec.GetPrimaryInstance());
	if (!CurrentAbility)
	{
		CurrentAbility = Cast<UPL_GameplayAbility>(CurrentAbilitySpec.Ability);
	}

	if (!CurrentAbility || !CurrentAbility->GetComboAbilityClass() || CurrentAbility->GetComboWindowDuration() <= 0.f)
	{
		ClearComboChain(StarterHandle);
		return;
	}

	FPLActiveComboChain& ComboChain = ActiveComboChains.FindOrAdd(StarterHandle);
	ComboChain.CurrentAbilityHandle = CurrentAbilitySpec.Handle;
	ComboChain.NextAbilityClass = CurrentAbility->GetComboAbilityClass();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboChain.TimerHandle);

		FTimerDelegate TimerDelegate =
			FTimerDelegate::CreateUObject(this, &ThisClass::ClearComboChain, StarterHandle);

		World->GetTimerManager().SetTimer(
			ComboChain.TimerHandle, TimerDelegate, CurrentAbility->GetComboWindowDuration(), false);
	}
}

void UPL_InputComponent::ClearComboChain(FGameplayAbilitySpecHandle StarterHandle)
{
	if (FPLActiveComboChain* ComboChain = ActiveComboChains.Find(StarterHandle))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ComboChain->TimerHandle);
		}
	}

	ActiveComboChains.Remove(StarterHandle);
}

void UPL_InputComponent::ClearAllComboChains()
{
	UWorld* World = GetWorld();
	for (TPair<FGameplayAbilitySpecHandle, FPLActiveComboChain>& ComboChainPair : ActiveComboChains)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(ComboChainPair.Value.TimerHandle);
		}
	}

	ActiveComboChains.Reset();
}

void UPL_InputComponent::Move(const FInputActionValue& Value)
{
	const FVector2D InputVector = Value.Get<FVector2D>();

	const APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController)
	{
		SetStrafeFacingRotationActive(false);
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		SetStrafeFacingRotationActive(false);
		return;
	}

	const bool bHasMoveInput = !InputVector.IsNearlyZero();
	const bool bCanProcessMoveInput = bHasMoveInput && !PlayerController->IsMoveInputIgnored();
	SetStrafeFacingRotationActive(bCanProcessMoveInput);

	if (!bCanProcessMoveInput)
	{
		return;
	}

	const FRotator YawRotation(0.f, PlayerController->GetControlRotation().Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	OwnerPawn->AddMovementInput(ForwardDirection, InputVector.Y);
	OwnerPawn->AddMovementInput(RightDirection, InputVector.X);
}

void UPL_InputComponent::StopMove(const FInputActionValue& Value)
{
	SetStrafeFacingRotationActive(false);
}

void UPL_InputComponent::Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();

	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController) return;

	PlayerController->AddYawInput(LookVector.X);
	PlayerController->AddPitchInput(LookVector.Y);
}

void UPL_InputComponent::Zoom(const FInputActionValue& Value)
{
	if (!bEnableZoom || MaxZoomLevel < MinZoomLevel) return;

	const float InputAxisValue = Value.Get<float>();
	if (FMath::IsNearlyZero(InputAxisValue)) return;

	if (InputAxisValue > 0.f && ZoomLevel > MinZoomLevel)
	{
		--ZoomLevel;
		ApplyZoom();
	}
	else if (InputAxisValue < 0.f && ZoomLevel < MaxZoomLevel)
	{
		++ZoomLevel;
		ApplyZoom();
	}
}

void UPL_InputComponent::ApplyZoom()
{
	USpringArmComponent* SpringArm = FindSpringArm();
	if (!SpringArm) return;

	DesiredZoomArmLength = ZoomLevel * ZoomStepDistance;
	SetComponentTickEnabled(true);

	if (UCameraComponent* Camera = FindCamera())
	{
		const FVector& CameraOffset = ZoomLevel == MinZoomLevel
			? ClosestZoomCameraOffset
			: DefaultCameraOffset;
		Camera->SetRelativeLocation(CameraOffset);
	}
}

void UPL_InputComponent::SetStrafeFacingRotationActive(bool bActive) const
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = CharacterOwner->GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	if (bActive)
	{
		if (const AController* CharacterController = CharacterOwner->GetController();
			CharacterController && CharacterController->IsMoveInputIgnored())
		{
			bActive = false;
		}

		if (const UPL_CharacterMovementComponent* PLMoveComp = Cast<UPL_CharacterMovementComponent>(MoveComp);
			PLMoveComp && PLMoveComp->IsAbilityMovementInputSuppressed())
		{
			bActive = false;
		}
	}

	MoveComp->bOrientRotationToMovement = false;
	MoveComp->bUseControllerDesiredRotation = bActive;
}

USpringArmComponent* UPL_InputComponent::FindSpringArm() const
{
	if (CachedSpringArm) return CachedSpringArm;

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Pawn->FindComponentByClass<USpringArmComponent>();
	}

	return nullptr;
}

UCameraComponent* UPL_InputComponent::FindCamera() const
{
	if (CachedCamera) return CachedCamera;

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Pawn->FindComponentByClass<UCameraComponent>();
	}

	return nullptr;
}
