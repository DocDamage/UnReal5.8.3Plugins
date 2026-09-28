#include "DocAcousticSpaceSubsystem.h"
#include "DocAcousticSpacesLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace DocAcousticPrivate
{
	static const FName ReasonSameSpace(TEXT("SameSpace"));
	static const FName ReasonConnected(TEXT("Connected"));
	static const FName ReasonNoRoute(TEXT("NoRoute"));
	static const FName ReasonHopLimit(TEXT("HopLimit"));
	static const FName ReasonBudget(TEXT("Budget"));
	static const FName ReasonBudgetApproximate(TEXT("BudgetApproximate"));
	static const FName ReasonUnresolved(TEXT("Unresolved"));

	static double WorldTime(const UWorld* World)
	{
		return World ? World->GetTimeSeconds() : 0.0;
	}
}

void UDocAcousticSpaceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TopologyRevision = 1;
}

void UDocAcousticSpaceSubsystem::Deinitialize()
{
	Spaces.Empty();
	Portals.Empty();
	Emitters.Empty();
	Listeners.Empty();
	TrackedMembership.Empty();
	ActiveClaims.Empty();
	PathCache.Empty();
	DebugPaths.Empty();
	PlaybackAdapter = nullptr;
	Super::Deinitialize();
}

void UDocAcousticSpaceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bAutoUpdate)
	{
		AdvanceSmoothing(DeltaTime);
	}
}

TStatId UDocAcousticSpaceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocAcousticSpaceSubsystem, STATGROUP_Tickables);
}

void UDocAcousticSpaceSubsystem::BumpTopology()
{
	++TopologyRevision;
}

FBox UDocAcousticSpaceSubsystem::GetSpaceBounds(const FSpaceEntry& Entry) const
{
	if (const UDocAcousticSpaceComponent* Comp = Entry.Component.Get())
	{
		return Comp->BoundsBox;
	}
	return Entry.Bounds;
}

int32 UDocAcousticSpaceSubsystem::GetSpacePriority(const FSpaceEntry& Entry) const
{
	if (const UDocAcousticSpaceComponent* Comp = Entry.Component.Get())
	{
		return Comp->Priority;
	}
	return Entry.Priority;
}

FDocAcousticPortalLink UDocAcousticSpaceSubsystem::GetPortalLink(const FPortalEntry& Entry) const
{
	if (const UDocAcousticPortalComponent* Comp = Entry.Component.Get())
	{
		return Comp->ToLink();
	}
	return Entry.Link;
}

// ---------------------------------------------------------------------------------------------
// Spaces
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAcousticSpaceSubsystem::RegisterSpace(UDocAcousticSpaceComponent* Space)
{
	if (!Space)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null space component."));
	}
	if (Space->SpaceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Space needs a SpaceId."));
	}
	if (!Space->BoundsBox.IsValid || Space->BoundsBox.Min.ContainsNaN() || Space->BoundsBox.Max.ContainsNaN())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Space %s has invalid bounds."), *Space->SpaceId.ToString()));
	}

	if (FSpaceEntry* Existing = Spaces.Find(Space->SpaceId))
	{
		if (Existing->Component.Get() == Space)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Space already registered."));
		}
		// A live component or a programmatic entry holds the id; a stale (destroyed) component or a retained descriptor is replaced.
		if (!Existing->bRetained && (Existing->Component.IsValid() || Existing->Component.IsExplicitlyNull()))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("SpaceId %s is already registered."), *Space->SpaceId.ToString()));
		}
	}

	FSpaceEntry Entry;
	Entry.SpaceId = Space->SpaceId;
	Entry.Bounds = Space->BoundsBox;
	Entry.Priority = Space->Priority;
	Entry.Component = Space;
	Spaces.Add(Space->SpaceId, Entry);
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

void UDocAcousticSpaceSubsystem::UnregisterSpace(UDocAcousticSpaceComponent* Space)
{
	if (!Space)
	{
		return;
	}
	FSpaceEntry* Entry = Spaces.Find(Space->SpaceId);
	if (!Entry || Entry->Component.Get() != Space)
	{
		return;
	}
	if (Space->bRetainDescriptorOnUnload)
	{
		Entry->Bounds = Space->BoundsBox;
		Entry->Priority = Space->Priority;
		Entry->Component = nullptr;
		Entry->bRetained = true;
	}
	else
	{
		Spaces.Remove(Space->SpaceId);
	}
	BumpTopology();
}

FDocSystemResult UDocAcousticSpaceSubsystem::AddProgrammaticSpace(FName SpaceId, const FBox& Bounds, int32 Priority)
{
	if (SpaceId.IsNone() || !Bounds.IsValid)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Programmatic space needs an id and valid bounds."));
	}
	if (const FSpaceEntry* Existing = Spaces.Find(SpaceId))
	{
		if (!Existing->bRetained)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("SpaceId %s is already registered."), *SpaceId.ToString()));
		}
	}
	FSpaceEntry Entry;
	Entry.SpaceId = SpaceId;
	Entry.Bounds = Bounds;
	Entry.Priority = Priority;
	Spaces.Add(SpaceId, Entry);
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

