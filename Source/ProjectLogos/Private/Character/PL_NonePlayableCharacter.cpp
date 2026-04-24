// Copyright ProjectLogos

#include "Character/PL_NonePlayableCharacter.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "GAS/Attribute/PL_AttributeSet.h"

APL_NonePlayableCharacter::APL_NonePlayableCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	AggroSensor = CreateDefaultSubobject<USphereComponent>(TEXT("AggroSensor"));
	AggroSensor->SetupAttachment(GetRootComponent());
	AggroSensor->InitSphereRadius(AggroRadius);
	AggroSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	AggroSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
	AggroSensor->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	AggroSensor->SetGenerateOverlapEvents(true);
	AggroSensor->SetCanEverAffectNavigation(false);
	
	// Non-player characters own their ASC directly.
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UPL_AttributeSet>("AttributeSet");
}

void APL_NonePlayableCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (AggroSensor)
	{
		AggroSensor->SetSphereRadius(AggroRadius);
		AggroSensor->SetGenerateOverlapEvents(HasAuthority());

		if (HasAuthority())
		{
			AggroSensor->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleAggroSensorBeginOverlap);
			AggroSensor->OnComponentEndOverlap.AddDynamic(this, &ThisClass::HandleAggroSensorEndOverlap);
			AggroSensor->UpdateOverlaps();
		}
		else
		{
			CachedAggroCandidates.Reset();
		}
	}

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

float APL_NonePlayableCharacter::ChooseDesiredCombatRange() const
{
	const float MinRange = FMath::Min(DesiredCombatRangeMin, DesiredCombatRangeMax);
	const float MaxRange = FMath::Max(DesiredCombatRangeMin, DesiredCombatRangeMax);
	if (FMath::IsNearlyEqual(MinRange, MaxRange))
	{
		return MinRange;
	}

	const float RandomAlpha = FMath::FRand();
	if (DesiredCombatRangeBias == EPLDesiredCombatRangeBias::Random || DesiredCombatRangeBiasStrength <= 0.f)
	{
		return FMath::Lerp(MinRange, MaxRange, RandomAlpha);
	}

	float BiasAlpha = 0.5f;
	switch (DesiredCombatRangeBias)
	{
	case EPLDesiredCombatRangeBias::Min:
		BiasAlpha = 0.f;
		break;

	case EPLDesiredCombatRangeBias::Mid:
		BiasAlpha = 0.5f;
		break;

	case EPLDesiredCombatRangeBias::Max:
		BiasAlpha = 1.f;
		break;

	case EPLDesiredCombatRangeBias::Random:
	default:
		break;
	}

	const float BlendedAlpha = FMath::Lerp(RandomAlpha, BiasAlpha, FMath::Clamp(DesiredCombatRangeBiasStrength, 0.f, 1.f));
	return FMath::Lerp(MinRange, MaxRange, BlendedAlpha);
}

TSubclassOf<UGameplayAbility> APL_NonePlayableCharacter::ChooseCombatAbilityForDistance(const float DistanceToTarget) const
{
	if (CombatComponent && CombatComponent->IsCrowdControlActive())
	{
		return nullptr;
	}

	const float MinRange = FMath::Min(DesiredCombatRangeMin, DesiredCombatRangeMax);
	const float MaxRange = FMath::Max(DesiredCombatRangeMin, DesiredCombatRangeMax);
	const float RangeSpan = MaxRange - MinRange;

	if (FMath::IsNearlyZero(RangeSpan))
	{
		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(MidRangeAbilities))
		{
			return AbilityClass;
		}

		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(CloseRangeAbilities))
		{
			return AbilityClass;
		}

		return ChooseWeightedAbilityFromPool(FarRangeAbilities);
	}

	if (DistanceToTarget < MinRange)
	{
		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(CloseRangeAbilities))
		{
			return AbilityClass;
		}

		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(MidRangeAbilities))
		{
			return AbilityClass;
		}

		return ChooseWeightedAbilityFromPool(FarRangeAbilities);
	}

	if (DistanceToTarget > MaxRange)
	{
		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(FarRangeAbilities))
		{
			return AbilityClass;
		}

		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(MidRangeAbilities))
		{
			return AbilityClass;
		}

		return ChooseWeightedAbilityFromPool(CloseRangeAbilities);
	}

	const float NormalizedDistance = (DistanceToTarget - MinRange) / RangeSpan;

	if (NormalizedDistance <= (1.f / 3.f))
	{
		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(CloseRangeAbilities))
		{
			return AbilityClass;
		}

		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(MidRangeAbilities))
		{
			return AbilityClass;
		}

		return ChooseWeightedAbilityFromPool(FarRangeAbilities);
	}

	if (NormalizedDistance >= (2.f / 3.f))
	{
		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(FarRangeAbilities))
		{
			return AbilityClass;
		}

		if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(MidRangeAbilities))
		{
			return AbilityClass;
		}

		return ChooseWeightedAbilityFromPool(CloseRangeAbilities);
	}

	if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(MidRangeAbilities))
	{
		return AbilityClass;
	}

	if (TSubclassOf<UGameplayAbility> AbilityClass = ChooseWeightedAbilityFromPool(CloseRangeAbilities))
	{
		return AbilityClass;
	}

	return ChooseWeightedAbilityFromPool(FarRangeAbilities);
}

