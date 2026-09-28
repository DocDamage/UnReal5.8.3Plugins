#include "DocSurfaceFeedbackSubsystem.h"
#include "DocSurfaceFeedbackLog.h"
#include "Camera/CameraShakeBase.h"
#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DataValidation.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSurfaceFeedbackSubsystem)

namespace DocSurfaceTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Surface, "Surface", "DocSurfaceFeedback surfaces (mapped from the host's physical materials)");
	UE_DEFINE_GAMEPLAY_TAG(Wood, "Surface.Wood");
	UE_DEFINE_GAMEPLAY_TAG(Metal, "Surface.Metal");
	UE_DEFINE_GAMEPLAY_TAG(Dirt, "Surface.Dirt");
	UE_DEFINE_GAMEPLAY_TAG(Grass, "Surface.Grass");
	UE_DEFINE_GAMEPLAY_TAG(Water, "Surface.Water");
	UE_DEFINE_GAMEPLAY_TAG(Mud, "Surface.Mud");
	UE_DEFINE_GAMEPLAY_TAG(Concrete, "Surface.Concrete");
	UE_DEFINE_GAMEPLAY_TAG(Glass, "Surface.Glass");
	UE_DEFINE_GAMEPLAY_TAG(Sand, "Surface.Sand");
	UE_DEFINE_GAMEPLAY_TAG(Snow, "Surface.Snow");
	UE_DEFINE_GAMEPLAY_TAG(Custom, "Surface.Custom");
	UE_DEFINE_GAMEPLAY_TAG(Default, "Surface.Default");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event, "SurfaceEvent", "DocSurfaceFeedback event types");
	UE_DEFINE_GAMEPLAY_TAG(Event_Footstep, "SurfaceEvent.Footstep");
	UE_DEFINE_GAMEPLAY_TAG(Event_Landing, "SurfaceEvent.Landing");
	UE_DEFINE_GAMEPLAY_TAG(Event_Jump, "SurfaceEvent.Jump");
	UE_DEFINE_GAMEPLAY_TAG(Event_Slide, "SurfaceEvent.Slide");
	UE_DEFINE_GAMEPLAY_TAG(Event_Impact, "SurfaceEvent.Impact");
	UE_DEFINE_GAMEPLAY_TAG(Event_Projectile, "SurfaceEvent.Impact.Projectile");
	UE_DEFINE_GAMEPLAY_TAG(Event_Melee, "SurfaceEvent.Impact.Melee");
	UE_DEFINE_GAMEPLAY_TAG(Event_Vehicle, "SurfaceEvent.Vehicle");
	UE_DEFINE_GAMEPLAY_TAG(Event_Drag, "SurfaceEvent.Drag");
	UE_DEFINE_GAMEPLAY_TAG(Event_Break, "SurfaceEvent.Break");
	UE_DEFINE_GAMEPLAY_TAG(Event_Custom, "SurfaceEvent.Custom");
}