FName UDocAcousticSpaceSubsystem::ResolveSpaceForLocation(const FVector& Location, FName PreviousSpaceId, float HysteresisMargin) const
{
	// Hysteresis: stay in the previous space while inside its expanded bounds.
	if (!PreviousSpaceId.IsNone())
	{
		if (const FSpaceEntry* Previous = Spaces.Find(PreviousSpaceId))
		{
			const FBox Bounds = GetSpaceBounds(*Previous);
			if (Bounds.IsValid && Bounds.ExpandBy(FMath::Max(0.0f, HysteresisMargin)).IsInsideOrOn(Location))
			{
				return PreviousSpaceId;
			}
		}
	}

	// Otherwise the highest priority containing space; ties by lexical id (stable across runs).
	FName BestSpaceId = NAME_None;
	int32 BestPriority = TNumericLimits<int32>::Lowest();
	for (const TPair<FName, FSpaceEntry>& Kvp : Spaces)
	{
		const FBox Bounds = GetSpaceBounds(Kvp.Value);
		if (!Bounds.IsValid || !Bounds.IsInsideOrOn(Location))
		{
			continue;
		}
		const int32 Priority = GetSpacePriority(Kvp.Value);
		if (BestSpaceId.IsNone() || Priority > BestPriority || (Priority == BestPriority && Kvp.Key.LexicalLess(BestSpaceId)))
		{
			BestPriority = Priority;
			BestSpaceId = Kvp.Key;
		}
	}
	return BestSpaceId;
}

// ---------------------------------------------------------------------------------------------
// Portals
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAcousticSpaceSubsystem::RegisterPortal(UDocAcousticPortalComponent* Portal)
{
	if (!Portal)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null portal component."));
	}
	const FString Error = Portal->ToLink().Validate();
	if (!Error.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, Error);
	}

	if (FPortalEntry* Existing = Portals.Find(Portal->PortalId))
	{
		if (Existing->Component.Get() == Portal)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Portal already registered."));
		}
		if (!Existing->bRetained && (Existing->Component.IsValid() || Existing->Component.IsExplicitlyNull()))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("PortalId %s is already registered."), *Portal->PortalId.ToString()));
		}
		if (Existing->bRetained)
		{
			// Reloaded: the retained openness is the meaningful saved value for this identity.
			Portal->Openness = FMath::Clamp(Existing->Link.Openness, 0.0f, 1.0f);
		}
	}

	FPortalEntry Entry;
	Entry.Component = Portal;
	Entry.Link = Portal->ToLink();
	Portals.Add(Portal->PortalId, Entry);
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

void UDocAcousticSpaceSubsystem::UnregisterPortal(UDocAcousticPortalComponent* Portal)
{
	if (!Portal)
	{
		return;
	}
	FPortalEntry* Entry = Portals.Find(Portal->PortalId);
	if (!Entry || Entry->Component.Get() != Portal)
	{
		return;
	}
	if (Portal->bRetainDescriptorOnUnload)
	{
		Entry->Link = Portal->ToLink();
		Entry->Component = nullptr;
		Entry->bRetained = true;
	}
	else
	{
		Portals.Remove(Portal->PortalId);
	}
	BumpTopology();
}

FDocSystemResult UDocAcousticSpaceSubsystem::AddProgrammaticPortal(
	FName PortalId, FName SpaceA, FName SpaceB, float Openness, float MinGain, float MaxGain,
	float MinCutoff, float MaxCutoff, const FVector& Location)
{
	FPortalEntry Entry;
	Entry.Link.PortalId = PortalId;
	Entry.Link.SpaceA = SpaceA;
	Entry.Link.SpaceB = SpaceB;
	Entry.Link.Openness = Openness;
	Entry.Link.MinTransmissionGain = MinGain;
	Entry.Link.MaxTransmissionGain = MaxGain;
	Entry.Link.MinCutoffHz = MinCutoff;
	Entry.Link.MaxCutoffHz = MaxCutoff;
	Entry.Link.bIsEnabled = true;
	Entry.Link.PortalLocation = Location;

	const FString Error = Entry.Link.Validate();
	if (!Error.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, Error);
	}
	if (const FPortalEntry* Existing = Portals.Find(PortalId))
	{
		if (!Existing->bRetained)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("PortalId %s is already registered."), *PortalId.ToString()));
		}
	}
	Portals.Add(PortalId, Entry);
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

