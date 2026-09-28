#include "DocRegionSubsystem.h"
#include "DocRegionComponents.h"
#include "DocRegionsLog.h"
#include "DocCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocRegionSubsystem)

UDocRegionSubsystem* UDocRegionSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocRegionSubsystem>() : nullptr;
}

bool UDocRegionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UDocRegionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocRegionSubsystem, STATGROUP_Tickables);
}

void UDocRegionSubsystem::Deinitialize()
{
	Observers.Reset();
	Regions.Reset();
	RegionInfoCache.Reset();
	Grid.Reset();
	RegionCells.Reset();
	MovableRegions.Reset();
	Super::Deinitialize();
}

void UDocRegionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate >= UpdateInterval)
	{
		TimeSinceUpdate = 0.f;
		UpdateAllObservers();
	}
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

UDocRegionSubsystem::FCellKey UDocRegionSubsystem::CellFor(const FVector& P) const
{
	const float Size = FMath::Max(100.f, GridCellSize);
	return FCellKey{ static_cast<int32>(FMath::FloorToInt(P.X / Size)), static_cast<int32>(FMath::FloorToInt(P.Y / Size)), static_cast<int32>(FMath::FloorToInt(P.Z / Size)) };
}

void UDocRegionSubsystem::AddToGrid(UDocRegionComponent* Region)
{
	const FBox Bounds = Region->GetWorldBounds();
	if (!Bounds.IsValid)
	{
		MovableRegions.AddUnique(Region->GetInstanceId()); // unknown bounds: brute-force it
		return;
	}
	const FCellKey Min = CellFor(Bounds.Min);
	const FCellKey Max = CellFor(Bounds.Max);
	const int64 CellCount = int64(Max.X - Min.X + 1) * (Max.Y - Min.Y + 1) * (Max.Z - Min.Z + 1);
	if (CellCount > 4096)
	{
		// Huge region: cheaper to test it directly than to fill thousands of cells.
		MovableRegions.AddUnique(Region->GetInstanceId());
		return;
	}
	TArray<FCellKey>& Cells = RegionCells.FindOrAdd(Region->GetInstanceId());
	for (int32 X = Min.X; X <= Max.X; ++X)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
			{
				const FCellKey Key{ X, Y, Z };
				Grid.FindOrAdd(Key).AddUnique(Region->GetInstanceId());
				Cells.Add(Key);
			}
		}
	}
}

void UDocRegionSubsystem::RemoveFromGrid(UDocRegionComponent* Region)
{
	const FGuid Id = Region->GetInstanceId();
	if (TArray<FCellKey>* Cells = RegionCells.Find(Id))
	{
		for (const FCellKey& Key : *Cells)
		{
			if (TArray<FGuid>* List = Grid.Find(Key))
			{
				List->RemoveSingle(Id);
				if (List->Num() == 0)
				{
					Grid.Remove(Key);
				}
			}
		}
		RegionCells.Remove(Id);
	}
	MovableRegions.Remove(Id);
}

