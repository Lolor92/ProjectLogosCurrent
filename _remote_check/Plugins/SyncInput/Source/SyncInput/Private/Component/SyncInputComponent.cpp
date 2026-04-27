#include "Component/SyncInputComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "AbilitySystemComponent.h"
#include "FunctionLibrary/SyncInputFunctionLibrary.h"
#include "InputActionValue.h"

namespace SyncInputTags
{
	static const FGameplayTag& Move()
	{
		static FGameplayTag T = FGameplayTag::RequestGameplayTag(TEXT("SyncInput.Move"));
		return T;
	}
	static const FGameplayTag& Look()
	{
		static FGameplayTag T = FGameplayTag::RequestGameplayTag(TEXT("SyncInput.Look"));
		return T;
	}
}

USyncInputComponent::USyncInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USyncInputComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveControllerChangedDelegate.AddDynamic(this, &USyncInputComponent::HandleControllerChanged);
	}

	if (APlayerController* PC = GetOwningPlayerController())
	{
		BindToPlayerController(PC);
		APawn* InitialPawn = PC->GetPawn();
		HandleNewPawn(InitialPawn ? InitialPawn : Cast<APawn>(GetOwner()));
	}
}

void USyncInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &USyncInputComponent::HandleControllerChanged);
	}

	UninstallFromPawn();
	UnbindFromPlayerController();
	Super::EndPlay(EndPlayReason);
}

void USyncInputComponent::HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	if (Pawn != GetOwner())
	{
		return;
	}

	UninstallFromPawn();
	UnbindFromPlayerController();

	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		BindToPlayerController(PC);
		APawn* PossessedPawn = PC->GetPawn();
		HandleNewPawn(PossessedPawn ? PossessedPawn : Pawn);
	}
}

void USyncInputComponent::HandleNewPawn(APawn* NewPawn)
{
	UninstallFromPawn();
	if (!NewPawn || NewPawn != GetOwner() || !IsLocallyControlledOwner()) return;

	InstallForPawn(NewPawn);
}

void USyncInputComponent::BindToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}

	if (CachedPlayerController.Get() == PlayerController && NewPawnHandle.IsValid())
	{
		return;
	}

	UnbindFromPlayerController();

	CachedPlayerController = PlayerController;
	NewPawnHandle = PlayerController->GetOnNewPawnNotifier().AddUObject(this, &USyncInputComponent::HandleNewPawn);
}

void USyncInputComponent::UnbindFromPlayerController()
{
	if (APlayerController* PC = CachedPlayerController.Get())
	{
		if (NewPawnHandle.IsValid())
		{
			PC->GetOnNewPawnNotifier().Remove(NewPawnHandle);
		}
	}

	NewPawnHandle.Reset();
	CachedPlayerController = nullptr;
}

void USyncInputComponent::InstallForPawn(APawn* Pawn)
{
	CachedPlayerController = GetOwningPlayerController();
	if (!CachedPlayerController.IsValid() || !CachedPlayerController->IsLocalController() || Pawn != GetOwner())
	{
		return;
	}

	AddMappingContextsForLocalPlayer();

	if (APlayerController* PC = CachedPlayerController.Get())
	{
		if (!InjectedEnhancedInputComponent)
		{
			InjectedEnhancedInputComponent = NewObject<UEnhancedInputComponent>(
				PC, UEnhancedInputComponent::StaticClass(), TEXT("SyncInput_InjectedInput"));
			InjectedEnhancedInputComponent->RegisterComponent();
			PC->PushInputComponent(InjectedEnhancedInputComponent);
		}
	}

	BindActionsFromConfig();
}

void USyncInputComponent::UninstallFromPawn()
{
	RemoveMappingContextsForLocalPlayer();

	if (APlayerController* PC = CachedPlayerController.Get())
	{
		if (InjectedEnhancedInputComponent)
		{
			PC->PopInputComponent(InjectedEnhancedInputComponent);
			InjectedEnhancedInputComponent->DestroyComponent();
			InjectedEnhancedInputComponent = nullptr;
		}
	}
	CachedPlayerController = nullptr;
}

void USyncInputComponent::AddMappingContextsForLocalPlayer() const
{
	if (!InputConfig) { UE_LOG(LogTemp, Warning, TEXT("SyncInput: InputConfig is null.")); return; }

	const APlayerController* PC = CachedPlayerController.IsValid() ? CachedPlayerController.Get() : GetOwningPlayerController();
	if (!PC) return;

	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			for (const FSyncInputMappingContextEntry& E : InputConfig->MappingContexts)
			{
				if (E.InputMappingContext)
				{
					Subsys->AddMappingContext(E.InputMappingContext, E.Priority);
				}
			}
		}
	}
}

void USyncInputComponent::RemoveMappingContextsForLocalPlayer() const
{
	if (!InputConfig) return;

	const APlayerController* PC = CachedPlayerController.IsValid() ? CachedPlayerController.Get() : GetOwningPlayerController();
	if (!PC) return;

	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			for (const FSyncInputMappingContextEntry& E : InputConfig->MappingContexts)
			{
				if (E.InputMappingContext)
				{
					Subsys->RemoveMappingContext(E.InputMappingContext);
				}
			}
		}
	}
}