void UDocAcousticSpaceSubsystem::ClearProgrammaticTopology()
{
	for (auto It = Spaces.CreateIterator(); It; ++It)
	{
		if (!It->Value.Component.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Portals.CreateIterator(); It; ++It)
	{
		if (!It->Value.Component.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	BumpTopology();
}

FDocSystemResult UDocAcousticSpaceSubsystem::SetPortalOpenness(FName PortalId, float NewOpenness)
{
	if (!FMath::IsFinite(NewOpenness))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Openness must be finite."));
	}
	FPortalEntry* Entry = Portals.Find(PortalId);
	if (!Entry)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Portal %s not found"), *PortalId.ToString()));
	}

	const float Clamped = FMath::Clamp(NewOpenness, 0.0f, 1.0f);
	const float Current = GetPortalLink(*Entry).Openness;
	if (FMath::IsNearlyEqual(Current, Clamped, 1e-4f))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Openness unchanged."));
	}

	if (UDocAcousticPortalComponent* Comp = Entry->Component.Get())
	{
		Comp->SetOpenness(Clamped); // notifies and bumps the revision
	}
	else
	{
		Entry->Link.Openness = Clamped;
		BumpTopology();
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocAcousticSpaceSubsystem::NotifyPortalChanged(FName PortalId)
{
	if (FPortalEntry* Entry = Portals.Find(PortalId))
	{
		Entry->Link = GetPortalLink(*Entry);
	}
	BumpTopology();
}

FDocSystemResult UDocAcousticSpaceSubsystem::ValidateTopology(TArray<FString>& OutIssues) const
{
	OutIssues.Reset();

	TArray<FName> PortalIds;
	Portals.GetKeys(PortalIds);
	PortalIds.Sort(FNameLexicalLess());
	for (const FName& Id : PortalIds)
	{
		const FDocAcousticPortalLink Link = GetPortalLink(Portals[Id]);
		const FString Error = Link.Validate();
		if (!Error.IsEmpty())
		{
			OutIssues.Add(Error);
		}
		if (!Spaces.Contains(Link.SpaceA) || !Spaces.Contains(Link.SpaceB))
		{
			OutIssues.Add(FString::Printf(TEXT("Portal %s references an unknown space (%s, %s)."),
				*Id.ToString(), *Link.SpaceA.ToString(), *Link.SpaceB.ToString()));
		}
	}

	TArray<FName> SpaceIds;
	Spaces.GetKeys(SpaceIds);
	SpaceIds.Sort(FNameLexicalLess());
	for (int32 i = 0; i < SpaceIds.Num(); ++i)
	{
		for (int32 j = i + 1; j < SpaceIds.Num(); ++j)
		{
			const FSpaceEntry& A = Spaces[SpaceIds[i]];
			const FSpaceEntry& B = Spaces[SpaceIds[j]];
			if (GetSpacePriority(A) == GetSpacePriority(B) && GetSpaceBounds(A).Intersect(GetSpaceBounds(B)))
			{
				OutIssues.Add(FString::Printf(TEXT("Spaces %s and %s overlap at equal priority %d (ambiguous; resolved by id)."),
					*SpaceIds[i].ToString(), *SpaceIds[j].ToString(), GetSpacePriority(A)));
			}
		}
	}

	if (OutIssues.Num() > 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Join(OutIssues, TEXT(" ")));
	}
	return FDocSystemResult::MakeSuccess();
}

TArray<FName> UDocAcousticSpaceSubsystem::GetRetainedDescriptorIds() const
{
	TArray<FName> Result;
	for (const TPair<FName, FSpaceEntry>& Kvp : Spaces)
	{
		if (Kvp.Value.bRetained)
		{
			Result.Add(Kvp.Key);
		}
	}
	for (const TPair<FName, FPortalEntry>& Kvp : Portals)
	{
		if (Kvp.Value.bRetained)
		{
			Result.Add(Kvp.Key);
		}
	}
	Result.Sort(FNameLexicalLess());
	return Result;
}

// ---------------------------------------------------------------------------------------------
// Emitters and listeners
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAcousticSpaceSubsystem::RegisterEmitter(UDocAcousticEmitterComponent* Emitter)
{
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null emitter."));
	}
	if (Emitter->EmitterId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Emitter needs an EmitterId."));
	}
	if (const TWeakObjectPtr<UDocAcousticEmitterComponent>* Existing = Emitters.Find(Emitter->EmitterId))
	{
		if (Existing->Get() == Emitter)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Emitter already registered."));
		}
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("EmitterId %s is already registered."), *Emitter->EmitterId.ToString()));
		}
	}
	Emitter->RegistrationGeneration = ++EmitterGenerationCounter;
	Emitter->bHasEvaluatedLocation = false;
	Emitters.Add(Emitter->EmitterId, Emitter);
	return FDocSystemResult::MakeSuccess();
}

void UDocAcousticSpaceSubsystem::UnregisterEmitter(UDocAcousticEmitterComponent* Emitter)
{
	if (!Emitter)
	{
		return;
	}
	const TWeakObjectPtr<UDocAcousticEmitterComponent>* Existing = Emitters.Find(Emitter->EmitterId);
	if (!Existing || Existing->Get() != Emitter)
	{
		return;
	}

	TArray<FDocAcousticClaim> ToRelease;
	for (const TPair<FGuid, FDocAcousticClaim>& Kvp : ActiveClaims)
	{
		if (Kvp.Value.EmitterId == Emitter->EmitterId)
		{
			ToRelease.Add(Kvp.Value);
		}
	}
	for (const FDocAcousticClaim& Claim : ToRelease)
	{
		ActiveClaims.Remove(Claim.ClaimId);
		ReleaseClaimInternal(Claim);
	}

	Emitters.Remove(Emitter->EmitterId);
	TrackedMembership.Remove(FName(*(TEXT("E:") + Emitter->EmitterId.ToString())));
	const FString Prefix = Emitter->EmitterId.ToString() + TEXT("|");
	for (auto It = DebugPaths.CreateIterator(); It; ++It)
	{
		if (It->Key.StartsWith(Prefix))
		{
			It.RemoveCurrent();
		}
	}
}