namespace DocSurfacePrivate
{
	int32 TagDepth(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid()) { return 0; }
		int32 Depth = 1;
		for (const TCHAR C : Tag.ToString()) { Depth += C == TEXT('.') ? 1 : 0; }
		return Depth;
	}

	bool IsFiniteVector(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }

	TArray<APlayerController*> LocalControllersNear(UWorld* World, const FVector& Location, float Range)
	{
		TArray<APlayerController*> Out;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC || !PC->IsLocalController()) { continue; }
			FVector View;
			FRotator Rot;
			PC->GetPlayerViewPoint(View, Rot);
			if (Range <= 0.f || FVector::Dist(View, Location) <= Range) { Out.Add(PC); }
		}
		return Out;
	}

	/** Native sound: SpawnSoundAtLocation (one-shot) or a kept component (continuous). */
	class FSoundExecutor final : public IDocSurfaceResponseExecutor
	{
	public:
		virtual EDocSurfaceAdmission Execute(UWorld* World, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response,
			UObject* Asset, float Scale, int32& OutInstance, FString& OutReason) override
		{
			USoundBase* Sound = Cast<USoundBase>(Asset);
			if (!Sound) { OutReason = TEXT("Asset is not a sound"); return EDocSurfaceAdmission::AssetUnavailable; }
			UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(World, Sound, Request.Location, FRotator::ZeroRotator, Scale,
				1.f, 0.f, nullptr, nullptr, /*bAutoDestroy*/ !Response.bContinuous);
			if (!Audio) { OutReason = TEXT("No audio device or sound refused"); return EDocSurfaceAdmission::Unsupported; }
			OutInstance = Next++;
			Instances.Add(OutInstance, Audio);
			return EDocSurfaceAdmission::Executed;
		}
		virtual bool IsInstanceActive(int32 Instance) const override
		{
			const UAudioComponent* Audio = Instances.FindRef(Instance).Get();
			return Audio && Audio->IsPlaying();
		}
		virtual void UpdateInstance(int32 Instance, const FVector& Location, float Scale) override
		{
			if (UAudioComponent* Audio = Instances.FindRef(Instance).Get()) { Audio->SetWorldLocation(Location); Audio->SetVolumeMultiplier(Scale); }
		}
		virtual void StopInstance(int32 Instance) override
		{
			TWeakObjectPtr<UAudioComponent> Weak;
			if (Instances.RemoveAndCopyValue(Instance, Weak) && Weak.IsValid()) { Weak->Stop(); Weak->DestroyComponent(); }
		}
	private:
		TMap<int32, TWeakObjectPtr<UAudioComponent>> Instances;
		int32 Next = 1;
	};

	class FDecalExecutor final : public IDocSurfaceResponseExecutor
	{
	public:
		virtual EDocSurfaceAdmission Execute(UWorld* World, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response,
			UObject* Asset, float Scale, int32& OutInstance, FString& OutReason) override
		{
			UMaterialInterface* Material = Cast<UMaterialInterface>(Asset);
			if (!Material) { OutReason = TEXT("Asset is not a material"); return EDocSurfaceAdmission::AssetUnavailable; }
			UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Material, Response.DecalSize * Scale, Request.Location,
				(-Request.Normal).Rotation(), Response.LifetimeSeconds);
			if (!Decal) { return EDocSurfaceAdmission::Unsupported; }
			OutInstance = Next++;
			Instances.Add(OutInstance, Decal);
			return EDocSurfaceAdmission::Executed;
		}
		virtual bool IsInstanceActive(int32 Instance) const override { return Instances.FindRef(Instance).IsValid(); }
		virtual void StopInstance(int32 Instance) override
		{
			TWeakObjectPtr<UDecalComponent> Weak;
			if (Instances.RemoveAndCopyValue(Instance, Weak) && Weak.IsValid()) { Weak->DestroyComponent(); }
		}
	private:
		TMap<int32, TWeakObjectPtr<UDecalComponent>> Instances;
		int32 Next = 1;
	};

	class FCameraShakeExecutor final : public IDocSurfaceResponseExecutor
	{
	public:
		virtual EDocSurfaceAdmission Execute(UWorld* World, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response,
			UObject* Asset, float Scale, int32& OutInstance, FString& OutReason) override
		{
			UClass* Class = Cast<UClass>(Asset);
			if (!Class || !Class->IsChildOf(UCameraShakeBase::StaticClass())) { OutReason = TEXT("Asset is not a camera shake class"); return EDocSurfaceAdmission::AssetUnavailable; }
			for (APlayerController* PC : LocalControllersNear(World, Request.Location, GetDefault<UDocSurfaceFeedbackSettings>()->CullDistance))
			{
				PC->ClientStartCameraShake(Class, Scale);
			}
			return EDocSurfaceAdmission::Executed;
		}
	};

	class FHapticExecutor final : public IDocSurfaceResponseExecutor
	{
	public:
		virtual EDocSurfaceAdmission Execute(UWorld* World, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response,
			UObject* Asset, float Scale, int32& OutInstance, FString& OutReason) override
		{
			UForceFeedbackEffect* Effect = Cast<UForceFeedbackEffect>(Asset);
			if (!Effect) { OutReason = TEXT("Asset is not a force feedback effect"); return EDocSurfaceAdmission::AssetUnavailable; }
			// Only the instigating local player feels it (never every player in range).
			const APawn* Pawn = Cast<APawn>(Request.Instigator.Get());
			APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
			if (!PC || !PC->IsLocalController()) { OutReason = TEXT("Instigator is not a local player"); return EDocSurfaceAdmission::Unsupported; }
			PC->ClientPlayForceFeedback(Effect);
			return EDocSurfaceAdmission::Executed;
		}
	};

	class FSpawnedActorExecutor final : public IDocSurfaceResponseExecutor
	{
	public:
		virtual bool IsCosmetic() const override { return false; }
		virtual EDocSurfaceAdmission Execute(UWorld* World, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse& Response,
			UObject* Asset, float Scale, int32& OutInstance, FString& OutReason) override
		{
			UClass* Class = Cast<UClass>(Asset);
			if (!Class || !Class->IsChildOf(AActor::StaticClass())) { OutReason = TEXT("Asset is not an actor class"); return EDocSurfaceAdmission::AssetUnavailable; }
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.Instigator = Cast<APawn>(Request.Instigator.Get());
			AActor* Spawned = World->SpawnActor<AActor>(Class, FTransform(Request.Normal.Rotation(), Request.Location), Params);
			if (!Spawned) { return EDocSurfaceAdmission::Unsupported; }
			if (Response.LifetimeSeconds > 0.f) { Spawned->SetLifeSpan(Response.LifetimeSeconds); }
			OutInstance = Next++;
			Instances.Add(OutInstance, Spawned);
			return EDocSurfaceAdmission::Executed;
		}
		virtual bool IsInstanceActive(int32 Instance) const override { const AActor* A = Instances.FindRef(Instance).Get(); return A && !A->IsActorBeingDestroyed(); }
		virtual void StopInstance(int32 Instance) override
		{
			TWeakObjectPtr<AActor> Weak;
			if (Instances.RemoveAndCopyValue(Instance, Weak) && Weak.IsValid()) { Weak->Destroy(); }
		}
	private:
		TMap<int32, TWeakObjectPtr<AActor>> Instances;
		int32 Next = 1;
	};
}

// ---------------------------------------------------------------------------
// Profile validation
// ---------------------------------------------------------------------------

TArray<FString> UDocSurfaceResponseProfile::FindAuthoringProblems() const
{
	TArray<FString> Problems;
	TSet<FName> Ids;
	for (int32 i = 0; i < Rules.Num(); ++i)
	{
		const FDocSurfaceResponseRule& A = Rules[i];
		bool bDuplicate = false;
		Ids.Add(A.RuleId, &bDuplicate);
		if (A.RuleId.IsNone() || bDuplicate) { Problems.Add(FString::Printf(TEXT("Rule %d: RuleId missing or duplicated"), i)); }
		if (A.Responses.Num() == 0) { Problems.Add(FString::Printf(TEXT("Rule %s has no responses"), *A.RuleId.ToString())); }
		if (A.MinMagnitude > A.MaxMagnitude) { Problems.Add(FString::Printf(TEXT("Rule %s: MinMagnitude > MaxMagnitude"), *A.RuleId.ToString())); }
		for (int32 j = i + 1; j < Rules.Num(); ++j)
		{
			const FDocSurfaceResponseRule& B = Rules[j];
			if (A.Priority == B.Priority && A.EventTag == B.EventTag && A.bExactEvent == B.bExactEvent && A.Surface == B.Surface
				&& A.bExactSurface == B.bExactSurface && A.RequiredContext == B.RequiredContext && A.BlockedContext == B.BlockedContext
				&& A.MinMagnitude == B.MinMagnitude && A.MaxMagnitude == B.MaxMagnitude)
			{
				Problems.Add(FString::Printf(TEXT("Rules %s and %s are equally ranked and ambiguous (RuleId decides)"), *A.RuleId.ToString(), *B.RuleId.ToString()));
			}
		}
	}
	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UDocSurfaceResponseProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FString& Problem : FindAuthoringProblems())
	{
		if (Problem.Contains(TEXT("ambiguous"))) { Context.AddWarning(FText::FromString(Problem)); }
		else { Context.AddError(FText::FromString(Problem)); Result = EDataValidationResult::Invalid; }
	}
	return Result;
}
#endif

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocSurfaceFeedbackSubsystem* UDocSurfaceFeedbackSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocSurfaceFeedbackSubsystem>() : nullptr;
}

bool UDocSurfaceFeedbackSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocSurfaceFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	using namespace DocSurfacePrivate;
	RegisterExecutor(EDocSurfaceResponseKind::Sound, MakeShared<FSoundExecutor>());
	RegisterExecutor(EDocSurfaceResponseKind::Decal, MakeShared<FDecalExecutor>());
	RegisterExecutor(EDocSurfaceResponseKind::CameraShake, MakeShared<FCameraShakeExecutor>());
	RegisterExecutor(EDocSurfaceResponseKind::Haptic, MakeShared<FHapticExecutor>());
	RegisterExecutor(EDocSurfaceResponseKind::SpawnedActor, MakeShared<FSpawnedActorExecutor>());
	// MetaSound, Niagara and Custom executors are registered by bridges.
}

void UDocSurfaceFeedbackSubsystem::Deinitialize()
{
	for (FQueuedRequest& Q : Queued)
	{
		if (Q.Handle.IsValid()) { Q.Handle->CancelHandle(); }
	}
	Queued.Reset();
	Continuous.ForEach([this](const FDocRequestHandle&, const FContinuous& C)
	{
		if (const TSharedPtr<IDocSurfaceResponseExecutor>* Exec = Executors.Find(C.Kind)) { (*Exec)->StopInstance(C.Instance); }
	});
	Continuous.Reset();
	Active.Reset();
	Super::Deinitialize();
}

TStatId UDocSurfaceFeedbackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocSurfaceFeedbackSubsystem, STATGROUP_Tickables);
}

void UDocSurfaceFeedbackSubsystem::RegisterExecutor(EDocSurfaceResponseKind Kind, TSharedPtr<IDocSurfaceResponseExecutor> Executor)
{
	if (Executor.IsValid()) { Executors.Add(Kind, Executor); }
}

void UDocSurfaceFeedbackSubsystem::UnregisterExecutor(EDocSurfaceResponseKind Kind)
{
	Executors.Remove(Kind);
}

// ---------------------------------------------------------------------------
// Resolution (pure)
// ---------------------------------------------------------------------------

FGameplayTag UDocSurfaceFeedbackSubsystem::ResolveSurface(const FDocSurfaceFeedbackRequest& Request, const UDocSurfaceMappingAsset* Mapping, EDocSurfaceOrigin& OutOrigin) const
{
	if (!Request.bHasHit)
	{
		OutOrigin = EDocSurfaceOrigin::NoHit;
		return FGameplayTag();
	}
	const UPhysicalMaterial* Material = Request.PhysicalMaterial.Get();
	static const TArray<EDocSurfaceMappingStage> DefaultOrder = { EDocSurfaceMappingStage::PhysicalMaterial, EDocSurfaceMappingStage::PhysicalSurfaceType, EDocSurfaceMappingStage::TagProvider };
	for (const EDocSurfaceMappingStage Stage : Mapping ? Mapping->Precedence : DefaultOrder)
	{
		switch (Stage)
		{
		case EDocSurfaceMappingStage::PhysicalMaterial:
			if (Mapping && Material)
			{
				const FSoftObjectPath Path(Material);
				for (const FDocSurfaceMaterialEntry& Entry : Mapping->Materials)
				{
					if (Entry.Material.ToSoftObjectPath() == Path && Entry.Surface.IsValid()) { OutOrigin = EDocSurfaceOrigin::PhysicalMaterial; return Entry.Surface; }
				}
			}
			break;
		case EDocSurfaceMappingStage::PhysicalSurfaceType:
			if (Mapping && Material)
			{
				// The host's surface table is read, never replaced: SurfaceType1 means whatever the project says.
				const EPhysicalSurface Type = Material->SurfaceType;
				for (const FDocSurfaceTypeEntry& Entry : Mapping->SurfaceTypes)
				{
					if (Entry.SurfaceType == Type && Entry.Surface.IsValid()) { OutOrigin = EDocSurfaceOrigin::PhysicalSurfaceType; return Entry.Surface; }
				}
			}
			break;
		case EDocSurfaceMappingStage::TagProvider:
			if (TagProvider.IsValid())
			{
				const FGameplayTag Tag = TagProvider->GetSurfaceTag(Request);
				if (Tag.IsValid()) { OutOrigin = EDocSurfaceOrigin::TagProvider; return Tag; }
			}
			break;
		}
	}
	if (Mapping && Mapping->DefaultSurface.IsValid())
	{
		OutOrigin = EDocSurfaceOrigin::ConfiguredDefault;
		return Mapping->DefaultSurface;
	}
	OutOrigin = EDocSurfaceOrigin::Unmapped;
	return FGameplayTag();
}

