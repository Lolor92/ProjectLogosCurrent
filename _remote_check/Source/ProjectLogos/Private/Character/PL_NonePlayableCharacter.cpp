// Copyright ProjectLogos

#include "Character/PL_NonePlayableCharacter.h"
#include "AbilitySystemComponent.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GAS/Attribute/PL_AttributeSet.h"

APL_NonePlayableCharacter::APL_NonePlayableCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	
	// Non-player characters own their ASC directly.
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UPL_AttributeSet>("AttributeSet");
}

void APL_NonePlayableCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	InitAbilityActorInfo();
}

void APL_NonePlayableCharacter::InitAbilityActorInfo()
{
	if (!AbilitySystemComponent) return;

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	
	if (HasAuthority()) InitializeDefaultAttributes();
	
	if (CombatComponent)
	{
		CombatComponent->InitializeCombat(this, AbilitySystemComponent);
	}
}