void UDocAcousticSpaceSubsystem::UpdateListener(FName ListenerId, const FVector& Location)
{
	if (ListenerId.IsNone() || Location.ContainsNaN())
	{
		return;
	}
	FListenerEntry* Existing = Listeners.Find(ListenerId);
	FListenerEntry& Entry = Existing ? *Existing : Listeners.Add(ListenerId);
	if (Existing && FVector::Dist(Entry.Location, Location) > TeleportDistance)
	{
		Entry.bJumped = true;
	}
	Entry.Location = Location;
	Entry.UpdateTime = DocAcousticPrivate::WorldTime(GetWorld());
}

void UDocAcousticSpaceSubsystem::RemoveListener(FName ListenerId)
{
	Listeners.Remove(ListenerId);
	TrackedMembership.Remove(FName(*(TEXT("L:") + ListenerId.ToString())));
	if (MixListenerId == ListenerId)
	{
		MixListenerId = NAME_None;
	}
}

FDocSystemResult UDocAcousticSpaceSubsystem::SetMixListener(FName ListenerId)
{
	if (!Listeners.Contains(ListenerId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Listener %s has no location."), *ListenerId.ToString()));
	}
	MixListenerId = ListenerId;
	if (PlaybackAdapter.GetInterface())
	{
		return PlaybackAdapter->SetActiveListener(ListenerId);
	}
	return FDocSystemResult::MakeSuccess();
}

FName UDocAcousticSpaceSubsystem::GetMixListener() const
{
	if (!MixListenerId.IsNone())
	{
		return MixListenerId;
	}
	if (PlaybackAdapter.GetInterface())
	{
		return PlaybackAdapter->GetActiveListener();
	}
	return NAME_None;
}

FDocSystemResult UDocAcousticSpaceSubsystem::RequestIndependentListenerMixes()
{
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
		TEXT("The base mixes each emitter for one selected listener. Independent per-listener mixes need a dedicated backend."));
}

FName UDocAcousticSpaceSubsystem::GetTrackedEmitterSpace(FName EmitterId) const
{
	return TrackedMembership.FindRef(FName(*(TEXT("E:") + EmitterId.ToString())));
}

FName UDocAcousticSpaceSubsystem::GetTrackedListenerSpace(FName ListenerId) const
{
	return TrackedMembership.FindRef(FName(*(TEXT("L:") + ListenerId.ToString())));
}

FName UDocAcousticSpaceSubsystem::ResolveTracked(const FString& Key, const FVector& Location, FName ExplicitSpace)
{
	if (!ExplicitSpace.IsNone() && Spaces.Contains(ExplicitSpace))
	{
		return ExplicitSpace;
	}
	if (Key.IsEmpty())
	{
		return ResolveSpaceForLocation(Location);
	}
	const FName KeyName(*Key);
	const FName Previous = TrackedMembership.FindRef(KeyName);
	const FName Resolved = ResolveSpaceForLocation(Location, Previous, MembershipHysteresis);
	TrackedMembership.Add(KeyName, Resolved);
	return Resolved;
}

// ---------------------------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------------------------

void UDocAcousticSpaceSubsystem::ApplyFloor(FDocAcousticPathResult& Result, EDocAcousticPathStatus Status, FName Reason, const FString& Message) const
{
	Result.Status = Status;
	Result.TransmissionGain = FMath::Clamp(DisconnectedFloorGain, 0.0f, 1.0f);
	Result.CutoffFrequencyHz = DisconnectedCutoffHz;
	Result.Reason = Reason;
	Result.DiagnosticMessage = Message;
}

float UDocAcousticSpaceSubsystem::ComputeRouteDistance(const FVector& From, const TArray<FName>& PortalIds, const FVector& To) const
{
	float Total = 0.0f;
	FVector Point = From;
	for (const FName& Id : PortalIds)
	{
		if (const FPortalEntry* Entry = Portals.Find(Id))
		{
			const FVector Next = GetPortalLink(*Entry).PortalLocation;
			Total += FVector::Dist(Point, Next);
			Point = Next;
		}
	}
	return Total + FVector::Dist(Point, To);
}