int32 UDocSurfaceFeedbackSubsystem::PickVariation(const FDocSurfaceResponse& Response, const FDocSurfaceFeedbackRequest& Request, const FName& RuleId) const
{
	const int32 Num = Response.Variations.Num();
	if (Num == 0) { return INDEX_NONE; }
	// Request-local stream: independent of every other system's random state.
	FRandomStream Stream((int32)HashCombine(GetTypeHash(Request.RequestId), HashCombine(GetTypeHash(Request.SourceId), GetTypeHash(RuleId))));
	int32 Index = Stream.RandRange(0, Num - 1);
	if (Response.bNoImmediateRepeat && Num > 1)
	{
		const int32* Last = LastVariation.Find(SourceKey(Request, RuleId) + FString::Printf(TEXT("/%d"), (int32)Response.Kind));
		if (Last && *Last == Index) { Index = (Index + 1) % Num; }
	}
	return Index;
}

FString UDocSurfaceFeedbackSubsystem::SourceKey(const FDocSurfaceFeedbackRequest& Request, const FName& RuleId) const
{
	return FString::Printf(TEXT("%s/%s/%s"), *GetPathNameSafe(Request.Instigator.Get()), *Request.SourceId.ToString(), *RuleId.ToString());
}

FDocSurfaceFeedbackResult UDocSurfaceFeedbackSubsystem::ResolveFeedback(const FDocSurfaceFeedbackRequest& Request, const UDocSurfaceResponseProfile* Profile) const
{
	using namespace DocSurfacePrivate;
	FDocSurfaceFeedbackResult Result;
	Result.RequestId = Request.RequestId;
	Result.EventTag = Request.EventTag;
	Result.Magnitude = Request.Magnitude;
	if (!Request.EventTag.IsValid() || !IsFiniteVector(Request.Location) || !IsFiniteVector(Request.Velocity) || !FMath::IsFinite(Request.Magnitude))
	{
		Result.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Event tag required; location/velocity/magnitude must be finite"));
		return Result;
	}
	Result.bNormalFallback = !IsFiniteVector(Request.Normal) || Request.Normal.IsNearlyZero();
	if (!Profile)
	{
		Result.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No response profile"));
		return Result;
	}
	Result.Surface = ResolveSurface(Request, Profile->Mapping, Result.SurfaceOrigin);

	TArray<FCandidate> Candidates;
	for (const FDocSurfaceResponseRule& Rule : Profile->Rules)
	{
		FCandidate C;
		C.Rule = &Rule;
		if (!Rule.EventTag.IsValid())
		{
			C.Path = EDocSurfaceMatchPath::Default;
		}
		else
		{
			const bool bEvent = Rule.bExactEvent ? Request.EventTag == Rule.EventTag : Request.EventTag.MatchesTag(Rule.EventTag);
			if (!bEvent) { continue; }
			C.EventSpecificity = TagDepth(Rule.EventTag) * 2 + (Request.EventTag == Rule.EventTag ? 1 : 0);
		}
		if (Rule.Surface.IsValid())
		{
			const bool bSurface = Result.Surface.IsValid() && (Rule.bExactSurface ? Result.Surface == Rule.Surface : Result.Surface.MatchesTag(Rule.Surface));
			if (!bSurface)
			{
				Result.Rejected.Add(FString::Printf(TEXT("%s: surface %s"), *Rule.RuleId.ToString(), *Result.Surface.ToString()));
				continue;
			}
			C.SurfaceSpecificity = TagDepth(Rule.Surface) * 2 + (Result.Surface == Rule.Surface ? 1 : 0);
		}
		if (!Request.ContextTags.HasAll(Rule.RequiredContext) || Request.ContextTags.HasAny(Rule.BlockedContext))
		{
			Result.Rejected.Add(FString::Printf(TEXT("%s: context"), *Rule.RuleId.ToString()));
			continue;
		}
		if (Request.Magnitude < Rule.MinMagnitude || Request.Magnitude > Rule.MaxMagnitude)
		{
			Result.Rejected.Add(FString::Printf(TEXT("%s: magnitude %.3f"), *Rule.RuleId.ToString(), Request.Magnitude));
			continue;
		}
		C.ContextCount = Rule.RequiredContext.Num();
		if (C.Path != EDocSurfaceMatchPath::Default)
		{
			C.Path = Rule.Surface.IsValid() ? (C.ContextCount > 0 ? EDocSurfaceMatchPath::EventSurfaceContext : EDocSurfaceMatchPath::EventSurface) : EDocSurfaceMatchPath::EventOnly;
		}
		Candidates.Add(C);
	}
	Result.CandidateCount = Candidates.Num();
	if (Candidates.Num() == 0)
	{
		Result.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No rule matched (including default)"));
		return Result;
	}
	// Ranking: priority → event specificity → surface specificity → satisfied context → stable RuleId.
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.Rule->Priority != B.Rule->Priority) { return A.Rule->Priority > B.Rule->Priority; }
		if ((A.Path == EDocSurfaceMatchPath::Default) != (B.Path == EDocSurfaceMatchPath::Default)) { return B.Path == EDocSurfaceMatchPath::Default; }
		if (A.EventSpecificity != B.EventSpecificity) { return A.EventSpecificity > B.EventSpecificity; }
		if (A.SurfaceSpecificity != B.SurfaceSpecificity) { return A.SurfaceSpecificity > B.SurfaceSpecificity; }
		if (A.ContextCount != B.ContextCount) { return A.ContextCount > B.ContextCount; }
		return A.Rule->RuleId.LexicalLess(B.Rule->RuleId);
	});
	const FCandidate& Winner = Candidates[0];
	Result.SelectedRule = Winner.Rule->RuleId;
	Result.MatchPath = Winner.Path;
	const float Alpha = FMath::Clamp(Request.Magnitude, 0.f, 1.f);
	for (const FDocSurfaceResponse& Response : Winner.Rule->Responses)
	{
		FDocSurfaceResponseDispatch Dispatch;
		Dispatch.Kind = Response.Kind;
		Dispatch.VariationIndex = PickVariation(Response, Request, Winner.Rule->RuleId);
		Dispatch.Asset = Response.Variations.IsValidIndex(Dispatch.VariationIndex) ? Response.Variations[Dispatch.VariationIndex].ToSoftObjectPath() : Response.Fallback.ToSoftObjectPath();
		Dispatch.Scale = FMath::Lerp(Response.MinScale, Response.MaxScale, Alpha);
		Result.Responses.Add(Dispatch);
	}
	Result.Result = FDocSystemResult::MakeSuccess();
	return Result;
}

