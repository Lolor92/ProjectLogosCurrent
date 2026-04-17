// Copyright ProjectLogos

#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Combat/Data/PL_TagReactionData.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Tag/PL_NativeTags.h"
#include "TimerManager.h"

namespace
{
	void RestoreRealMeshAfterProxySwap(
		USkeletalMeshComponent* RealMesh,
		const bool bRealMeshWasHidden,
		const bool bRealMeshWasTickEnabled,
		const EVisibilityBasedAnimTickOption PreviousRealMeshTickOption)
	{
		if (!RealMesh) return;

		RealMesh->SetHiddenInGame(bRealMeshWasHidden, true);
		RealMesh->VisibilityBasedAnimTickOption = PreviousRealMeshTickOption;
		RealMesh->SetComponentTickEnabled(bRealMeshWasTickEnabled);
	}

	void RefreshMeshVisualPose(USkeletalMeshComponent* Mesh)
	{
		if (!Mesh) return;

		Mesh->TickAnimation(0.f, false);
		Mesh->RefreshBoneTransforms();
		Mesh->UpdateComponentToWorld();
		Mesh->MarkRenderTransformDirty();
		Mesh->MarkRenderDynamicDataDirty();
	}

	FTransform BlendWorldTransform(
		const FTransform& StartTransform,
		const FTransform& TargetTransform,
		const float Alpha)
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
		const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, ClampedAlpha);

		const FVector Location = FMath::Lerp(
			StartTransform.GetLocation(),
			TargetTransform.GetLocation(),
			SmoothAlpha);

		const FQuat Rotation = FQuat::Slerp(
			StartTransform.GetRotation(),
			TargetTransform.GetRotation(),
			SmoothAlpha).GetNormalized();

		const FVector Scale = FMath::Lerp(
			StartTransform.GetScale3D(),
			TargetTransform.GetScale3D(),
			SmoothAlpha);

		return FTransform(Rotation, Location, Scale);
	}

	void MatchProxyAttachmentToRealMesh(
		USkeletalMeshComponent* ProxyMesh,
		const USkeletalMeshComponent* RealMesh)
	{
		if (!ProxyMesh || !RealMesh) return;

		USceneComponent* RealAttachParent = RealMesh->GetAttachParent();

		if (!RealAttachParent)
		{
			return;
		}

		if (ProxyMesh->GetAttachParent() == RealAttachParent)
		{
			return;
		}

		ProxyMesh->AttachToComponent(
			RealAttachParent,
			FAttachmentTransformRules::KeepWorldTransform,
			RealMesh->GetAttachSocketName());
	}

	void RevealRealMeshAndDestroyProxyNextFrame(
		USkeletalMeshComponent* RealMesh,
		USkeletalMeshComponent* ProxyMesh,
		const bool bRealMeshWasHidden,
		const bool bRealMeshWasTickEnabled,
		const EVisibilityBasedAnimTickOption PreviousRealMeshTickOption)
	{
		if (!RealMesh) return;

		UWorld* World = RealMesh->GetWorld();

		RestoreRealMeshAfterProxySwap(
			RealMesh,
			bRealMeshWasHidden,
			bRealMeshWasTickEnabled,
			PreviousRealMeshTickOption);

		RefreshMeshVisualPose(RealMesh);

		if (!ProxyMesh)
		{
			return;
		}

		ProxyMesh->SetHiddenInGame(false, true);
		ProxyMesh->SetComponentTickEnabled(true);
		RefreshMeshVisualPose(ProxyMesh);

		if (!World || World->bIsTearingDown)
		{
			ProxyMesh->DestroyComponent();
			return;
		}

		TWeakObjectPtr<USkeletalMeshComponent> WeakProxyMesh = ProxyMesh;

		FTimerHandle DestroyProxyHandle;

		World->GetTimerManager().SetTimer(
			DestroyProxyHandle,
			[WeakProxyMesh]()
			{
				if (USkeletalMeshComponent* ProxyMeshPtr = WeakProxyMesh.Get())
				{
					ProxyMeshPtr->DestroyComponent();
				}
			},
			0.03f,
			false);
	}
}

FPLLocalHitFeedbackRuntime::FPLLocalHitFeedbackRuntime(UPL_CombatComponent& InCombatComponent)
	: CombatComponent(InCombatComponent)
{
}