void UDocRegionSubsystem::GatherCandidateRegions(const FBox& QueryBox, TArray<UDocRegionComponent*>& Out) const
{
	Out.Reset();
	TSet<FGuid> Seen;
	auto Consider = [&](const FGuid& Id)
	{
		if (Seen.Contains(Id)) { return; }
		Seen.Add(Id);
		if (const TWeakObjectPtr<UDocRegionComponent>* Weak = Regions.Find(Id))
		{
			if (UDocRegionComponent* Region = Weak->Get())
			{
				Out.Add(Region);
			}
		}
	};

	const FCellKey Min = CellFor(QueryBox.Min);
	const FCellKey Max = CellFor(QueryBox.Max);
	for (int32 X = Min.X; X <= Max.X; ++X)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
			{
				if (const TArray<FGuid>* List = Grid.Find(FCellKey{ X, Y, Z }))
				{
					for (const FGuid& Id : *List) { Consider(Id); }
				}
			}
		}
	}
	for (const FGuid& Id : MovableRegions) { Consider(Id); }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void UDocRegionSubsystem::RegisterRegion(UDocRegionComponent* Region)
{
	if (!Region || !Region->GetInstanceId().IsValid())
	{
		UE_LOG(LogDocRegions, Warning, TEXT("RegisterRegion: invalid region or missing InstanceId (%s)"), *GetPathNameSafe(Region));
		return;
	}
	const FGuid Id = Region->GetInstanceId();
	if (const TWeakObjectPtr<UDocRegionComponent>* Existing = Regions.Find(Id))
	{
		if (Existing->Get() && Existing->Get() != Region)
		{
			UE_LOG(LogDocRegions, Error, TEXT("Duplicate region InstanceId %s on %s and %s; second registration ignored"),
				*Id.ToString(), *GetPathNameSafe(Existing->Get()), *GetPathNameSafe(Region));
			return;
		}
		RemoveFromGrid(Region);
	}
	Regions.Add(Id, Region);
	RegionInfoCache.Add(Id, Region->MakeInfo());
	if (Region->bMovable)
	{
		MovableRegions.AddUnique(Id);
	}
	else
	{
		AddToGrid(Region);
	}

	// Observers already inside a newly loaded region enter with a reason.
	for (int32 i = 0; i < Observers.Num(); ++i) // index loop: listeners may add/remove observers
	{
		if (Observers[i].bInitialized)
		{
			UpdateObserver(Observers[i], EDocRegionTransitionReason::RegionAdded, true);
		}
	}
}

void UDocRegionSubsystem::UnregisterRegion(UDocRegionComponent* Region)
{
	if (!Region)
	{
		return;
	}
	const FGuid Id = Region->GetInstanceId();
	const TWeakObjectPtr<UDocRegionComponent>* Existing = Regions.Find(Id);
	if (!Existing || (Existing->Get() && Existing->Get() != Region))
	{
		return;
	}
	RemoveFromGrid(Region);
	Regions.Remove(Id);

	// Members exit with RegionUnloaded; no dangling reference is kept (info cache holds values only).
	for (int32 i = 0; i < Observers.Num(); ++i)
	{
		if (Observers[i].bInitialized && Observers[i].Current.Contains(Id))
		{
			UpdateObserver(Observers[i], EDocRegionTransitionReason::RegionUnloaded, true);
		}
	}
	RegionInfoCache.Remove(Id);
}