// ---------------------------------------------------------------------------
// Submission and dispatch
// ---------------------------------------------------------------------------

bool UDocSurfaceFeedbackSubsystem::IsCosmeticAllowed(const FDocSurfaceFeedbackRequest& Request, FString& OutReason) const
{
	const UWorld* World = GetWorld();
	if (bForceNoCosmetics || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		OutReason = TEXT("No cosmetics on a dedicated server");
		return false;
	}
	const float Cull = GetDefault<UDocSurfaceFeedbackSettings>()->CullDistance;
	if (Cull <= 0.f)
	{
		return true;
	}
	TArray<FVector> Views = ViewOverride;
	if (!bUseViewOverride)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				FVector V;
				FRotator R;
				PC->GetPlayerViewPoint(V, R);
				Views.Add(V);
			}
		}
	}
	for (const FVector& View : Views)
	{
		if (FVector::Dist(View, Request.Location) <= Cull) { return true; }
	}
	OutReason = TEXT("Culled: beyond CullDistance from every local view");
	return false;
}

int32 UDocSurfaceFeedbackSubsystem::CountActive(EDocSurfaceResponseKind Kind) const
{
	int32 Count = 0;
	for (const FActiveInstance& A : Active) { Count += A.Kind == Kind ? 1 : 0; }
	return Count;
}

int32 UDocSurfaceFeedbackSubsystem::BudgetFor(EDocSurfaceResponseKind Kind) const
{
	const UDocSurfaceFeedbackSettings* S = GetDefault<UDocSurfaceFeedbackSettings>();
	switch (Kind)
	{
	case EDocSurfaceResponseKind::Sound:
	case EDocSurfaceResponseKind::MetaSound: return S->MaxActiveSounds;
	case EDocSurfaceResponseKind::Decal: return S->MaxActiveDecals;
	default: return S->MaxActiveEffects;
	}
}

FDocSurfaceFeedbackResult UDocSurfaceFeedbackSubsystem::SubmitFeedback(FDocSurfaceFeedbackRequest Request, UDocSurfaceResponseProfile* Profile)
{
	const UDocSurfaceFeedbackSettings* Settings = GetDefault<UDocSurfaceFeedbackSettings>();
	Request.RequestId = NextRequestId++;
	Request.SubmittedAt = Clock;
	const bool bNormalFallback = !DocSurfacePrivate::IsFiniteVector(Request.Normal) || Request.Normal.IsNearlyZero();
	if (bNormalFallback)
	{
		Request.Normal = FVector::UpVector; // explicit safe normal fallback (reported in the result)
	}

	FDocSurfaceFeedbackResult Result;
	if (Request.CorrelationId != 0)
	{
		if (const double* Seen = SeenCorrelations.Find(Request.CorrelationId); Seen && Clock - *Seen <= Settings->DedupeWindowSeconds)
		{
			Result.RequestId = Request.RequestId;
			Result.EventTag = Request.EventTag;
			Result.Result = FDocSystemResult::MakeNoChange(TEXT("Duplicate of a correlated request (predicted + confirmed)"));
			Record(Result);
			return Result;
		}
		SeenCorrelations.Add(Request.CorrelationId, Clock);
	}
	RecentRequests.RemoveAll([this](double T) { return T < Clock - 1.0; });
	if (Settings->MaxRequestsPerSecond > 0 && RecentRequests.Num() >= Settings->MaxRequestsPerSecond)
	{
		Result.RequestId = Request.RequestId;
		Result.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Request rate budget exceeded"));
		Record(Result);
		return Result;
	}
	RecentRequests.Add(Clock);

	Result = ResolveFeedback(Request, Profile);
	Result.bNormalFallback = bNormalFallback;
	if (!Result.Result.IsSuccess())
	{
		Record(Result);
		return Result;
	}

	const FDocSurfaceResponseRule* Rule = Profile->Rules.FindByPredicate([&Result](const FDocSurfaceResponseRule& R) { return R.RuleId == Result.SelectedRule; });
	if (Rule && Rule->CooldownSeconds > 0.f)
	{
		const FString Key = SourceKey(Request, Rule->RuleId);
		if (const double* Until = Cooldowns.Find(Key); Until && Clock < *Until)
		{
			for (FDocSurfaceResponseDispatch& D : Result.Responses) { D.Admission = EDocSurfaceAdmission::BudgetSuppressed; D.Reason = TEXT("Source cooldown"); }
			Record(Result);
			return Result;
		}
		Cooldowns.Add(Key, Clock + Rule->CooldownSeconds);
	}

	TArray<FSoftObjectPath> Missing;
	for (const FDocSurfaceResponseDispatch& D : Result.Responses)
	{
		if (!D.Asset.IsNull() && !D.Asset.ResolveObject()) { Missing.AddUnique(D.Asset); }
	}
	if (Missing.Num() > 0)
	{
		if (Queued.Num() >= Settings->MaxQueuedRequests)
		{
			for (FDocSurfaceResponseDispatch& D : Result.Responses) { D.Admission = EDocSurfaceAdmission::BudgetSuppressed; D.Reason = TEXT("Load queue full"); }
			Record(Result);
			return Result;
		}
		FQueuedRequest Q;
		Q.Request = Request;
		Q.Profile = Profile;
		Q.Result = Result;
		Q.LoadId = NextLoadId++;
		Q.bHadInstigator = Request.Instigator.IsValid();
		for (FDocSurfaceResponseDispatch& D : Q.Result.Responses) { D.Admission = EDocSurfaceAdmission::Pending; }
		const int64 LoadId = Q.LoadId;
		Queued.Add(MoveTemp(Q));
		TWeakObjectPtr<UDocSurfaceFeedbackSubsystem> WeakThis(this);
		auto Done = [WeakThis, LoadId]() { if (UDocSurfaceFeedbackSubsystem* Self = WeakThis.Get()) { Self->OnLoaded(LoadId); } };
		if (LoaderOverride)
		{
			LoaderOverride(Missing, Done);
		}
		else
		{
			TSharedPtr<FStreamableHandle> Handle = Streamable.RequestAsyncLoad(Missing, FStreamableDelegate::CreateLambda(Done));
			if (FQueuedRequest* Still = Queued.FindByPredicate([LoadId](const FQueuedRequest& R) { return R.LoadId == LoadId; })) { Still->Handle = Handle; }
		}
		FQueuedRequest* Pending = Queued.FindByPredicate([LoadId](const FQueuedRequest& R) { return R.LoadId == LoadId; });
		return Pending ? Pending->Result : Result;
	}

	Dispatch(Result, Request, Profile);
	Record(Result);
	return Result;
}

