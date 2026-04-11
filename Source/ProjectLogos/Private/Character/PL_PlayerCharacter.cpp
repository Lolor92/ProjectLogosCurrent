#include "Character/PL_PlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Player/PL_PlayerState.h"


APL_PlayerCharacter::APL_PlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.SetTickFunctionEnable(false);
	
	// Camera boom.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>("Spring Arm");
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 750.f;
	SpringArm->SetRelativeLocation(FVector(0.f, 25.f, 50.f));
	SpringArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;
	
	// Player movement tuning.
	GetCharacterMovement()->RotationRate = FRotator(0.f, 600.f, 0.f);
	GetCharacterMovement()->AirControl = 0.5;
	GetCharacterMovement()->MaxAcceleration = 2600.f;
	GetCharacterMovement()->bOrientRotationToMovement = false;
}

void APL_PlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	
	// Server-side ASC initialization.
	InitializeAbilitySystem();
}

void APL_PlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	// Client-side ASC initialization.
	InitializeAbilitySystem();
}

void APL_PlayerCharacter::InitializeAbilitySystem()
{
	APL_PlayerState* PL_PlayerState = GetPlayerState<APL_PlayerState>();
	if (!PL_PlayerState) return;

	// Player characters use the PlayerState-owned ASC.
	AbilitySystemComponent = PL_PlayerState->GetAbilitySystemComponent();
	AttributeSet = PL_PlayerState->GetAttributeSet();

	if (!AbilitySystemComponent) return;

	AbilitySystemComponent->InitAbilityActorInfo(PL_PlayerState, this);

	if (HasAuthority()) InitializeDefaultAttributes();

	if (CombatComponent)
	{
		CombatComponent->InitializeCombat(this, AbilitySystemComponent);
	}
}