FDocSystemResult UDocRegionSubsystem::RegisterObserver(AActor* Observer, FDocRegionObserverOptions Options)
{
	if (!Observer)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null observer"));
	}
	for (const FObserver& Existing : Observers)
	{
		if (Existing.Actor.Get() == Observer)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Already observed"));
		}
	}
	FObserver& Added = Observers.AddDefaulted_GetRef();
	Added.Actor = Observer;
	Added.Options = Options;
	UpdateObserver(Added, EDocRegionTransitionReason::Initial, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocRegionSubsystem::UnregisterObserver(AActor* Observer)
{
	const int32 Index = Observers.IndexOfByPredicate([Observer](const FObserver& O) { return O.Actor.Get() == Observer; });
	if (Index == INDEX_NONE)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Not observed"));
	}
	FObserver Removed = Observers[Index];
	Observers.RemoveAt(Index);

	// Emit exits so consumers release per-region state.
	const FDocRegionInfo OldPrimary = InfoFor(Removed.Primary);
	for (const FGuid& Id : Removed.Current)
	{
		OnRegionExited.Broadcast(Observer, InfoFor(Id), EDocRegionTransitionReason::ObserverRemoved);
	}
	if (OldPrimary.IsValid())
	{
		OnPrimaryRegionChanged.Broadcast(Observer, FDocRegionInfo(), OldPrimary, EDocRegionTransitionReason::ObserverRemoved);
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------

void UDocRegionSubsystem::ComputeMembership(const FObserver& Observer, TArray<UDocRegionComponent*>& OutRegions) const
{
	OutRegions.Reset();
	const AActor* Actor = Observer.Actor.Get();
	if (!Actor)
	{
		return;
	}

	if (Observer.Options.MembershipTest == EDocRegionMembershipTest::ReferencePoint)
	{
		const FVector Point = Actor->GetActorLocation() + Observer.Options.ReferenceOffset;
		TArray<UDocRegionComponent*> Candidates;
		GatherCandidateRegions(FBox(Point, Point), Candidates);
		for (UDocRegionComponent* Region : Candidates)
		{
			if (Region->ContainsPoint(Point, BoundaryTolerance))
			{
				OutRegions.Add(Region);
			}
		}
	}
	else
	{
		const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
		const FBox Query = ActorBox.IsValid ? ActorBox : FBox(Actor->GetActorLocation(), Actor->GetActorLocation());
		TArray<UDocRegionComponent*> Candidates;
		GatherCandidateRegions(Query, Candidates);
		for (UDocRegionComponent* Region : Candidates)
		{
			// Conservative: bounds intersection, then a precise point test at the closest point.
			const FBox RegionBox = Region->GetWorldBounds();
			if (RegionBox.IsValid && RegionBox.Intersect(Query))
			{
				const FVector Closest = Query.GetClosestPointTo(RegionBox.GetCenter());
				if (Region->ContainsPoint(Closest, BoundaryTolerance) || Region->ContainsPoint(Query.GetCenter(), BoundaryTolerance))
				{
					OutRegions.Add(Region);
				}
			}
		}
	}
	OutRegions.Sort([](const UDocRegionComponent& A, const UDocRegionComponent& B) { return A.GetInstanceId() < B.GetInstanceId(); });
}

bool UDocRegionSubsystem::IsBetterPrimary(const FDocRegionInfo& A, const UDocRegionComponent* AComp, const FDocRegionInfo& B, const UDocRegionComponent* BComp)
{
	if (A.Priority != B.Priority) { return A.Priority > B.Priority; }
	if (A.Depth != B.Depth) { return A.Depth > B.Depth; }
	const int32 ATagDepth = A.RegionTag.IsValid() ? A.RegionTag.GetGameplayTagParents().Num() : 0;
	const int32 BTagDepth = B.RegionTag.IsValid() ? B.RegionTag.GetGameplayTagParents().Num() : 0;
	if (ATagDepth != BTagDepth) { return ATagDepth > BTagDepth; }
	if (AComp && BComp)
	{
		const double AVol = AComp->GetWorldBounds().GetVolume();
		const double BVol = BComp->GetWorldBounds().GetVolume();
		if (!FMath::IsNearlyEqual(AVol, BVol, 1.0)) { return AVol < BVol; }
	}
	return A.InstanceId < B.InstanceId; // stable, registration-order independent
}

UDocRegionComponent* UDocRegionSubsystem::ChoosePrimary(const TArray<UDocRegionComponent*>& Members) const
{
	UDocRegionComponent* Best = nullptr;
	FDocRegionInfo BestInfo;
	for (UDocRegionComponent* Region : Members)
	{
		const FDocRegionInfo Info = Region->MakeInfo();
		if (!Best || IsBetterPrimary(Info, Region, BestInfo, Best))
		{
			Best = Region;
			BestInfo = Info;
		}
	}
	return Best;
}

FDocRegionInfo UDocRegionSubsystem::InfoFor(const FGuid& Id) const
{
	if (!Id.IsValid())
	{
		return FDocRegionInfo();
	}
	if (const TWeakObjectPtr<UDocRegionComponent>* Weak = Regions.Find(Id))
	{
		if (const UDocRegionComponent* Region = Weak->Get())
		{
			return Region->MakeInfo();
		}
	}
	if (const FDocRegionInfo* Cached = RegionInfoCache.Find(Id))
	{
		FDocRegionInfo Copy = *Cached;
		Copy.Component.Reset(); // never hand out a dangling component
		return Copy;
	}
	FDocRegionInfo Unknown;
	Unknown.InstanceId = Id;
	return Unknown;
}

void UDocRegionSubsystem::UpdateObserver(FObserver& Observer, EDocRegionTransitionReason ForcedReason, bool bForceReason)
{
	AActor* Actor = Observer.Actor.Get();
	if (!Actor)
	{
		return;
	}

	EDocRegionTransitionReason Reason = ForcedReason;
	const FVector Location = Actor->GetActorLocation();
	if (!bForceReason)
	{
		Reason = Observer.bInitialized && FVector::Dist(Location, Observer.LastLocation) > TeleportDistance
			? EDocRegionTransitionReason::Teleport : EDocRegionTransitionReason::Movement;
	}
	if (!Observer.bInitialized)
	{
		Reason = EDocRegionTransitionReason::Initial;
	}
	Observer.LastLocation = Location;
	Observer.bInitialized = true;

	TArray<UDocRegionComponent*> Members;
	ComputeMembership(Observer, Members);
	TArray<FGuid> NewIds;
	for (const UDocRegionComponent* Region : Members) { NewIds.Add(Region->GetInstanceId()); }

	const UDocRegionComponent* NewPrimaryComp = ChoosePrimary(Members);
	const FGuid NewPrimary = NewPrimaryComp ? NewPrimaryComp->GetInstanceId() : FGuid();

	TArray<FGuid> Exited;
	TArray<FGuid> Entered;
	for (const FGuid& Id : Observer.Current) { if (!NewIds.Contains(Id)) { Exited.Add(Id); } }
	for (const FGuid& Id : NewIds) { if (!Observer.Current.Contains(Id)) { Entered.Add(Id); } }

	const FGuid OldPrimary = Observer.Primary;
	// Capture exit infos before the membership is replaced (unloaded regions resolve from the cache).
	TArray<FDocRegionInfo> ExitInfos;
	for (const FGuid& Id : Exited) { ExitInfos.Add(InfoFor(Id)); }
	const FDocRegionInfo OldPrimaryInfo = InfoFor(OldPrimary);

	// Commit the full new state before notifying (listeners see consistent queries).
	Observer.Current = NewIds;
	Observer.Primary = NewPrimary;

	TWeakObjectPtr<AActor> WeakActor = Actor;
	for (const FDocRegionInfo& Info : ExitInfos)
	{
		OnRegionExited.Broadcast(Actor, Info, Reason);
		if (!WeakActor.IsValid()) { return; }
	}
	for (const FGuid& Id : Entered)
	{
		OnRegionEntered.Broadcast(Actor, InfoFor(Id), Reason);
		if (!WeakActor.IsValid()) { return; }
	}
	if (OldPrimary != NewPrimary)
	{
		OnPrimaryRegionChanged.Broadcast(Actor, InfoFor(NewPrimary), OldPrimaryInfo, Reason);
	}
}

void UDocRegionSubsystem::UpdateAllObservers()
{
	// Re-bin movable regions implicitly (they are brute-forced) and drop dead observers.
	for (int32 i = Observers.Num() - 1; i >= 0; --i)
	{
		if (!Observers[i].Actor.IsValid())
		{
			// Destroyed without unregistering: emit exits with ObserverRemoved, actor pointer null.
			const FObserver Dead = Observers[i];
			Observers.RemoveAt(i);
			for (const FGuid& Id : Dead.Current)
			{
				OnRegionExited.Broadcast(nullptr, InfoFor(Id), EDocRegionTransitionReason::ObserverRemoved);
			}
		}
	}
	// Index-based: listeners may register/unregister observers.
	for (int32 i = 0; i < Observers.Num(); ++i)
	{
		UpdateObserver(Observers[i], EDocRegionTransitionReason::Movement, false);
	}
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

TArray<FDocRegionInfo> UDocRegionSubsystem::GetRegionsAtLocation(const FVector& Location) const
{
	TArray<UDocRegionComponent*> Candidates;
	GatherCandidateRegions(FBox(Location, Location), Candidates);
	TArray<FDocRegionInfo> Out;
	for (const UDocRegionComponent* Region : Candidates)
	{
		if (Region->ContainsPoint(Location, BoundaryTolerance))
		{
			Out.Add(Region->MakeInfo());
		}
	}
	Out.Sort([](const FDocRegionInfo& A, const FDocRegionInfo& B) { return A.InstanceId < B.InstanceId; });
	return Out;
}

FDocRegionInfo UDocRegionSubsystem::GetRegionAtLocation(const FVector& Location) const
{
	TArray<UDocRegionComponent*> Candidates;
	GatherCandidateRegions(FBox(Location, Location), Candidates);
	TArray<UDocRegionComponent*> Members;
	for (UDocRegionComponent* Region : Candidates)
	{
		if (Region->ContainsPoint(Location, BoundaryTolerance))
		{
			Members.Add(Region);
		}
	}
	const UDocRegionComponent* Primary = ChoosePrimary(Members);
	return Primary ? Primary->MakeInfo() : FDocRegionInfo();
}

TArray<FDocRegionInfo> UDocRegionSubsystem::GetRegionsForActor(const AActor* Actor) const
{
	TArray<FDocRegionInfo> Out;
	for (const FObserver& Observer : Observers)
	{
		if (Observer.Actor.Get() == Actor)
		{
			for (const FGuid& Id : Observer.Current) { Out.Add(InfoFor(Id)); }
			break;
		}
	}
	return Out;
}

FDocRegionInfo UDocRegionSubsystem::GetPrimaryRegionForActor(const AActor* Actor) const
{
	for (const FObserver& Observer : Observers)
	{
		if (Observer.Actor.Get() == Actor)
		{
			return InfoFor(Observer.Primary);
		}
	}
	return FDocRegionInfo();
}

bool UDocRegionSubsystem::IsActorInRegion(const AActor* Actor, FGameplayTag RegionTag, bool bIncludeChildTags) const
{
	for (const FDocRegionInfo& Info : GetRegionsForActor(Actor))
	{
		if (bIncludeChildTags ? Info.RegionTag.MatchesTag(RegionTag) : Info.RegionTag == RegionTag)
		{
			return true;
		}
	}
	return false;
}

TArray<AActor*> UDocRegionSubsystem::GetActorsInRegion(FGameplayTag RegionTag, bool bIncludeChildTags) const
{
	TArray<AActor*> Out;
	for (const FObserver& Observer : Observers)
	{
		AActor* Actor = Observer.Actor.Get();
		if (!Actor) { continue; }
		for (const FGuid& Id : Observer.Current)
		{
			const FDocRegionInfo Info = InfoFor(Id);
			if (bIncludeChildTags ? Info.RegionTag.MatchesTag(RegionTag) : Info.RegionTag == RegionTag)
			{
				Out.Add(Actor);
				break;
			}
		}
	}
	return Out;
}

TArray<FDocRegionInfo> UDocRegionSubsystem::FindRegionsByTag(FGameplayTag RegionTag, bool bIncludeChildTags) const
{
	TArray<FDocRegionInfo> Out;
	for (const TPair<FGuid, TWeakObjectPtr<UDocRegionComponent>>& Pair : Regions)
	{
		if (const UDocRegionComponent* Region = Pair.Value.Get())
		{
			const FGameplayTag Tag = Region->GetRegionTag();
			if (bIncludeChildTags ? Tag.MatchesTag(RegionTag) : Tag == RegionTag)
			{
				Out.Add(Region->MakeInfo());
			}
		}
	}
	Out.Sort([](const FDocRegionInfo& A, const FDocRegionInfo& B) { return A.InstanceId < B.InstanceId; });
	return Out;
}

FDocSystemResult UDocRegionSubsystem::FindRegionByTag(FGameplayTag RegionTag, FDocRegionInfo& OutRegion) const
{
	OutRegion = FDocRegionInfo();
	const TArray<FDocRegionInfo> Found = FindRegionsByTag(RegionTag, false);
	if (Found.Num() == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No loaded region with tag %s"), *RegionTag.ToString()));
	}
	if (Found.Num() > 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("%d region instances share tag %s; use FindRegionsByTag"), Found.Num(), *RegionTag.ToString()));
	}
	OutRegion = Found[0];
	return FDocSystemResult::MakeSuccess();
}