void UDocSurfaceFeedbackSubsystem::Dispatch(FDocSurfaceFeedbackResult& Result, const FDocSurfaceFeedbackRequest& Request, const UDocSurfaceResponseProfile* Profile)
{
	const FDocSurfaceResponseRule* Rule = Profile ? Profile->Rules.FindByPredicate([&Result](const FDocSurfaceResponseRule& R) { return R.RuleId == Result.SelectedRule; }) : nullptr;
	if (!Rule)
	{
		for (FDocSurfaceResponseDispatch& D : Result.Responses) { D.Admission = EDocSurfaceAdmission::Cancelled; D.Reason = TEXT("Profile changed or unloaded"); }
		return;
	}
	UWorld* World = GetWorld();
	const bool bAuthority = World && World->GetNetMode() != NM_Client;
	const TArray<TSoftClassPtr<AActor>>& AllowedClasses = GetDefault<UDocSurfaceFeedbackSettings>()->AllowedSpawnClasses;

	for (int32 i = 0; i < Result.Responses.Num() && i < Rule->Responses.Num(); ++i)
	{
		FDocSurfaceResponseDispatch& D = Result.Responses[i];
		const FDocSurfaceResponse& Response = Rule->Responses[i];
		// Each response is independent: a cosmetic failure never suppresses gameplay and vice versa.
		if (Response.IsGameplay())
		{
			if (!Request.bAuthoritativeIntent || !bAuthority)
			{
				D.Admission = EDocSurfaceAdmission::PermissionDenied;
				D.Reason = TEXT("Gameplay responses need authoritative intent on the authority");
			}
			else if (!GameplayProvider.IsValid())
			{
				D.Admission = EDocSurfaceAdmission::Unsupported;
				D.Reason = TEXT("No gameplay provider installed");
			}
			else
			{
				const FDocSystemResult Applied = GameplayProvider->ApplySurfaceGameplayResponse(Request, Response);
				D.Admission = Applied.IsSuccess() ? EDocSurfaceAdmission::Executed : EDocSurfaceAdmission::PermissionDenied;
				D.Reason = Applied.Diagnostic;
			}
			continue;
		}
		const TSharedPtr<IDocSurfaceResponseExecutor>* Executor = Executors.Find(Response.Kind);
		if (!Executor || !Executor->IsValid())
		{
			D.Admission = EDocSurfaceAdmission::Unsupported;
			D.Reason = TEXT("No executor registered for this response kind (bridge not installed?)");
			continue;
		}
		FString Reason;
		if ((*Executor)->IsCosmetic() && !IsCosmeticAllowed(Request, Reason))
		{
			D.Admission = Reason.StartsWith(TEXT("Culled")) ? EDocSurfaceAdmission::BudgetSuppressed : EDocSurfaceAdmission::Unsupported;
			D.Reason = Reason;
			continue;
		}
		if (CountActive(Response.Kind) >= BudgetFor(Response.Kind))
		{
			D.Admission = EDocSurfaceAdmission::BudgetSuppressed;
			D.Reason = TEXT("Active budget reached");
			continue;
		}
		UObject* Asset = D.Asset.ResolveObject();
		if (!Asset && !Response.Fallback.IsNull())
		{
			Asset = Response.Fallback.Get();
			D.Asset = Response.Fallback.ToSoftObjectPath();
		}
		if (!Asset)
		{
			D.Admission = EDocSurfaceAdmission::AssetUnavailable;
			D.Reason = TEXT("Variation and fallback unavailable");
			continue;
		}
		if (Response.Kind == EDocSurfaceResponseKind::SpawnedActor)
		{
			const UClass* Class = Cast<UClass>(Asset);
			const bool bAllowed = Class && AllowedClasses.ContainsByPredicate([Class](const TSoftClassPtr<AActor>& C) { const UClass* A = C.Get(); return A && Class->IsChildOf(A); });
			if (!bAllowed)
			{
				D.Admission = EDocSurfaceAdmission::PermissionDenied;
				D.Reason = TEXT("Spawn class not allowlisted");
				continue;
			}
		}
		int32 Instance = 0;
		D.Admission = (*Executor)->Execute(World, Request, Response, Asset, D.Scale, Instance, D.Reason);
		if (D.Admission == EDocSurfaceAdmission::Executed)
		{
			LastVariation.Add(SourceKey(Request, Rule->RuleId) + FString::Printf(TEXT("/%d"), (int32)Response.Kind), D.VariationIndex);
			if (Instance != 0)
			{
				Active.Add(FActiveInstance{ Response.Kind, Instance });
				if (Response.bContinuous)
				{
					FContinuous C;
					C.Kind = Response.Kind;
					C.Instance = Instance;
					C.Source = Request.Instigator;
					C.bHadSource = Request.Instigator.IsValid();
					C.MinScale = Response.MinScale;
					C.MaxScale = Response.MaxScale;
					D.Handle = Continuous.Add(this, C);
				}
			}
		}
	}
}