void FPLLocalHitFeedbackRuntime::PlayPredictedHitFeedback(
	AActor* HitActor,
	const FHitResult& HitResult,
	const FPLHitWindowSettings& HitWindowSettings)
{
	if (!HitActor) return;

	APL_BaseCharacter* OwnerCharacter = CombatComponent.GetOwningCharacter();
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled()) return;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	PruneOldEntries();
	if (WasRecentlyPredictedHit(HitActor)) return;

	const float Now = World->GetTimeSeconds();

	FPLLocalHitFeedbackEntry& Entry = RecentPredictedHits.AddDefaulted_GetRef();
	Entry.TargetActor = HitActor;
	Entry.TimeSeconds = Now;

	PlayPredictedReactionMontage(HitActor, HitWindowSettings);

	for (const FPLHitWindowGameplayCue& Cue : HitWindowSettings.GameplayCuesToExecute)
	{
		if (Cue.TriggerTiming != EPLHitWindowCueTriggerTiming::OnHit) continue;
		if (!Cue.CueTag.MatchesTag(TAG_GameplayCue_Hit_CameraShake)) continue;

		ExecuteLocalCameraShakeCue(Cue, HitResult);
	}

	if (HitWindowSettings.HitStopSettings.IsEnabled())
	{
		OwnerCharacter->ApplyHitStop(
			HitWindowSettings.HitStopSettings.Duration,
			HitWindowSettings.HitStopSettings.TimeScale);
	}
}

void FPLLocalHitFeedbackRuntime::PlayPredictedReactionMontage(
	AActor* HitActor,
	const FPLHitWindowSettings& HitWindowSettings) const
{
	if (!HitActor) return;

	APL_BaseCharacter* OwnerCharacter = CombatComponent.GetOwningCharacter();
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled()) return;

	APL_BaseCharacter* TargetCharacter = Cast<APL_BaseCharacter>(HitActor);
	if (!TargetCharacter) return;

	for (const FPLHitWindowGameplayEffect& GameplayEffectToApply : HitWindowSettings.GameplayEffectsToApply)
	{
		if (!GameplayEffectToApply.GameplayEffectClass) continue;

		FGameplayTag TriggerTag;
		if (!FindTriggerTagFromGameplayEffectClass(GameplayEffectToApply.GameplayEffectClass, TriggerTag)) continue;

		const FPL_TagReactionBinding* ReactionBinding = CombatComponent.FindTagReactionBindingForTriggerTag(TriggerTag);
		if (!ReactionBinding) continue;

		UAnimMontage* MontageToPlay = ReactionBinding->Ability.PredictedReactionMontage;
		if (!MontageToPlay) continue;

		if (!PlayPredictedReactionProxyMontage(TargetCharacter, MontageToPlay)) return;

		UE_LOG(LogTemp, Warning,
			TEXT("Predicted reaction montage played. Attacker=%s Target=%s GE=%s TriggerTag=%s Montage=%s"),
			*GetNameSafe(OwnerCharacter),
			*GetNameSafe(TargetCharacter),
			*GetNameSafe(GameplayEffectToApply.GameplayEffectClass),
			*TriggerTag.ToString(),
			*GetNameSafe(MontageToPlay));

		return;
	}
}

