// Copyright ProjectLogos

#include "Player/PL_PlayerState.h"
#include "GAS/ASC/PL_AbilitySystemComponent.h"
#include "GAS/Attribute/PL_AttributeSet.h"

APL_PlayerState::APL_PlayerState()
{
	// PlayerState replicates independently from the pawn, so keep ASC updates responsive.
	SetNetUpdateFrequency(100.f);

	AbilitySystemComponent = CreateDefaultSubobject<UPL_AbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UPL_AttributeSet>("AttributeSet");
}