void UDocSurfaceFeedbackSubsystem::OnLoaded(int64 LoadId)
{
	const int32 Index = Queued.IndexOfByPredicate([LoadId](const FQueuedRequest& R) { return R.LoadId == LoadId; });
	if (Index == INDEX_NONE)
	{
		return; // cancelled or expired: a late asset callback never spawns orphan feedback
	}
	FQueuedRequest Q = MoveTemp(Queued[Index]);
	Queued.RemoveAt(Index);
	const bool bExpired = Clock - Q.Request.SubmittedAt > GetDefault<UDocSurfaceFeedbackSettings>()->QueuedRequestTTLSeconds;
	const bool bOwnerLost = Q.bHadInstigator && !Q.Request.Instigator.IsValid();
	if (bExpired || bOwnerLost || !Q.Profile.IsValid())
	{
		for (FDocSurfaceResponseDispatch& D : Q.Result.Responses) { D.Admission = EDocSurfaceAdmission::Cancelled; D.Reason = bOwnerLost ? TEXT("Source unloaded") : TEXT("Stale (TTL)"); }
	}
	else
	{
		Dispatch(Q.Result, Q.Request, Q.Profile.Get());
	}
	Record(Q.Result);
}

FDocSystemResult UDocSurfaceFeedbackSubsystem::UpdateContinuous(FDocRequestHandle Handle, FVector Location, float Magnitude)
{
	const FContinuous* C = Continuous.Find(Handle, this);
	if (!C)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Continuous response ended"));
	}
	if (const TSharedPtr<IDocSurfaceResponseExecutor>* Exec = Executors.Find(C->Kind))
	{
		(*Exec)->UpdateInstance(C->Instance, Location, FMath::Lerp(C->MinScale, C->MaxScale, FMath::Clamp(Magnitude, 0.f, 1.f)));
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSurfaceFeedbackSubsystem::StopContinuous(FDocRequestHandle Handle)
{
	FContinuous Removed;
	if (!Continuous.Remove(Handle, this, &Removed))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already stopped"));
	}
	if (const TSharedPtr<IDocSurfaceResponseExecutor>* Exec = Executors.Find(Removed.Kind))
	{
		(*Exec)->StopInstance(Removed.Instance); // generation-safe: the handle owns exactly this instance
	}
	Active.RemoveAll([&Removed](const FActiveInstance& A) { return A.Kind == Removed.Kind && A.Instance == Removed.Instance; });
	return FDocSystemResult::MakeSuccess();
}

void UDocSurfaceFeedbackSubsystem::Record(const FDocSurfaceFeedbackResult& Result)
{
#if !UE_BUILD_SHIPPING
	const int32 Size = GetDefault<UDocSurfaceFeedbackSettings>()->DebugHistorySize;
	if (Size > 0)
	{
		History.Add(Result);
		if (History.Num() > Size) { History.RemoveAt(0, History.Num() - Size); }
	}
#endif
	OnFeedbackDispatchedNative.Broadcast(Result);
}

void UDocSurfaceFeedbackSubsystem::TickFeedback(float DeltaSeconds)
{
	Clock += FMath::Max(0.f, DeltaSeconds);
	const UDocSurfaceFeedbackSettings* Settings = GetDefault<UDocSurfaceFeedbackSettings>();

	Active.RemoveAll([this](const FActiveInstance& A)
	{
		const TSharedPtr<IDocSurfaceResponseExecutor>* Exec = Executors.Find(A.Kind);
		return !Exec || !(*Exec)->IsInstanceActive(A.Instance);
	});

	for (int32 i = Queued.Num() - 1; i >= 0; --i)
	{
		FQueuedRequest& Q = Queued[i];
		const bool bOwnerLost = Q.bHadInstigator && !Q.Request.Instigator.IsValid();
		if (bOwnerLost || Clock - Q.Request.SubmittedAt > Settings->QueuedRequestTTLSeconds)
		{
			if (Q.Handle.IsValid()) { Q.Handle->CancelHandle(); }
			for (FDocSurfaceResponseDispatch& D : Q.Result.Responses) { D.Admission = EDocSurfaceAdmission::Cancelled; D.Reason = bOwnerLost ? TEXT("Source unloaded") : TEXT("Stale (TTL)"); }
			const FDocSurfaceFeedbackResult Cancelled = Q.Result;
			Queued.RemoveAt(i);
			Record(Cancelled);
		}
	}

	// Continuous responses stop with their source.
	TArray<FDocRequestHandle> Orphans;
	Continuous.ForEach([&Orphans](const FDocRequestHandle& H, const FContinuous& C)
	{
		if (C.bHadSource && !C.Source.IsValid()) { Orphans.Add(H); }
	});
	for (const FDocRequestHandle& H : Orphans) { StopContinuous(H); }

	for (auto It = SeenCorrelations.CreateIterator(); It; ++It)
	{
		if (Clock - It.Value() > Settings->DedupeWindowSeconds) { It.RemoveCurrent(); }
	}
	for (auto It = Cooldowns.CreateIterator(); It; ++It)
	{
		if (Clock >= It.Value()) { It.RemoveCurrent(); }
	}
}

// ---------------------------------------------------------------------------
// Producers
// ---------------------------------------------------------------------------