FDocSystemResult UDocAcousticSpaceSubsystem::QueryTransmission(const FDocAcousticPathQuery& Query, FDocAcousticPathResult& OutResult)
{
	using namespace DocAcousticPrivate;

	OutResult = FDocAcousticPathResult();
	OutResult.TopologyRevision = TopologyRevision;
	OutResult.EmitterId = Query.EmitterId;
	OutResult.ListenerId = Query.ListenerId;
	OutResult.QueryTimeSeconds = WorldTime(GetWorld());
	if (const FListenerEntry* Listener = Listeners.Find(Query.ListenerId))
	{
		OutResult.ListenerTimeSeconds = Listener->UpdateTime;
	}

	if (Query.EmitterLocation.ContainsNaN() || Query.ListenerLocation.ContainsNaN())
	{
		ApplyFloor(OutResult, EDocAcousticPathStatus::Unresolved, ReasonUnresolved, TEXT("Non-finite query location."));
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, OutResult.DiagnosticMessage);
	}

	// Membership (tracked with hysteresis when ids are given; explicit emitter space wins).
	FName ExplicitEmitterSpace = NAME_None;
	if (const TWeakObjectPtr<UDocAcousticEmitterComponent>* Emitter = Emitters.Find(Query.EmitterId))
	{
		if (Emitter->IsValid())
		{
			ExplicitEmitterSpace = (*Emitter)->ExplicitSpaceId;
		}
	}
	FName SourceSpace = ResolveTracked(Query.EmitterId.IsNone() ? FString() : TEXT("E:") + Query.EmitterId.ToString(), Query.EmitterLocation, ExplicitEmitterSpace);
	FName TargetSpace = ResolveTracked(Query.ListenerId.IsNone() ? FString() : TEXT("L:") + Query.ListenerId.ToString(), Query.ListenerLocation, NAME_None);

	if (UnresolvedPolicy == EDocAcousticUnresolvedPolicy::TreatAsExterior && Spaces.Contains(ExteriorSpaceId))
	{
		SourceSpace = SourceSpace.IsNone() ? ExteriorSpaceId : SourceSpace;
		TargetSpace = TargetSpace.IsNone() ? ExteriorSpaceId : TargetSpace;
	}
	OutResult.EmitterSpace = SourceSpace;
	OutResult.ListenerSpace = TargetSpace;

	const FString DebugKey = Query.EmitterId.ToString() + TEXT("|") + Query.ListenerId.ToString();
	auto Finish = [&](const FDocSystemResult& Ret) -> FDocSystemResult
	{
		if (!Query.EmitterId.IsNone() || !Query.ListenerId.IsNone())
		{
			DebugPaths.Add(DebugKey, OutResult);
		}
		if (!OutResult.bFromCache)
		{
			OnPathResolved.Broadcast(Query.EmitterId, OutResult);
			OnPathResolvedNative.Broadcast(Query.EmitterId, OutResult);
		}
		return Ret;
	};

	if (SourceSpace.IsNone() || TargetSpace.IsNone())
	{
		ApplyFloor(OutResult, EDocAcousticPathStatus::Unresolved, ReasonUnresolved,
			TEXT("Emitter or listener is outside every acoustic space; unresolved policy applies the floor."));
		return Finish(FDocSystemResult::MakeSuccess());
	}

	if (SourceSpace == TargetSpace)
	{
		OutResult.Status = EDocAcousticPathStatus::SameSpace;
		OutResult.TransmissionGain = 1.0f;
		OutResult.CutoffFrequencyHz = 20000.0f;
		OutResult.PathSpaces.Add(SourceSpace);
		OutResult.TotalDistance = FVector::Dist(Query.EmitterLocation, Query.ListenerLocation);
		OutResult.Reason = ReasonSameSpace;
		return Finish(FDocSystemResult::MakeSuccess());
	}

	// Cache: valid only for the same topology revision, memberships and limits (and when cost ignores distance).
	if (CacheRevision != TopologyRevision)
	{
		PathCache.Empty();
		CacheRevision = TopologyRevision;
	}
	const bool bCacheable = DistanceCostPerUnit <= 0.0f;
	const FString CacheKey = FString::Printf(TEXT("%s>%s|%d|%d|%d"), *SourceSpace.ToString(), *TargetSpace.ToString(),
		Query.MaxHops, Query.MaxVisitedNodes, Query.bAllowApproximation ? 1 : 0);
	if (bCacheable)
	{
		if (const FDocAcousticPathResult* Cached = PathCache.Find(CacheKey))
		{
			const FDocAcousticPathResult Fresh = OutResult;
			OutResult = *Cached;
			OutResult.EmitterId = Fresh.EmitterId;
			OutResult.ListenerId = Fresh.ListenerId;
			OutResult.QueryTimeSeconds = Fresh.QueryTimeSeconds;
			OutResult.ListenerTimeSeconds = Fresh.ListenerTimeSeconds;
			OutResult.TotalDistance = ComputeRouteDistance(Query.EmitterLocation, OutResult.PathPortals, Query.ListenerLocation);
			OutResult.bFromCache = true;
			if (OutResult.Status == EDocAcousticPathStatus::Blocked || OutResult.Status == EDocAcousticPathStatus::BudgetExceeded)
			{
				// The route is cached; the floor is policy and always reflects the current settings.
				OutResult.TransmissionGain = FMath::Clamp(DisconnectedFloorGain, 0.0f, 1.0f);
				OutResult.CutoffFrequencyHz = DisconnectedCutoffHz;
			}
			++PendingCacheHits;
			const bool bFailed = OutResult.Status == EDocAcousticPathStatus::BudgetExceeded && !Query.bAllowApproximation;
			return Finish(bFailed ? FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, OutResult.DiagnosticMessage) : FDocSystemResult::MakeSuccess());
		}
	}

	// Adjacency in stable portal-id order.
	TMap<FName, TArray<FName>> Adjacency;
	TMap<FName, FDocAcousticPortalLink> Links;
	{
		TArray<FName> PortalIds;
		Portals.GetKeys(PortalIds);
		PortalIds.Sort(FNameLexicalLess());
		for (const FName& Id : PortalIds)
		{
			const FDocAcousticPortalLink Link = GetPortalLink(Portals[Id]);
			if (Link.GetEffectiveGain() <= 0.0f)
			{
				continue; // blocked edge, never a huge cost in the solver
			}
			Links.Add(Id, Link);
			Adjacency.FindOrAdd(Link.SpaceA).Add(Id);
			Adjacency.FindOrAdd(Link.SpaceB).Add(Id);
		}
	}

	// Dijkstra over (space, hops) states: exact under the hop limit.
	struct FState
	{
		FName Space;
		int32 Hops = 0;
		double Cost = 0.0;
		int32 Parent = INDEX_NONE;
		FName ViaPortal = NAME_None;
		FVector Point = FVector::ZeroVector;
		bool bSettled = false;
	};
	TArray<FState> States;
	TMap<TPair<FName, int32>, int32> StateIndex;
	TArray<int32> Open;

	{
		FState Start;
		Start.Space = SourceSpace;
		Start.Point = Query.EmitterLocation;
		States.Add(Start);
		StateIndex.Add(TPair<FName, int32>(SourceSpace, 0), 0);
		Open.Add(0);
	}

	const int32 MaxHops = FMath::Max(0, Query.MaxHops);
	const int32 MaxVisited = FMath::Max(1, Query.MaxVisitedNodes);
	int32 Visited = 0;
	int32 Found = INDEX_NONE;
	bool bBudgetHit = false;
	bool bHopLimited = false;

	while (Open.Num() > 0)
	{
		int32 BestOpen = 0;
		for (int32 i = 1; i < Open.Num(); ++i)
		{
			const FState& A = States[Open[i]];
			const FState& B = States[Open[BestOpen]];
			if (A.Cost < B.Cost - 1e-9
				|| (FMath::Abs(A.Cost - B.Cost) <= 1e-9 && (A.Hops < B.Hops || (A.Hops == B.Hops && A.Space.LexicalLess(B.Space)))))
			{
				BestOpen = i;
			}
		}

		if (Visited >= MaxVisited)
		{
			bBudgetHit = true;
			break;
		}

		const int32 CurrentIndex = Open[BestOpen];
		Open.RemoveAtSwap(BestOpen);
		States[CurrentIndex].bSettled = true;
		++Visited;

		const FState Current = States[CurrentIndex];
		if (Current.Space == TargetSpace)
		{
			Found = CurrentIndex;
			break;
		}

		const TArray<FName>* Edges = Adjacency.Find(Current.Space);
		if (!Edges)
		{
			continue;
		}
		if (Current.Hops >= MaxHops)
		{
			bHopLimited = true;
			continue;
		}

		for (const FName& PortalId : *Edges)
		{
			const FDocAcousticPortalLink& Link = Links.FindChecked(PortalId);
			const FName Neighbor = (Link.SpaceA == Current.Space) ? Link.SpaceB : Link.SpaceA;
			const int32 NewHops = Current.Hops + 1;
			const double Segment = FVector::Dist(Current.Point, Link.PortalLocation);
			const double EdgeCost = -FMath::Loge(FMath::Max(static_cast<double>(Link.GetEffectiveGain()), 1e-6))
				+ static_cast<double>(DistanceCostPerUnit) * Segment;
			const double NewCost = Current.Cost + FMath::Max(0.0, EdgeCost);

			const TPair<FName, int32> Key(Neighbor, NewHops);
			if (const int32* Existing = StateIndex.Find(Key))
			{
				FState& Other = States[*Existing];
				if (Other.bSettled)
				{
					continue;
				}
				const bool bBetter = NewCost < Other.Cost - 1e-9
					|| (FMath::Abs(NewCost - Other.Cost) <= 1e-9 && PortalId.LexicalLess(Other.ViaPortal));
				if (bBetter)
				{
					Other.Cost = NewCost;
					Other.Parent = CurrentIndex;
					Other.ViaPortal = PortalId;
					Other.Point = Link.PortalLocation;
				}
				continue;
			}

			FState Next;
			Next.Space = Neighbor;
			Next.Hops = NewHops;
			Next.Cost = NewCost;
			Next.Parent = CurrentIndex;
			Next.ViaPortal = PortalId;
			Next.Point = Link.PortalLocation;
			const int32 NewIndex = States.Add(Next);
			StateIndex.Add(Key, NewIndex);
			Open.Add(NewIndex);
		}
	}

	// Budget hit: optionally fall back to the best tentative route, flagged approximate.
	bool bApproximate = false;
	if (Found == INDEX_NONE && bBudgetHit && Query.bAllowApproximation)
	{
		for (int32 i = 0; i < States.Num(); ++i)
		{
			if (States[i].Space == TargetSpace && (Found == INDEX_NONE || States[i].Cost < States[Found].Cost - 1e-9))
			{
				Found = i;
			}
		}
		bApproximate = true;
	}

	FDocSystemResult Ret = FDocSystemResult::MakeSuccess();
	if (Found != INDEX_NONE)
	{
		TArray<int32> Chain;
		for (int32 Index = Found; Index != INDEX_NONE; Index = States[Index].Parent)
		{
			Chain.Insert(Index, 0);
		}

		OutResult.Status = EDocAcousticPathStatus::Connected;
		OutResult.Reason = bApproximate ? ReasonBudgetApproximate : ReasonConnected;
		OutResult.bIsApproximation = bApproximate;
		OutResult.PathCost = static_cast<float>(States[Found].Cost);

		float Gain = 1.0f;
		float Cutoff = 20000.0f;
		for (const int32 Index : Chain)
		{
			const FState& S = States[Index];
			OutResult.PathSpaces.Add(S.Space);
			if (!S.ViaPortal.IsNone())
			{
				const FDocAcousticPortalLink& Link = Links.FindChecked(S.ViaPortal);
				OutResult.PathPortals.Add(S.ViaPortal);
				OutResult.PortalGains.Add(Link.GetEffectiveGain());
				OutResult.PortalCutoffsHz.Add(Link.GetEffectiveCutoffHz());
				Gain *= Link.GetEffectiveGain();
				Cutoff = FMath::Min(Cutoff, Link.GetEffectiveCutoffHz());
			}
		}
		OutResult.TransmissionGain = Gain;
		OutResult.CutoffFrequencyHz = Cutoff;
		OutResult.TotalDistance = ComputeRouteDistance(Query.EmitterLocation, OutResult.PathPortals, Query.ListenerLocation);
		if (bApproximate)
		{
			OutResult.DiagnosticMessage = TEXT("Visited budget reached; best tentative route, not proven strongest.");
		}
	}
	else if (bBudgetHit)
	{
		ApplyFloor(OutResult, EDocAcousticPathStatus::BudgetExceeded, ReasonBudget, TEXT("Graph search exceeded MaxVisitedNodes budget"));
		OutResult.bIsApproximation = Query.bAllowApproximation;
		if (!Query.bAllowApproximation)
		{
			Ret = FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, OutResult.DiagnosticMessage);
		}
	}
	else
	{
		ApplyFloor(OutResult, EDocAcousticPathStatus::Blocked, bHopLimited ? ReasonHopLimit : ReasonNoRoute,
			bHopLimited ? TEXT("No admissible route within MaxHops; blocked policy applies.") : TEXT("No admissible portal path found between spaces"));
	}

	if (bCacheable)
	{
		if (PathCache.Num() >= FMath::Max(1, MaxCacheEntries))
		{
			PathCache.Empty();
		}
		PathCache.Add(CacheKey, OutResult);
	}
	return Finish(Ret);
}