bool FPLLocalHitFeedbackRuntime::PlayPredictedReactionProxyMontage(
	APL_BaseCharacter* TargetCharacter,
	UAnimMontage* MontageToPlay) const
{
	if (!TargetCharacter || !MontageToPlay) return false;

	USkeletalMeshComponent* RealMesh = TargetCharacter->GetMesh();
	if (!RealMesh || !RealMesh->GetSkeletalMeshAsset()) return false;

	UWorld* World = TargetCharacter->GetWorld();
	if (!World || World->bIsTearingDown) return false;

	// Do not create another proxy if one is already hidden/playing from this same local prediction window.
	UAnimInstance* RealAnimInstance = RealMesh->GetAnimInstance();
	if (RealAnimInstance && RealAnimInstance->Montage_IsPlaying(MontageToPlay)) return false;

	USkeletalMeshComponent* ProxyMesh = NewObject<USkeletalMeshComponent>(TargetCharacter);
	if (!ProxyMesh) return false;

	ProxyMesh->SetSkeletalMesh(RealMesh->GetSkeletalMeshAsset());
	ProxyMesh->SetAnimInstanceClass(RealMesh->GetAnimClass());
	ProxyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	ProxyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProxyMesh->SetGenerateOverlapEvents(false);
	ProxyMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	for (int32 MaterialIndex = 0; MaterialIndex < RealMesh->GetNumMaterials(); ++MaterialIndex)
	{
		ProxyMesh->SetMaterial(MaterialIndex, RealMesh->GetMaterial(MaterialIndex));
	}

	ProxyMesh->RegisterComponentWithWorld(World);

	if (USceneComponent* RealAttachParent = RealMesh->GetAttachParent())
	{
		ProxyMesh->AttachToComponent(
			RealAttachParent,
			FAttachmentTransformRules::KeepRelativeTransform,
			RealMesh->GetAttachSocketName());

		ProxyMesh->SetRelativeTransform(RealMesh->GetRelativeTransform());
	}
	else
	{
		ProxyMesh->SetWorldTransform(RealMesh->GetComponentTransform());
	}

	ProxyMesh->bPauseAnims = false;
	ProxyMesh->SetComponentTickEnabled(true);

	// Local-only visual swap.
	// Keep the hidden real mesh fully animated so swap-back does not reveal a stale pose.
	const bool bRealMeshWasHidden = RealMesh->bHiddenInGame;
	const bool bRealMeshWasTickEnabled = RealMesh->IsComponentTickEnabled();
	const EVisibilityBasedAnimTickOption PreviousRealMeshTickOption = RealMesh->VisibilityBasedAnimTickOption;

	RealMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	RealMesh->SetComponentTickEnabled(true);
	RealMesh->SetHiddenInGame(true, true);

	UAnimInstance* ProxyAnimInstance = ProxyMesh->GetAnimInstance();
	if (!ProxyAnimInstance)
	{
		RestoreRealMeshAfterProxySwap(
			RealMesh,
			bRealMeshWasHidden,
			bRealMeshWasTickEnabled,
			PreviousRealMeshTickOption);
		ProxyMesh->DestroyComponent();
		return false;
	}

	// Important:
	// This keeps root motion visible in the proxy pose without letting this local-only prediction
	// drive the replicated NPC capsule/movement.
	ProxyAnimInstance->SetRootMotionMode(ERootMotionMode::NoRootMotionExtraction);

	const float MontageLength = ProxyAnimInstance->Montage_Play(MontageToPlay, 1.f);
	if (MontageLength <= 0.f)
	{
		RestoreRealMeshAfterProxySwap(
			RealMesh,
			bRealMeshWasHidden,
			bRealMeshWasTickEnabled,
			PreviousRealMeshTickOption);
		ProxyMesh->DestroyComponent();
		return false;
	}

	TWeakObjectPtr<USkeletalMeshComponent> WeakRealMesh = RealMesh;
	TWeakObjectPtr<USkeletalMeshComponent> WeakProxyMesh = ProxyMesh;

	FOnMontageBlendingOutStarted BlendOutDelegate;
	BlendOutDelegate.BindLambda(
		[WeakRealMesh, WeakProxyMesh, bRealMeshWasHidden, bRealMeshWasTickEnabled, PreviousRealMeshTickOption, MontageToPlay](UAnimMontage* Montage, bool bInterrupted)
		{
			if (Montage != MontageToPlay) return;

			USkeletalMeshComponent* ProxyMeshPtr = WeakProxyMesh.Get();
			if (ProxyMeshPtr)
			{
				// Keep the proxy alive and ticking after the predicted montage finishes.
				// This avoids the visible statue/freeze while waiting for the hidden real mesh
				// to finish the server-confirmed reaction.
				ProxyMeshPtr->bPauseAnims = false;
				ProxyMeshPtr->SetComponentTickEnabled(true);
			}

			USkeletalMeshComponent* RealMeshPtr = WeakRealMesh.Get();
			if (!RealMeshPtr)
			{
				if (ProxyMeshPtr)
				{
					ProxyMeshPtr->DestroyComponent();
				}

				return;
			}

			UWorld* World = RealMeshPtr->GetWorld();
			if (!World || World->bIsTearingDown)
			{
				RealMeshPtr->SetHiddenInGame(bRealMeshWasHidden, true);
				RealMeshPtr->VisibilityBasedAnimTickOption = PreviousRealMeshTickOption;
				RealMeshPtr->SetComponentTickEnabled(bRealMeshWasTickEnabled);

				if (ProxyMeshPtr)
				{
					ProxyMeshPtr->DestroyComponent();
				}

				return;
			}

			const float ProxyFinishedTime = World->GetTimeSeconds();
			const float MaxHoldTime = 3.0f;

			TSharedRef<FTimerHandle> PollHandle = MakeShared<FTimerHandle>();
			TSharedRef<float> ServerReactionFinishedTime = MakeShared<float>(-1.f);
			const float PostServerReactionSettleTime = 0.08f;
			TSharedRef<bool> bHandoffStarted = MakeShared<bool>(false);
			TSharedRef<float> HandoffStartTime = MakeShared<float>(-1.f);
			TSharedRef<FTransform> HandoffProxyStartWorldTransform = MakeShared<FTransform>();
			const float HandoffBlendTime = 0.12f;

			World->GetTimerManager().SetTimer(
				*PollHandle,
				[WeakRealMesh, WeakProxyMesh, bRealMeshWasHidden, bRealMeshWasTickEnabled, PreviousRealMeshTickOption, MontageToPlay, ProxyFinishedTime, MaxHoldTime, PollHandle, ServerReactionFinishedTime, PostServerReactionSettleTime, bHandoffStarted, HandoffStartTime, HandoffProxyStartWorldTransform, HandoffBlendTime]()
				{
					USkeletalMeshComponent* RealMesh = WeakRealMesh.Get();
					USkeletalMeshComponent* ProxyMesh = WeakProxyMesh.Get();

					if (!RealMesh)
					{
						if (ProxyMesh)
						{
							ProxyMesh->DestroyComponent();
						}

						return;
					}

					UWorld* World = RealMesh->GetWorld();
					if (!World || World->bIsTearingDown)
					{
						RealMesh->SetHiddenInGame(bRealMeshWasHidden, true);
						RealMesh->VisibilityBasedAnimTickOption = PreviousRealMeshTickOption;
						RealMesh->SetComponentTickEnabled(bRealMeshWasTickEnabled);

						if (ProxyMesh)
						{
							ProxyMesh->DestroyComponent();
						}

						return;
					}

					if (ProxyMesh)
					{
						MatchProxyAttachmentToRealMesh(ProxyMesh, RealMesh);

						ProxyMesh->bPauseAnims = false;
						ProxyMesh->SetComponentTickEnabled(true);
					}

					UAnimInstance* RealAnimInstance = RealMesh->GetAnimInstance();

					const bool bRealMeshStillPlayingServerReaction =
						RealAnimInstance && RealAnimInstance->Montage_IsPlaying(MontageToPlay);

					const float ElapsedSinceProxyFinished = World->GetTimeSeconds() - ProxyFinishedTime;
					const bool bTimedOut = ElapsedSinceProxyFinished >= MaxHoldTime;

					if (bRealMeshStillPlayingServerReaction && !bTimedOut)
					{
						*ServerReactionFinishedTime = -1.f;
						return;
					}

					const float Now = World->GetTimeSeconds();

					if (*ServerReactionFinishedTime < 0.f)
					{
						*ServerReactionFinishedTime = Now;
						return;
					}

					const float TimeSinceServerReactionFinished = Now - *ServerReactionFinishedTime;

					if (TimeSinceServerReactionFinished < PostServerReactionSettleTime && !bTimedOut)
					{
						return;
					}

					if (ProxyMesh && !*bHandoffStarted)
					{
						*bHandoffStarted = true;
						*HandoffStartTime = Now;
						*HandoffProxyStartWorldTransform = ProxyMesh->GetComponentTransform();

						UE_LOG(LogTemp, Warning,
							TEXT("Predicted reaction proxy handoff blend started. RealMesh=%s ProxyMesh=%s Montage=%s"),
							*GetNameSafe(RealMesh),
							*GetNameSafe(ProxyMesh),
							*GetNameSafe(MontageToPlay));
					}

					if (ProxyMesh && *bHandoffStarted && !bTimedOut)
					{
						const float HandoffElapsed = Now - *HandoffStartTime;
						const float HandoffAlpha = FMath::Clamp(HandoffElapsed / HandoffBlendTime, 0.f, 1.f);

						const FTransform BlendedProxyTransform = BlendWorldTransform(
							*HandoffProxyStartWorldTransform,
							RealMesh->GetComponentTransform(),
							HandoffAlpha);

						ProxyMesh->SetWorldTransform(
							BlendedProxyTransform,
							false,
							nullptr,
							ETeleportType::TeleportPhysics);

						RefreshMeshVisualPose(ProxyMesh);

						if (HandoffAlpha < 1.f)
						{
							return;
						}
					}

					World->GetTimerManager().ClearTimer(*PollHandle);

					RevealRealMeshAndDestroyProxyNextFrame(
						RealMesh,
						ProxyMesh,
						bRealMeshWasHidden,
						bRealMeshWasTickEnabled,
						PreviousRealMeshTickOption);

					UE_LOG(LogTemp, Warning,
						TEXT("Predicted reaction proxy swapped back. RealMesh=%s Montage=%s TimedOut=%s"),
						*GetNameSafe(RealMesh),
						*GetNameSafe(MontageToPlay),
						bTimedOut ? TEXT("TRUE") : TEXT("FALSE"));
				},
				0.03f,
				true);
		});

	ProxyAnimInstance->Montage_SetBlendingOutDelegate(BlendOutDelegate, MontageToPlay);

	UE_LOG(LogTemp, Warning,
		TEXT("Predicted reaction proxy montage started. Target=%s Montage=%s Length=%.3f"),
		*GetNameSafe(TargetCharacter),
		*GetNameSafe(MontageToPlay),
		MontageLength);

	return true;
}