UDocSurfaceFeedbackComponent::UDocSurfaceFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // no default per-frame trace
}

FDocSurfaceFeedbackRequest UDocSurfaceFeedbackComponent::MakeRequest(FGameplayTag EventTag, float Magnitude, FName SourceId) const
{
	FDocSurfaceFeedbackRequest Request;
	Request.EventTag = EventTag;
	Request.Magnitude = Magnitude;
	Request.Instigator = GetOwner();
	Request.ContextTags = ContextTags;
	Request.SourceId = SourceId;
	return Request;
}

FDocSurfaceFeedbackResult UDocSurfaceFeedbackComponent::TriggerManual(FGameplayTag EventTag, FVector Location, FVector Normal, float Magnitude, FName SourceId)
{
	FDocSurfaceFeedbackRequest Request = MakeRequest(EventTag, Magnitude, SourceId);
	Request.Location = Location;
	Request.Normal = Normal;
	UDocSurfaceFeedbackSubsystem* Surface = UDocSurfaceFeedbackSubsystem::Get(this);
	return Surface ? Surface->SubmitFeedback(Request, Profile) : FDocSurfaceFeedbackResult();
}

FDocSurfaceFeedbackResult UDocSurfaceFeedbackComponent::TriggerFromHit(FGameplayTag EventTag, const FHitResult& Hit, float Magnitude, FName SourceId)
{
	FDocSurfaceFeedbackRequest Request = MakeRequest(EventTag, Magnitude, SourceId);
	Request.bHasHit = Hit.bBlockingHit;
	Request.Location = Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : FVector(Hit.TraceEnd);
	Request.Normal = Hit.bBlockingHit ? FVector(Hit.ImpactNormal) : FVector::UpVector;
	Request.PhysicalMaterial = Hit.PhysMaterial;
	Request.HitComponent = Hit.GetComponent();
	UDocSurfaceFeedbackSubsystem* Surface = UDocSurfaceFeedbackSubsystem::Get(this);
	return Surface ? Surface->SubmitFeedback(Request, Profile) : FDocSurfaceFeedbackResult();
}

FDocSurfaceFeedbackResult UDocSurfaceFeedbackComponent::TriggerTraced(FGameplayTag EventTag, FVector From, float Magnitude, FName SourceId, bool bFromAnimNotify)
{
	if (bFromAnimNotify && bDistanceFootsteps && EventTag.MatchesTag(DocSurfaceTags::Event_Footstep))
	{
		FDocSurfaceFeedbackResult Ignored;
		Ignored.EventTag = EventTag;
		Ignored.Result = FDocSystemResult::MakeNoChange(TEXT("Distance producer owns footsteps for this component"));
		return Ignored;
	}
	UWorld* World = GetWorld();
	FHitResult Hit;
	if (World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DocSurfaceTrace), bTraceComplex, GetOwner());
		Params.bReturnPhysicalMaterial = true;
		World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, TraceDistance), TraceChannel, Params);
	}
	if (!Hit.bBlockingHit)
	{
		Hit.TraceEnd = From;
	}
	return TriggerFromHit(EventTag, Hit, Magnitude, SourceId);
}

int32 UDocSurfaceFeedbackComponent::UpdateMovement(FVector Location, bool bGrounded, FVector Velocity)
{
	if (!bHasLast)
	{
		bHasLast = true;
		LastLocation = Location;
		bWasGrounded = bGrounded;
		return 0;
	}
	const float Delta = FVector::Dist(Location, LastLocation);
	LastLocation = Location;
	if (Delta > TeleportThresholdCm)
	{
		// Teleport: reset phase; never a burst of steps.
		Accumulated = 0.f;
		bWasGrounded = bGrounded;
		return 0;
	}
	int32 Emitted = 0;
	if (!bGrounded)
	{
		LastAirVelocity = Velocity;
		bWasGrounded = false;
		return 0;
	}
	if (!bWasGrounded)
	{
		bWasGrounded = true;
		Accumulated = 0.f;
		const float FallSpeed = FMath::Max(-LastAirVelocity.Z, -Velocity.Z);
		if (FallSpeed >= MinLandingSpeed)
		{
			TriggerTraced(DocSurfaceTags::Event_Landing, Location, FallSpeed / FMath::Max(1.f, MinLandingSpeed * 4.f), NAME_None);
			++Emitted; // once per validated transition
		}
		return Emitted;
	}
	if (!bDistanceFootsteps)
	{
		return 0;
	}
	Accumulated += Delta;
	if (Accumulated >= StrideCm)
	{
		const FName Source = StepSources.Num() > 0 ? StepSources[NextStepSource++ % StepSources.Num()] : NAME_None;
		TriggerTraced(DocSurfaceTags::Event_Footstep, Location, 1.f, Source);
		++Emitted;
		// Bounded: at most one step per update; a hitch drops the backlog instead of bursting.
		Accumulated = FMath::Fmod(Accumulated - StrideCm, StrideCm);
	}
	return Emitted;
}

void UDocSurfaceAnimNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	UDocSurfaceFeedbackComponent* Producer = Owner ? Owner->FindComponentByClass<UDocSurfaceFeedbackComponent>() : nullptr;
	if (!Producer)
	{
		return; // editor previews and actors without a producer: nothing
	}
	const FVector From = (!Socket.IsNone() && MeshComp->DoesSocketExist(Socket)) ? MeshComp->GetSocketLocation(Socket) : MeshComp->GetComponentLocation();
	Producer->TriggerTraced(EventTag, From, Magnitude, Socket, /*bFromAnimNotify*/ true);
}

FString UDocSurfaceAnimNotify::GetNotifyName_Implementation() const
{
	return EventTag.IsValid() ? FString::Printf(TEXT("Surface %s"), *EventTag.ToString()) : FString(TEXT("Doc Surface Feedback"));
}