APL_BaseCharacter* APL_NonePlayableCharacter::FindBestTargetInAggroRadius()
{
	if (HasAuthority() && CachedAggroCandidates.IsEmpty())
	{
		RefreshAggroCandidatesFromSensor();
	}

	const float AggroRadiusSquared = FMath::Square(AggroRadius);
	const FVector MyLocation = GetActorLocation();

	APL_BaseCharacter* BestTarget = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

	TArray<TWeakObjectPtr<APL_BaseCharacter>> InvalidCandidates;

	for (const TWeakObjectPtr<APL_BaseCharacter>& CandidatePtr : CachedAggroCandidates)
	{
		APL_BaseCharacter* Candidate = CandidatePtr.Get();
		if (!IsValidAggroCandidate(Candidate))
		{
			InvalidCandidates.Add(CandidatePtr);
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(MyLocation, Candidate->GetActorLocation());
		if (DistanceSquared > AggroRadiusSquared)
		{
			continue;
		}

		if (DistanceSquared >= BestDistanceSquared)
		{
			continue;
		}

		BestDistanceSquared = DistanceSquared;
		BestTarget = Candidate;
	}

	for (const TWeakObjectPtr<APL_BaseCharacter>& CandidatePtr : InvalidCandidates)
	{
		CachedAggroCandidates.Remove(CandidatePtr);
	}

	return BestTarget;
}

void APL_NonePlayableCharacter::SetAggroRadius(const float NewRadius)
{
	AggroRadius = FMath::Max(0.f, NewRadius);

	if (AggroSensor)
	{
		AggroSensor->SetSphereRadius(AggroRadius, true);
	}
}

int32 APL_NonePlayableCharacter::GetCachedAggroCandidateCount() const
{
	int32 Count = 0;

	for (const TWeakObjectPtr<APL_BaseCharacter>& CandidatePtr : CachedAggroCandidates)
	{
		if (IsValidAggroCandidate(CandidatePtr.Get()))
		{
			++Count;
		}
	}

	return Count;
}

void APL_NonePlayableCharacter::HandleAggroSensorBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AddAggroCandidate(Cast<APL_BaseCharacter>(OtherActor));
}

void APL_NonePlayableCharacter::HandleAggroSensorEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	RemoveAggroCandidate(Cast<APL_BaseCharacter>(OtherActor));
}

bool APL_NonePlayableCharacter::IsValidAggroCandidate(const APL_BaseCharacter* Candidate) const
{
	if (!Candidate || Candidate == this)
	{
		return false;
	}

	if (bRequireTargetableTargets && !Candidate->CanBeTargeted())
	{
		return false;
	}

	if (bRequireAliveTargets && !Candidate->IsAlive())
	{
		return false;
	}

	if (bRequireHostileTargets && !IsHostileTo(Candidate))
	{
		return false;
	}

	return true;
}

TSubclassOf<UGameplayAbility> APL_NonePlayableCharacter::ChooseWeightedAbilityFromPool(
	const TArray<FPLWeightedGameplayAbilityEntry>& AbilityPool) const
{
	float TotalWeight = 0.f;
	for (const FPLWeightedGameplayAbilityEntry& Entry : AbilityPool)
	{
		if (!Entry.AbilityClass || Entry.SelectionWeight <= 0.f)
		{
			continue;
		}

		TotalWeight += Entry.SelectionWeight;
	}

	if (TotalWeight <= 0.f)
	{
		return nullptr;
	}

	float WeightRoll = FMath::FRandRange(0.f, TotalWeight);
	for (const FPLWeightedGameplayAbilityEntry& Entry : AbilityPool)
	{
		if (!Entry.AbilityClass || Entry.SelectionWeight <= 0.f)
		{
			continue;
		}

		WeightRoll -= Entry.SelectionWeight;
		if (WeightRoll <= 0.f)
		{
			return Entry.AbilityClass;
		}
	}

	for (int32 Index = AbilityPool.Num() - 1; Index >= 0; --Index)
	{
		const FPLWeightedGameplayAbilityEntry& Entry = AbilityPool[Index];
		if (Entry.AbilityClass && Entry.SelectionWeight > 0.f)
		{
			return Entry.AbilityClass;
		}
	}

	return nullptr;
}

void APL_NonePlayableCharacter::RefreshAggroCandidatesFromSensor()
{
	if (!AggroSensor || !HasAuthority())
	{
		return;
	}

	AggroSensor->UpdateOverlaps();

	TArray<AActor*> OverlappingActors;
	AggroSensor->GetOverlappingActors(OverlappingActors, APL_BaseCharacter::StaticClass());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		AddAggroCandidate(Cast<APL_BaseCharacter>(OverlappingActor));
	}
}

void APL_NonePlayableCharacter::AddAggroCandidate(APL_BaseCharacter* Candidate)
{
	if (!IsValidAggroCandidate(Candidate))
	{
		return;
	}

	CachedAggroCandidates.Add(Candidate);
}

void APL_NonePlayableCharacter::RemoveAggroCandidate(APL_BaseCharacter* Candidate)
{
	if (!Candidate)
	{
		return;
	}

	CachedAggroCandidates.Remove(Candidate);
}