void FPLLocalHitFeedbackRuntime::RegisterPredictedReactionMontage(UAnimMontage* Montage)
{
	if (!Montage) return;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	RecentPredictedReactionMontages.RemoveAll(
		[Montage](const FPLPredictedReactionMontageEntry& Entry)
		{
			return Entry.Montage.Get() == Montage;
		});

	FPLPredictedReactionMontageEntry& Entry = RecentPredictedReactionMontages.AddDefaulted_GetRef();
	Entry.Montage = Montage;
	Entry.TimeSeconds = Now;
	Entry.bHasLoggedSuppression = false;

	UE_LOG(LogTemp, Warning, TEXT("Registered predicted reaction montage receipt. Owner=%s Montage=%s Time=%.3f"),
		*GetNameSafe(CombatComponent.GetOwner()),
		*GetNameSafe(Montage),
		Now);
}

bool FPLLocalHitFeedbackRuntime::ShouldSuppressPredictedReactionMontageReplay(const UAnimMontage* Montage)
{
	if (!Montage) return false;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return false;

	PruneOldReactionMontageEntries();

	const float Now = World->GetTimeSeconds();

	for (FPLPredictedReactionMontageEntry& Entry : RecentPredictedReactionMontages)
	{
		if (Entry.Montage.Get() != Montage) continue;

		const float ElapsedSincePredictedPlaySeconds = Now - Entry.TimeSeconds;
		if (ElapsedSincePredictedPlaySeconds <= 0.f) return false;

		if (!Entry.bHasLoggedSuppression)
		{
			Entry.bHasLoggedSuppression = true;

			UE_LOG(LogTemp, Warning,
				TEXT("Suppressing duplicate server reaction montage before replay. Owner=%s Montage=%s Elapsed=%.3f"),
				*GetNameSafe(CombatComponent.GetOwner()),
				*GetNameSafe(Montage),
				ElapsedSincePredictedPlaySeconds);
		}

		return true;
	}

	return false;
}

