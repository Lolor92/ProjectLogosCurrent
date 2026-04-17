// Copyright ProjectLogos

#include "Combat/Runtime/PL_LocalHitFeedbackRuntime.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/PL_BaseCharacter.h"
#include "Combat/Components/PL_CombatComponent.h"
#include "Combat/Data/PL_TagReactionData.h"
#include "Components/PoseableMeshComponent.h"
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

	void CopyMaterialsFromMesh(USkinnedMeshComponent* SourceMesh, USkinnedMeshComponent* TargetMesh)
	{
		if (!SourceMesh || !TargetMesh) return;

		for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
		{
			TargetMesh->SetMaterial(MaterialIndex, SourceMesh->GetMaterial(MaterialIndex));
		}
	}

	bool StartProxyToRealPoseBlend(
		USkeletalMeshComponent* RealMesh,
		USkeletalMeshComponent* ProxyMesh,
		const bool bRealMeshWasHidden,
		const bool bRealMeshWasTickEnabled,
		const EVisibilityBasedAnimTickOption PreviousRealMeshTickOption,
		const float BlendDuration = 0.10f)
	{
		if (!RealMesh || !ProxyMesh) return false;

		UWorld* World = RealMesh->GetWorld();
		if (!World || World->bIsTearingDown) return false;

		USkeletalMesh* SkeletalMeshAsset = RealMesh->GetSkeletalMeshAsset();
		if (!SkeletalMeshAsset) return false;

		UPoseableMeshComponent* BlendMesh = NewObject<UPoseableMeshComponent>(RealMesh->GetOwner());
		if (!BlendMesh) return false;

		BlendMesh->SetSkeletalMesh(SkeletalMeshAsset);
		BlendMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BlendMesh->SetGenerateOverlapEvents(false);
		BlendMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		CopyMaterialsFromMesh(RealMesh, BlendMesh);

		BlendMesh->RegisterComponentWithWorld(World);

		if (USceneComponent* RealAttachParent = RealMesh->GetAttachParent())
		{
			BlendMesh->AttachToComponent(
				RealAttachParent,
				FAttachmentTransformRules::KeepRelativeTransform,
				RealMesh->GetAttachSocketName());

			BlendMesh->SetRelativeTransform(RealMesh->GetRelativeTransform());
		}
		else
		{
			BlendMesh->SetWorldTransform(RealMesh->GetComponentTransform());
		}

		struct FBonePoseBlendData
		{
			FName BoneName = NAME_None;
			FTransform StartTransform = FTransform::Identity;
		};

		TArray<FBonePoseBlendData> BoneBlendData;
		BoneBlendData.Reserve(RealMesh->GetNumBones());

		const FTransform BlendMeshWorldTransform = BlendMesh->GetComponentTransform();

		for (int32 BoneIndex = 0; BoneIndex < RealMesh->GetNumBones(); ++BoneIndex)
		{
			const FName BoneName = RealMesh->GetBoneName(BoneIndex);
			if (BoneName == NAME_None) continue;
			if (ProxyMesh->GetBoneIndex(BoneName) == INDEX_NONE) continue;

			const FTransform ProxyBoneWorldTransform = ProxyMesh->GetSocketTransform(BoneName, RTS_World);
			const FTransform ProxyBoneComponentTransform =
				ProxyBoneWorldTransform.GetRelativeTransform(BlendMeshWorldTransform);

			FBonePoseBlendData& BlendData = BoneBlendData.AddDefaulted_GetRef();
			BlendData.BoneName = BoneName;
			BlendData.StartTransform = ProxyBoneComponentTransform;

			BlendMesh->SetBoneTransformByName(
				BoneName,
				ProxyBoneComponentTransform,
				EBoneSpaces::ComponentSpace);
		}

		// The blend mesh now visually matches the proxy, so we can remove the proxy without a pop.
		ProxyMesh->DestroyComponent();

		const float BlendStartTime = World->GetTimeSeconds();
		TSharedRef<FTimerHandle> BlendTimerHandle = MakeShared<FTimerHandle>();

		World->GetTimerManager().SetTimer(
			*BlendTimerHandle,
			[RealMesh, BlendMesh, BoneBlendData, BlendStartTime, BlendDuration,
			 bRealMeshWasHidden, bRealMeshWasTickEnabled, PreviousRealMeshTickOption, BlendTimerHandle]()
			{
				if (!RealMesh || !BlendMesh)
				{
					return;
				}

				UWorld* World = RealMesh->GetWorld();
				if (!World || World->bIsTearingDown)
				{
					RestoreRealMeshAfterProxySwap(
						RealMesh,
						bRealMeshWasHidden,
						bRealMeshWasTickEnabled,
						PreviousRealMeshTickOption);

					if (BlendMesh)
					{
						BlendMesh->DestroyComponent();
					}

					return;
				}

				// Keep the bridge mesh following the real mesh/capsule during the blend.
				if (USceneComponent* RealAttachParent = RealMesh->GetAttachParent())
				{
					if (BlendMesh->GetAttachParent() != RealAttachParent)
					{
						BlendMesh->AttachToComponent(
							RealAttachParent,
							FAttachmentTransformRules::KeepWorldTransform,
							RealMesh->GetAttachSocketName());
					}

					BlendMesh->SetRelativeTransform(RealMesh->GetRelativeTransform());
				}
				else
				{
					BlendMesh->SetWorldTransform(RealMesh->GetComponentTransform());
				}

				const float Elapsed = World->GetTimeSeconds() - BlendStartTime;
				const float Alpha = FMath::Clamp(Elapsed / FMath::Max(BlendDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);

				// Smoothstep. Less robotic than raw linear interpolation.
				const float SmoothAlpha = Alpha * Alpha * (3.f - 2.f * Alpha);

				const FTransform BlendMeshWorldTransform = BlendMesh->GetComponentTransform();

				for (const FBonePoseBlendData& BlendData : BoneBlendData)
				{
					if (BlendData.BoneName == NAME_None) continue;

					const FTransform RealBoneWorldTransform =
						RealMesh->GetSocketTransform(BlendData.BoneName, RTS_World);

					const FTransform RealBoneComponentTransform =
						RealBoneWorldTransform.GetRelativeTransform(BlendMeshWorldTransform);

					FTransform BlendedTransform;
					BlendedTransform.Blend(
						BlendData.StartTransform,
						RealBoneComponentTransform,
						SmoothAlpha);

					BlendMesh->SetBoneTransformByName(
						BlendData.BoneName,
						BlendedTransform,
						EBoneSpaces::ComponentSpace);
				}

				if (Alpha < 1.f)
				{
					return;
				}

				World->GetTimerManager().ClearTimer(*BlendTimerHandle);

				RestoreRealMeshAfterProxySwap(
					RealMesh,
					bRealMeshWasHidden,
					bRealMeshWasTickEnabled,
					PreviousRealMeshTickOption);

				BlendMesh->DestroyComponent();

				UE_LOG(LogTemp, Warning,
					TEXT("Predicted reaction proxy pose-blended back to real mesh. RealMesh=%s Duration=%.3f"),
					*GetNameSafe(RealMesh),
					BlendDuration);
			},
			0.016f,
			true);

		return true;
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
		RealMesh->SetHiddenInGame(bRealMeshWasHidden, true);
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
		RealMesh->SetHiddenInGame(bRealMeshWasHidden, true);
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

			World->GetTimerManager().SetTimer(
				*PollHandle,
				[WeakRealMesh, WeakProxyMesh, bRealMeshWasHidden, bRealMeshWasTickEnabled, PreviousRealMeshTickOption, MontageToPlay, ProxyFinishedTime, MaxHoldTime, PollHandle, ServerReactionFinishedTime, PostServerReactionSettleTime]()
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
						if (USceneComponent* RealAttachParent = RealMesh->GetAttachParent())
						{
							if (ProxyMesh->GetAttachParent() != RealAttachParent)
							{
								ProxyMesh->AttachToComponent(
									RealAttachParent,
									FAttachmentTransformRules::KeepWorldTransform,
									RealMesh->GetAttachSocketName());
							}

							ProxyMesh->SetRelativeTransform(RealMesh->GetRelativeTransform());
						}
						else
						{
							ProxyMesh->SetWorldTransform(RealMesh->GetComponentTransform());
						}

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

					if (Now - *ServerReactionFinishedTime < PostServerReactionSettleTime && !bTimedOut)
					{
						return;
					}

					World->GetTimerManager().ClearTimer(*PollHandle);

					if (ProxyMesh)
					{
						constexpr float PoseBlendDuration = 0.10f;

						if (StartProxyToRealPoseBlend(
							RealMesh,
							ProxyMesh,
							bRealMeshWasHidden,
							bRealMeshWasTickEnabled,
							PreviousRealMeshTickOption,
							PoseBlendDuration))
						{
							UE_LOG(LogTemp, Warning,
								TEXT("Predicted reaction proxy started pose blend back. RealMesh=%s Montage=%s TimedOut=%s"),
								*GetNameSafe(RealMesh),
								*GetNameSafe(MontageToPlay),
								bTimedOut ? TEXT("TRUE") : TEXT("FALSE"));

							return;
						}
					}

					RestoreRealMeshAfterProxySwap(
						RealMesh,
						bRealMeshWasHidden,
						bRealMeshWasTickEnabled,
						PreviousRealMeshTickOption);

					if (ProxyMesh)
					{
						ProxyMesh->DestroyComponent();
					}

					UE_LOG(LogTemp, Warning,
						TEXT("Predicted reaction proxy swapped back without pose blend. RealMesh=%s Montage=%s TimedOut=%s"),
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