void USyncInputComponent::BindActionsFromConfig()
{
	if (!InjectedEnhancedInputComponent) return;
	if (!InputConfig) return;

	if (InputConfig->SyncInputActions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SyncInput: SyncInputActions is empty on %s"), *GetNameSafe(InputConfig));
	}

	for (const FSyncInputAction& Row : InputConfig->SyncInputActions)
	{
		if (!Row.InputAction || !Row.InputTag.IsValid()) continue;

		// Special-case Move/Look: bind to axis handlers
		if (Row.InputTag.MatchesTagExact(SyncInputTags::Move()))
		{
			InjectedEnhancedInputComponent->BindAction(
				Row.InputAction, ETriggerEvent::Triggered, this, &USyncInputComponent::Move);
			continue;
		}
		if (Row.InputTag.MatchesTagExact(SyncInputTags::Look()))
		{
			InjectedEnhancedInputComponent->BindAction(
				Row.InputAction, ETriggerEvent::Triggered, this, &USyncInputComponent::Look);
			continue;
		}

		// Everything else forwards to GAS via the tag
		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Started,
			this, &USyncInputComponent::HandleActionPressed, Row.InputTag);

		InjectedEnhancedInputComponent->BindAction(
			Row.InputAction, ETriggerEvent::Completed,
			this, &USyncInputComponent::HandleActionReleased, Row.InputTag);
	}
}

bool USyncInputComponent::IsLocallyControlledOwner() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	return PC && PC->IsLocalController();
}

APlayerController* USyncInputComponent::GetOwningPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}

void USyncInputComponent::HandleActionPressed(FGameplayTag InputTag)
{
	OnSyncInputPressed.Broadcast(InputTag);
	
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = USyncInputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}
	if (!AbilitySystemComponent) return;

	// 1) Activate any ability whose AbilityTags contain InputTag (server-authoritative)
	FGameplayTagContainer ActivationTags; ActivationTags.AddTag(InputTag);
	AbilitySystemComponent->TryActivateAbilitiesByTag(ActivationTags);

	// 2) Also forward "pressed" to already-active matching specs (dynamic OR ability tags)
	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		const bool bMatches = Spec.GetDynamicSpecSourceTags().HasTag(InputTag) ||
			(Spec.Ability && Spec.Ability->GetAssetTags().HasTag(InputTag));
		if (!bMatches) continue;

		AbilitySystemComponent->AbilitySpecInputPressed(Spec);

		FPredictionKey PredictionKey;
		if (UGameplayAbility* PrimaryInstance = Spec.GetPrimaryInstance())
		{
			PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
		}
		AbilitySystemComponent->InvokeReplicatedEvent(
			EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, PredictionKey);

		if (!Spec.IsActive())
		{
			AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		}
	}
}

void USyncInputComponent::HandleActionReleased(FGameplayTag InputTag)
{
	OnSyncInputReleased.Broadcast(InputTag);
	
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = USyncInputFunctionLibrary::GetAbilitySystemComponent(GetOwner());
	}
	if (!AbilitySystemComponent) return;

	for (FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		const bool bMatches = Spec.GetDynamicSpecSourceTags().HasTag(InputTag) ||
			(Spec.Ability && Spec.Ability->GetAssetTags().HasTag(InputTag));

		if (!bMatches) continue;

		if (Spec.IsActive())
		{
			AbilitySystemComponent->AbilitySpecInputReleased(Spec);

			FPredictionKey PredictionKey;
			if (UGameplayAbility* PrimaryInstance = Spec.GetPrimaryInstance())
			{
				PredictionKey = PrimaryInstance->GetCurrentActivationInfo().GetActivationPredictionKey();
			}

			AbilitySystemComponent->InvokeReplicatedEvent(
				EAbilityGenericReplicatedEvent::InputReleased,
				Spec.Handle,
				PredictionKey);
		}
	}
}

void USyncInputComponent::Move(const FInputActionValue& Value)
{
	// Retrieve 2D input vector (X: right/left, Y: forward/backward)
	const FVector2D InputVector = Value.Get<FVector2D>();

	// Get Yaw rotation from controller
	const APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController) return;

	const FRotator YawRotation(0.f, PlayerController->GetControlRotation().Yaw, 0.f);

	// Calculate forward and right directions based on Yaw rotation
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// Add movement input to pawn if valid
	if (APawn* ControlledPawn = Cast<APawn>(GetOwner()))
	{
		ControlledPawn->AddMovementInput(Forward, InputVector.Y);
		ControlledPawn->AddMovementInput(Right,   InputVector.X);
	}
}

void USyncInputComponent::Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();

	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		PlayerController->AddYawInput(LookVector.X);
		PlayerController->AddPitchInput(LookVector.Y);
	}
}