bool FPLLocalHitFeedbackRuntime::FindTriggerTagFromGameplayEffectClass(
	TSubclassOf<UGameplayEffect> GameplayEffectClass,
	FGameplayTag& OutTriggerTag) const
{
	OutTriggerTag = FGameplayTag();

	if (!GameplayEffectClass) return false;

	const UGameplayEffect* GameplayEffectCDO = GameplayEffectClass->GetDefaultObject<UGameplayEffect>();
	if (!GameplayEffectCDO) return false;

	const FGameplayTagContainer& GrantedTags = GameplayEffectCDO->GetGrantedTags();
	const FGameplayTag TriggerRootTag = FGameplayTag::RequestGameplayTag(TEXT("Trigger"), false);

	for (const FGameplayTag& GrantedTag : GrantedTags)
	{
		if (!GrantedTag.IsValid()) continue;

		if (TriggerRootTag.IsValid() && GrantedTag.MatchesTag(TriggerRootTag))
		{
			OutTriggerTag = GrantedTag;
			return true;
		}

		if (GrantedTag.ToString().StartsWith(TEXT("Trigger.")))
		{
			OutTriggerTag = GrantedTag;
			return true;
		}
	}

	return false;
}

void FPLLocalHitFeedbackRuntime::PruneOldReactionMontageEntries()
{
	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	RecentPredictedReactionMontages.RemoveAll(
		[this, Now](const FPLPredictedReactionMontageEntry& Entry)
		{
			return !Entry.Montage.IsValid() ||
				Now - Entry.TimeSeconds > PredictedReactionReplaySuppressionTime;
		});
}

