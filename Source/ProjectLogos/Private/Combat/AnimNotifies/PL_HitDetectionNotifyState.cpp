// Copyright ProjectLogos

#include "Combat/AnimNotifies/PL_HitDetectionNotifyState.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Character/PL_BaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"


void UPL_HitDetectionNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	UPL_CombatComponent* CombatComponent = ResolveCombatComponent(MeshComp);

	if (CombatComponent)
	{
		CombatComponent->BeginHitDetectionWindow(this, MeshComp, TraceSocketName, HitWindowSettings);
	}
}

void UPL_HitDetectionNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	UPL_CombatComponent* CombatComponent = ResolveCombatComponent(MeshComp);

	if (CombatComponent)
	{
		CombatComponent->EndHitDetectionWindow(this, MeshComp);
	}
}

UPL_CombatComponent* UPL_HitDetectionNotifyState::ResolveCombatComponent(USkeletalMeshComponent* MeshComp)
{
	if (!MeshComp) return nullptr;

	if (APL_BaseCharacter* Character = Cast<APL_BaseCharacter>(MeshComp->GetOwner()))
	{
		return Character->GetCombatComponent();
	}

	return MeshComp->GetOwner()
		? MeshComp->GetOwner()->FindComponentByClass<UPL_CombatComponent>()
		: nullptr;
}