bool UDocAcousticSpaceSubsystem::GetDebugPath(FName EmitterId, FName ListenerId, FDocAcousticPathResult& OutResult) const
{
	if (const FDocAcousticPathResult* Found = DebugPaths.Find(EmitterId.ToString() + TEXT("|") + ListenerId.ToString()))
	{
		OutResult = *Found;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Claims and update
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAcousticSpaceSubsystem::AcquireAcousticClaim(FName EmitterId, FGuid& OutClaimId)
{
	if (EmitterId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("EmitterId is empty"));
	}
	const TWeakObjectPtr<UDocAcousticEmitterComponent>* Emitter = Emitters.Find(EmitterId);
	if (!Emitter || !Emitter->IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Emitter %s is not registered."), *EmitterId.ToString()));
	}
	for (const TPair<FGuid, FDocAcousticClaim>& Kvp : ActiveClaims)
	{
		if (Kvp.Value.EmitterId == EmitterId)
		{
			OutClaimId = Kvp.Key;
			return FDocSystemResult::MakeNoChange(TEXT("Emitter already holds an acoustic claim."));
		}
	}

	FDocAcousticClaim Claim;
	Claim.ClaimId = FGuid::NewGuid();
	Claim.EmitterId = EmitterId;
	Claim.EmitterGeneration = (*Emitter)->RegistrationGeneration;
	Claim.bIsActive = true;
	ActiveClaims.Add(Claim.ClaimId, Claim);
	OutClaimId = Claim.ClaimId;
	return FDocSystemResult::MakeSuccess();
}

void UDocAcousticSpaceSubsystem::ReleaseClaimInternal(const FDocAcousticClaim& Claim)
{
	float BaselineGain = 1.0f;
	float BaselineCutoff = 20000.0f;
	if (const TWeakObjectPtr<UDocAcousticEmitterComponent>* Emitter = Emitters.Find(Claim.EmitterId))
	{
		if (UDocAcousticEmitterComponent* Comp = Emitter->Get())
		{
			Comp->ClearAcousticInfluence();
			BaselineGain = Comp->BaseGain;
			BaselineCutoff = Comp->BaseCutoffHz;
		}
	}
	if (PlaybackAdapter.GetInterface())
	{
		PlaybackAdapter->ResetAcousticParameters(Claim.EmitterId, BaselineGain, BaselineCutoff);
	}
}

FDocSystemResult UDocAcousticSpaceSubsystem::ReleaseAcousticClaim(const FGuid& ClaimId)
{
	FDocAcousticClaim FoundClaim;
	if (ActiveClaims.RemoveAndCopyValue(ClaimId, FoundClaim))
	{
		ReleaseClaimInternal(FoundClaim);
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("ClaimId not found"));
}

bool UDocAcousticSpaceSubsystem::HasAcousticClaim(FName EmitterId) const
{
	for (const TPair<FGuid, FDocAcousticClaim>& Kvp : ActiveClaims)
	{
		if (Kvp.Value.EmitterId == EmitterId)
		{
			return true;
		}
	}
	return false;
}

void UDocAcousticSpaceSubsystem::AdvanceSmoothing(float DeltaSeconds)
{
	FDocAcousticUpdateStats Stats;
	PendingCacheHits = 0;

	// Claimed, live emitters in stable id order. Claims from an older registration generation are dropped.
	TArray<TPair<FGuid, UDocAcousticEmitterComponent*>> Claimed;
	TArray<FGuid> StaleClaims;
	for (const TPair<FGuid, FDocAcousticClaim>& Kvp : ActiveClaims)
	{
		const TWeakObjectPtr<UDocAcousticEmitterComponent>* Emitter = Emitters.Find(Kvp.Value.EmitterId);
		UDocAcousticEmitterComponent* Comp = Emitter ? Emitter->Get() : nullptr;
		if (!Comp || Comp->RegistrationGeneration != Kvp.Value.EmitterGeneration)
		{
			StaleClaims.Add(Kvp.Key);
			continue;
		}
		Claimed.Add(TPair<FGuid, UDocAcousticEmitterComponent*>(Kvp.Key, Comp));
	}
	for (const FGuid& Id : StaleClaims)
	{
		ActiveClaims.Remove(Id);
	}
	Claimed.Sort([](const TPair<FGuid, UDocAcousticEmitterComponent*>& A, const TPair<FGuid, UDocAcousticEmitterComponent*>& B)
	{
		return A.Value->EmitterId.LexicalLess(B.Value->EmitterId);
	});
	Stats.ClaimedEmitters = Claimed.Num();

	// Evaluate a staggered subset against the one mix listener.
	const FName ListenerId = GetMixListener();
	FListenerEntry* Listener = ListenerId.IsNone() ? nullptr : Listeners.Find(ListenerId);
	if (bAutoEvaluateClaimedEmitters && Listener && Claimed.Num() > 0)
	{
		const int32 Count = Claimed.Num();
		const int32 Budget = FMath::Min(Count, FMath::Max(1, MaxEmitterEvaluationsPerUpdate));
		const int32 Start = EvaluationCursor % Count;
		for (int32 k = 0; k < Budget; ++k)
		{
			UDocAcousticEmitterComponent* Comp = Claimed[(Start + k) % Count].Value;
			const FVector Location = Comp->GetEmitterLocation();
			const bool bTeleported = Comp->bHasEvaluatedLocation && FVector::Dist(Location, Comp->LastEvaluatedLocation) > TeleportDistance;
			const bool bSnap = !Comp->bHasEvaluatedLocation || bTeleported || Listener->bJumped;

			FDocAcousticPathQuery Query;
			Query.EmitterId = Comp->EmitterId;
			Query.EmitterLocation = Location;
			Query.ListenerId = ListenerId;
			Query.ListenerLocation = Listener->Location;
			FDocAcousticPathResult Result;
			QueryTransmission(Query, Result); // floor policy applies on failure as well

			Comp->SetTargetParameters(Result.TransmissionGain, Result.CutoffFrequencyHz, bSnap);
			Comp->LastEvaluatedLocation = Location;
			Comp->bHasEvaluatedLocation = true;
			++Stats.EvaluatedEmitters;
		}
		EvaluationCursor = (Start + Budget) % Count;
		Stats.DeferredEmitters = Count - Budget;
		if (Budget == Count)
		{
			Listener->bJumped = false;
		}
	}
	Stats.CacheHits = PendingCacheHits;

	// Smooth and apply only claimed emitters.
	for (const TPair<FGuid, UDocAcousticEmitterComponent*>& Entry : Claimed)
	{
		UDocAcousticEmitterComponent* Comp = Entry.Value;
		Comp->AdvanceSmoothing(DeltaSeconds);
		if (FDocAcousticClaim* Claim = ActiveClaims.Find(Entry.Key))
		{
			Claim->AppliedGainMultiplier = Comp->TransmissionMultiplier;
			Claim->AppliedCutoffHz = Comp->CurrentEffectiveCutoffHz;
		}
		if (PlaybackAdapter.GetInterface())
		{
			PlaybackAdapter->ApplyAcousticParameters(Comp->EmitterId, Comp->CurrentEffectiveGain, Comp->CurrentEffectiveCutoffHz);
			++Stats.AdapterApplies;
		}
	}

	LastUpdateStats = Stats;
}

void UDocAcousticSpaceSubsystem::SetPlaybackAdapter(TScriptInterface<IDocAcousticPlaybackAdapter> InAdapter)
{
	PlaybackAdapter = InAdapter;
}