bool FPLLocalHitFeedbackRuntime::WasRecentlyPredictedHit(AActor* HitActor) const
{
	if (!HitActor) return false;

	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return false;

	const float Now = World->GetTimeSeconds();

	for (const FPLLocalHitFeedbackEntry& Entry : RecentPredictedHits)
	{
		if (Entry.TargetActor == HitActor && Now - Entry.TimeSeconds <= DuplicateSuppressionTime)
		{
			return true;
		}
	}

	return false;
}

void FPLLocalHitFeedbackRuntime::PruneOldEntries()
{
	const UWorld* World = CombatComponent.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	RecentPredictedHits.RemoveAll([this, Now](const FPLLocalHitFeedbackEntry& Entry)
	{
		return !Entry.TargetActor.IsValid() || Now - Entry.TimeSeconds > DuplicateSuppressionTime;
	});

	PruneOldReactionMontageEntries();
}

void FPLLocalHitFeedbackRuntime::ExecuteLocalCameraShakeCue(const FPLHitWindowGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	if (!ShouldExecuteLocalCameraShakeCue()) return;

	UAbilitySystemComponent* AbilitySystemComponent = CombatComponent.GetAbilitySystemComponent();
	if (!AbilitySystemComponent) return;

	AActor* const OwnerActor = CombatComponent.GetOwner();

	FGameplayCueParameters Params;
	Params.Instigator = OwnerActor;
	Params.EffectCauser = OwnerActor;
	Params.SourceObject = &CombatComponent;
	Params.Location = GetCueSpawnLocation(Cue, HitResult);
	Params.Normal = FVector(HitResult.ImpactNormal);
	Params.TargetAttachComponent = Cue.bAttachToTarget ? HitResult.GetComponent() : nullptr;

	AbilitySystemComponent->InvokeGameplayCueEvent(Cue.CueTag, EGameplayCueEvent::Executed, Params);
}

bool FPLLocalHitFeedbackRuntime::ShouldExecuteLocalCameraShakeCue() const
{
	if (CombatComponent.GetNetMode() == NM_DedicatedServer) return false;

	const UAbilitySystemComponent* AbilitySystemComponent = CombatComponent.GetAbilitySystemComponent();
	if (!AbilitySystemComponent) return false;

	const AActor* LocalCueActor = AbilitySystemComponent->GetAvatarActor_Direct();
	if (!LocalCueActor)
	{
		LocalCueActor = AbilitySystemComponent->GetOwnerActor();
	}

	const APawn* LocalCuePawn = Cast<APawn>(LocalCueActor);
	return LocalCuePawn && LocalCuePawn->IsLocallyControlled();
}

FVector FPLLocalHitFeedbackRuntime::GetCueSpawnLocation(const FPLHitWindowGameplayCue& Cue,
	const FHitResult& HitResult) const
{
	AActor* const OwnerActor = CombatComponent.GetOwner();
	FVector SpawnLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;

	switch (Cue.SpawnPoint)
	{
	case EPLHitWindowCueSpawnPoint::OwnerLocation:
		SpawnLocation = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
		break;

	case EPLHitWindowCueSpawnPoint::HitImpactPoint:
		SpawnLocation = HitResult.ImpactPoint;
		break;

	case EPLHitWindowCueSpawnPoint::HitLocation:
		SpawnLocation = HitResult.Location;
		break;

	default:
		break;
	}

	return SpawnLocation + Cue.LocationOffset;
}
