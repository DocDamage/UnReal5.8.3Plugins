#include "DocOpticalBeamSubsystem.h"
#include "DocOpticalBeamsLog.h"
#include "DocBeamEmitterComponent.h"
#include "DocBeamSurfaceComponent.h"
#include "DocBeamReceiverComponent.h"
#include "DocOpticalProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"

UDocOpticalBeamSubsystem::UDocOpticalBeamSubsystem()
{
}

UDocOpticalBeamSubsystem* UDocOpticalBeamSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UDocOpticalBeamSubsystem>() : nullptr;
}

void UDocOpticalBeamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocOpticalBeamSubsystem::Deinitialize()
{
	Emitters.Empty();
	Surfaces.Empty();
	Receivers.Empty();
	ProgrammaticPlanes.Empty();
	CachedPaths.Empty();
	DirtySince.Empty();
	Super::Deinitialize();
}

void UDocOpticalBeamSubsystem::Tick(float DeltaTime)
{
	if (bAutoTick)
	{
		AdvanceSimulation(DeltaTime);
	}
}

bool UDocOpticalBeamSubsystem::IsTickable() const
{
	return !IsTemplate() && bAutoTick && (Receivers.Num() > 0 || DirtySince.Num() > 0);
}

TStatId UDocOpticalBeamSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocOpticalBeamSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------------------------
// Lookup and invalidation
// ---------------------------------------------------------------------------------------------

UDocBeamEmitterComponent* UDocOpticalBeamSubsystem::FindEmitter(FName EmitterId) const
{
	const TWeakObjectPtr<UDocBeamEmitterComponent>* Found = Emitters.Find(EmitterId);
	return Found ? Found->Get() : nullptr;
}

UDocBeamSurfaceComponent* UDocOpticalBeamSubsystem::FindSurface(FName SurfaceId) const
{
	const TWeakObjectPtr<UDocBeamSurfaceComponent>* Found = Surfaces.Find(SurfaceId);
	return Found ? Found->Get() : nullptr;
}

UDocBeamReceiverComponent* UDocOpticalBeamSubsystem::FindReceiver(FName ReceiverId) const
{
	const TWeakObjectPtr<UDocBeamReceiverComponent>* Found = Receivers.Find(ReceiverId);
	return Found ? Found->Get() : nullptr;
}

TArray<FName> UDocOpticalBeamSubsystem::SortedEmitterIds() const
{
	TArray<FName> Ids;
	for (const TPair<FName, TWeakObjectPtr<UDocBeamEmitterComponent>>& Kvp : Emitters)
	{
		if (Kvp.Value.IsValid())
		{
			Ids.Add(Kvp.Key);
		}
	}
	Ids.Sort(FNameLexicalLess());
	return Ids;
}

void UDocOpticalBeamSubsystem::MarkDirty(FName EmitterId)
{
	if (!DirtySince.Contains(EmitterId))
	{
		DirtySince.Add(EmitterId, SimTime);
	}
}

void UDocOpticalBeamSubsystem::MarkAllDirty()
{
	for (const FName& Id : SortedEmitterIds())
	{
		MarkDirty(Id);
	}
}

void UDocOpticalBeamSubsystem::BumpTopology()
{
	++TopologyRevision;
	MarkAllDirty();
}

// ---------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocOpticalBeamSubsystem::RegisterEmitter(UDocBeamEmitterComponent* Emitter)
{
	if (!Emitter || Emitter->EmitterId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Emitter needs an EmitterId."));
	}
	if (const TWeakObjectPtr<UDocBeamEmitterComponent>* Existing = Emitters.Find(Emitter->EmitterId))
	{
		if (Existing->Get() == Emitter)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Emitter already registered."));
		}
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("EmitterId %s already registered."), *Emitter->EmitterId.ToString()));
		}
	}
	Emitters.Add(Emitter->EmitterId, Emitter);
	++Emitter->PathGeneration;
	MarkDirty(Emitter->EmitterId);
	return FDocSystemResult::MakeSuccess();
}

void UDocOpticalBeamSubsystem::UnregisterEmitter(UDocBeamEmitterComponent* Emitter)
{
	if (!Emitter || FindEmitter(Emitter->EmitterId) != Emitter)
	{
		return;
	}
	++Emitter->PathGeneration; // in-flight tickets for this emitter become stale
	for (const TPair<FName, TWeakObjectPtr<UDocBeamReceiverComponent>>& Kvp : Receivers)
	{
		if (UDocBeamReceiverComponent* Receiver = Kvp.Value.Get())
		{
			Receiver->RemoveContribution(Emitter->EmitterId); // only this emitter's contribution
		}
	}
	CachedPaths.Remove(Emitter->EmitterId);
	DirtySince.Remove(Emitter->EmitterId);
	Emitters.Remove(Emitter->EmitterId);
}

FDocSystemResult UDocOpticalBeamSubsystem::RegisterSurface(UDocBeamSurfaceComponent* Surface)
{
	if (!Surface || Surface->SurfaceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Surface needs a SurfaceId."));
	}
	if (Surface->SurfaceNormalOverride.ContainsNaN())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Surface normal override is not finite."));
	}
	if (const TWeakObjectPtr<UDocBeamSurfaceComponent>* Existing = Surfaces.Find(Surface->SurfaceId))
	{
		if (Existing->Get() == Surface)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Surface already registered."));
		}
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("SurfaceId %s already registered."), *Surface->SurfaceId.ToString()));
		}
	}
	Surfaces.Add(Surface->SurfaceId, Surface);
	BumpTopology();
	if (Surface->OpticalProfile)
	{
		const FDocSystemResult Valid = Surface->OpticalProfile->ValidateProfile();
		if (!Valid.IsSuccess())
		{
			UE_LOG(LogDocOpticalBeams, Warning, TEXT("Surface %s profile invalid (%s); it acts as a blocker."), *Surface->SurfaceId.ToString(), *Valid.ToString());
		}
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocOpticalBeamSubsystem::UnregisterSurface(UDocBeamSurfaceComponent* Surface)
{
	if (Surface && FindSurface(Surface->SurfaceId) == Surface)
	{
		Surfaces.Remove(Surface->SurfaceId);
		BumpTopology();
	}
}

FDocSystemResult UDocOpticalBeamSubsystem::RegisterReceiver(UDocBeamReceiverComponent* Receiver)
{
	if (!Receiver || Receiver->ReceiverId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Receiver needs a ReceiverId."));
	}
	if (const TWeakObjectPtr<UDocBeamReceiverComponent>* Existing = Receivers.Find(Receiver->ReceiverId))
	{
		if (Existing->Get() == Receiver)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Receiver already registered."));
		}
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("ReceiverId %s already registered."), *Receiver->ReceiverId.ToString()));
		}
	}
	Receivers.Add(Receiver->ReceiverId, Receiver);
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

void UDocOpticalBeamSubsystem::UnregisterReceiver(UDocBeamReceiverComponent* Receiver)
{
	if (Receiver && FindReceiver(Receiver->ReceiverId) == Receiver)
	{
		Receivers.Remove(Receiver->ReceiverId);
		BumpTopology();
	}
}

// ---------------------------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocOpticalBeamSubsystem::SetEmitterEnabled(FName EmitterId, bool bEnabled)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter not found"));
	}
	if (Emitter->bIsEnabled == bEnabled)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Enabled state unchanged."));
	}
	Emitter->bIsEnabled = bEnabled;
	++Emitter->PathGeneration;
	MarkDirty(EmitterId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::UpdateEmitterPose(FName EmitterId, const FVector& LocalOffset, const FVector& LocalDirection)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter not found"));
	}
	if (LocalOffset.ContainsNaN() || LocalDirection.ContainsNaN() || LocalDirection.IsNearlyZero())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Emitter pose must be finite with a non-zero direction."));
	}
	Emitter->LocalOffset = LocalOffset;
	Emitter->LocalDirection = LocalDirection.GetSafeNormal();
	++Emitter->PathGeneration;
	MarkDirty(EmitterId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::NotifyEmitterMoved(FName EmitterId)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter not found"));
	}
	++Emitter->PathGeneration;
	MarkDirty(EmitterId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::NotifySurfaceMoved(FName SurfaceId)
{
	if (!FindSurface(SurfaceId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Surface not found"));
	}
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::SetSurfaceProfile(FName SurfaceId, UDocOpticalProfile* Profile)
{
	UDocBeamSurfaceComponent* Surface = FindSurface(SurfaceId);
	if (!Surface)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Surface not found"));
	}
	if (Profile)
	{
		const FDocSystemResult Valid = Profile->ValidateProfile();
		if (!Valid.IsSuccess())
		{
			return Valid;
		}
	}
	if (Surface->OpticalProfile == Profile)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Profile unchanged."));
	}
	Surface->OpticalProfile = Profile;
	BumpTopology();
	return FDocSystemResult::MakeSuccess();
}

void UDocOpticalBeamSubsystem::AddProgrammaticPlane(FName Id, const FVector& PlaneOrigin, const FVector& PlaneNormal, UDocBeamSurfaceComponent* SurfaceComp, UDocBeamReceiverComponent* ReceiverComp)
{
	if (PlaneOrigin.ContainsNaN() || PlaneNormal.ContainsNaN() || PlaneNormal.IsNearlyZero())
	{
		UE_LOG(LogDocOpticalBeams, Warning, TEXT("Programmatic plane %s refused: invalid origin/normal."), *Id.ToString());
		return;
	}
	FProgrammaticPlane Plane;
	Plane.Id = Id;
	Plane.Origin = PlaneOrigin;
	Plane.Normal = PlaneNormal.GetSafeNormal();
	Plane.Surface = SurfaceComp;
	Plane.Receiver = ReceiverComp;
	ProgrammaticPlanes.Add(Plane);
	BumpTopology();
}

void UDocOpticalBeamSubsystem::ClearProgrammaticPlanes()
{
	ProgrammaticPlanes.Empty();
	BumpTopology();
}

// ---------------------------------------------------------------------------------------------
// Tracing
// ---------------------------------------------------------------------------------------------

bool UDocOpticalBeamSubsystem::IntersectScene(const UDocBeamEmitterComponent& Emitter, const FVector& RayOrigin, const FVector& RayDir, double MaxDist, bool bIgnoreOwner, FSceneHit& OutHit) const
{
	bool bFound = false;
	double Closest = MaxDist;

	for (const FProgrammaticPlane& Plane : ProgrammaticPlanes)
	{
		const double Denom = FVector::DotProduct(RayDir, Plane.Normal);
		if (Denom < -1e-6) // one-sided: beam must travel against the plane normal
		{
			const double T = FVector::DotProduct(Plane.Origin - RayOrigin, Plane.Normal) / Denom;
			if (T > 1e-4 && T <= Closest)
			{
				bFound = true;
				Closest = T;
				OutHit = FSceneHit();
				OutHit.Point = RayOrigin + RayDir * T;
				OutHit.Normal = Plane.Normal;
				OutHit.Distance = T;
				OutHit.Surface = Plane.Surface.Get();
				OutHit.Receiver = Plane.Receiver.Get();
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DocOpticalBeamTrace), false);
		if (bIgnoreOwner && Emitter.GetOwner())
		{
			Params.AddIgnoredActor(Emitter.GetOwner());
		}
		FHitResult Hit;
		const FVector End = RayOrigin + RayDir * Closest;
		if (World->LineTraceSingleByChannel(Hit, RayOrigin, End, Emitter.TraceChannel, Params) && Hit.bBlockingHit)
		{
			if (Hit.Distance < Closest)
			{
				bFound = true;
				OutHit = FSceneHit();
				OutHit.Point = Hit.ImpactPoint;
				OutHit.Normal = Hit.ImpactNormal;
				OutHit.Distance = Hit.Distance;
				OutHit.Primitive = Hit.GetComponent();
				OutHit.Actor = Hit.GetActor();
				if (OutHit.Actor)
				{
					OutHit.Surface = OutHit.Actor->FindComponentByClass<UDocBeamSurfaceComponent>();
					OutHit.Receiver = OutHit.Actor->FindComponentByClass<UDocBeamReceiverComponent>();
				}
			}
		}
	}
	return bFound;
}

void UDocOpticalBeamSubsystem::TraceInternal(const UDocBeamEmitterComponent& Emitter, FDocBeamPath& OutPath) const
{
	OutPath = FDocBeamPath();
	OutPath.EmitterId = Emitter.EmitterId;
	OutPath.PathGeneration = Emitter.PathGeneration;
	OutPath.TopologyRevision = TopologyRevision;

	FVector Dir = Emitter.GetWorldBeamDirection();
	FVector Origin = Emitter.GetWorldBeamOrigin();
	if (!Emitter.bIsEnabled || Dir.IsNearlyZero() || Dir.ContainsNaN() || Origin.ContainsNaN() || !FMath::IsFinite(Emitter.MaxRange))
	{
		OutPath.TerminationReason = EDocBeamTerminationReason::Disabled;
		return;
	}

	const double MaxRange = FMath::Max(0.0, static_cast<double>(Emitter.MaxRange));
	double Remaining = MaxRange;
	float Intensity = FMath::Clamp(Emitter.InitialIntensity, 0.0f, 1.0f);
	FGameplayTag Channel = Emitter.BeamChannel;
	int32 Reflections = 0;
	TSet<FString> Visited;
	EDocBeamTerminationReason Reason = EDocBeamTerminationReason::None;

	for (int32 SegmentIndex = 0; ; ++SegmentIndex)
	{
		if (SegmentIndex >= Emitter.MaxSegments)
		{
			Reason = EDocBeamTerminationReason::TerminatedByBudget;
			break;
		}
		if (Intensity < Emitter.MinIntensity)
		{
			Reason = EDocBeamTerminationReason::IntensityDepleted;
			break;
		}
		if (Remaining <= UE_KINDA_SMALL_NUMBER)
		{
			Reason = EDocBeamTerminationReason::OutOfRange;
			break;
		}

		FDocBeamSegment Segment;
		Segment.StartPoint = Origin;
		Segment.Direction = Dir;
		Segment.InitialIntensity = Intensity;
		Segment.FinalIntensity = Intensity;
		Segment.ChannelTag = Channel;
		Segment.SegmentIndex = SegmentIndex;

		FSceneHit Hit;
		const bool bIgnoreOwner = Emitter.bIgnoreOwnerOnFirstSegment && SegmentIndex == 0;
		if (!IntersectScene(Emitter, Origin, Dir, Remaining, bIgnoreOwner, Hit))
		{
			Segment.EndPoint = Origin + Dir * Remaining;
			Segment.Length = static_cast<float>(Remaining);
			Segment.TerminationReason = EDocBeamTerminationReason::OutOfRange;
			OutPath.Segments.Add(Segment);
			Remaining = 0.0;
			Reason = EDocBeamTerminationReason::OutOfRange;
			break;
		}

		Segment.EndPoint = Hit.Point;
		Segment.Length = static_cast<float>(Hit.Distance);
		Segment.HitComponent = Hit.Primitive;
		Segment.HitActor = Hit.Actor;
		Segment.HitNormal = Hit.Normal;
		Remaining -= Hit.Distance;

		if (Hit.Receiver && Hit.Receiver->bIsEnabled)
		{
			Segment.HitReceiverId = Hit.Receiver->ReceiverId;
			Segment.TerminationReason = EDocBeamTerminationReason::HitReceiver;
			OutPath.Segments.Add(Segment);
			OutPath.TerminalReceiverId = Hit.Receiver->ReceiverId;
			Reason = EDocBeamTerminationReason::HitReceiver;
			break;
		}

		UDocBeamSurfaceComponent* Surface = (Hit.Surface && Hit.Surface->bIsEnabled) ? Hit.Surface : nullptr;
		const EDocBeamSurfaceType Type = Surface ? Surface->GetSurfaceType() : EDocBeamSurfaceType::Blocker;
		if (Surface)
		{
			Segment.HitSurfaceId = Surface->SurfaceId;
		}
		if (!Surface || Type == EDocBeamSurfaceType::Blocker || Type == EDocBeamSurfaceType::Prism)
		{
			Segment.TerminationReason = EDocBeamTerminationReason::HitBlocker;
			OutPath.Segments.Add(Segment);
			Reason = EDocBeamTerminationReason::HitBlocker;
			break;
		}

		const FVector Normal = Surface->GetSurfaceNormal(Hit.Normal);
		if (!Surface->bTwoSided && FVector::DotProduct(Dir, Normal) > 0.0)
		{
			Segment.TerminationReason = EDocBeamTerminationReason::HitBlocker; // back face of a one-sided surface
			OutPath.Segments.Add(Segment);
			Reason = EDocBeamTerminationReason::HitBlocker;
			break;
		}

		// Visited guard: surface + quantized direction + channel.
		const FIntVector Quantized(static_cast<int32>(FMath::RoundToDouble(Dir.X * 1000.0)), static_cast<int32>(FMath::RoundToDouble(Dir.Y * 1000.0)), static_cast<int32>(FMath::RoundToDouble(Dir.Z * 1000.0)));
		const FString Key = FString::Printf(TEXT("%s|%d,%d,%d|%s"), *Surface->SurfaceId.ToString(), Quantized.X, Quantized.Y, Quantized.Z, *Channel.ToString());
		if (Visited.Contains(Key))
		{
			Segment.TerminationReason = EDocBeamTerminationReason::LoopDetected;
			OutPath.Segments.Add(Segment);
			Reason = EDocBeamTerminationReason::LoopDetected;
			break;
		}
		Visited.Add(Key);

		FVector NewDir = Dir;
		const UDocOpticalProfile* Profile = Surface->OpticalProfile;
		if (Type == EDocBeamSurfaceType::Mirror)
		{
			if (Reflections >= Emitter.MaxReflections)
			{
				Segment.TerminationReason = EDocBeamTerminationReason::TerminatedByBudget;
				OutPath.Segments.Add(Segment);
				Reason = EDocBeamTerminationReason::TerminatedByBudget;
				break;
			}
			++Reflections;
			NewDir = DocOpticalMath::ComputeReflection(Dir, Normal);
			Intensity *= FMath::Clamp(Profile->Reflectivity, 0.0f, 1.0f);
		}
		else // Filter
		{
			if (!Profile->FilterChannels.IsEmpty() && !Channel.MatchesAny(Profile->FilterChannels))
			{
				Segment.TerminationReason = EDocBeamTerminationReason::FilteredOut;
				OutPath.Segments.Add(Segment);
				Reason = EDocBeamTerminationReason::FilteredOut;
				break;
			}
			Intensity *= FMath::Clamp(Profile->TransmissionEfficiency, 0.0f, 1.0f);
			if (Profile->ShiftedOutputChannel.IsValid())
			{
				Channel = Profile->ShiftedOutputChannel;
			}
		}

		Segment.FinalIntensity = Intensity;
		OutPath.Segments.Add(Segment);

		if (NewDir.IsNearlyZero())
		{
			Reason = EDocBeamTerminationReason::HitBlocker;
			break;
		}

		// Self-hit offset is charged to the range ledger, so it can never extend the beam.
		const double Epsilon = FMath::Min(static_cast<double>(Emitter.TraceEpsilon), Remaining);
		Remaining -= Epsilon;
		Dir = NewDir;
		Origin = Hit.Point + Dir * Epsilon;
	}

	OutPath.TotalLength = static_cast<float>(MaxRange - FMath::Max(0.0, Remaining));
	OutPath.ReflectionCount = Reflections;
	OutPath.TerminationReason = Reason;
}

void UDocOpticalBeamSubsystem::ApplyCommittedPath(UDocBeamEmitterComponent& Emitter, const FDocBeamPath& InPath)
{
	FDocBeamPath Path = InPath;
	Path.SolvedAtSeconds = SimTime;
	CachedPaths.Add(Emitter.EmitterId, Path);
	Emitter.CurrentPath = Path;
	DirtySince.Remove(Emitter.EmitterId);

	// Atomically swap this emitter's contribution: install at the terminal receiver, remove everywhere else.
	TArray<FName> ReceiverIds;
	Receivers.GetKeys(ReceiverIds);
	ReceiverIds.Sort(FNameLexicalLess());
	for (const FName& ReceiverId : ReceiverIds)
	{
		UDocBeamReceiverComponent* Receiver = FindReceiver(ReceiverId);
		if (!Receiver)
		{
			continue;
		}
		if (ReceiverId == Path.TerminalReceiverId && Path.Segments.Num() > 0)
		{
			const FDocBeamSegment& Last = Path.Segments.Last();
			FDocReceiverContribution C;
			C.EmitterId = Emitter.EmitterId;
			C.Intensity = Last.InitialIntensity;
			C.Channel = Last.ChannelTag;
			C.PathGeneration = Path.PathGeneration;
			C.SolvedAtSeconds = Path.SolvedAtSeconds;
			Receiver->ReplaceContribution(C);
		}
		else if (Receiver->HasContribution(Emitter.EmitterId))
		{
			Receiver->RemoveContribution(Emitter.EmitterId);
		}
	}

	OnBeamPathUpdated.Broadcast(Emitter.EmitterId, Path);
	OnBeamPathUpdatedNative.Broadcast(Emitter.EmitterId, Path);
}

FDocSystemResult UDocOpticalBeamSubsystem::CreatePathRequest(FName EmitterId, FDocBeamPathRequest& OutRequest)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter not found"));
	}
	OutRequest = FDocBeamPathRequest();
	OutRequest.RequestId = NextRequestId++;
	OutRequest.EmitterId = EmitterId;
	OutRequest.EmitterGeneration = Emitter->PathGeneration;
	OutRequest.TopologyRevision = TopologyRevision;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::TracePath(const FDocBeamPathRequest& Request, FDocBeamPath& OutPath)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(Request.EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter not found"));
	}
	TraceInternal(*Emitter, OutPath);
	// Stamp with the ticket, so a commit can tell exactly which state it was traced from.
	OutPath.PathGeneration = Request.EmitterGeneration;
	OutPath.TopologyRevision = Request.TopologyRevision;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::CommitPath(const FDocBeamPathRequest& Request, const FDocBeamPath& Path)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(Request.EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter was removed; late trace discarded."));
	}
	if (Path.EmitterId != Request.EmitterId || Path.PathGeneration != Request.EmitterGeneration || Path.TopologyRevision != Request.TopologyRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Path does not belong to this request."));
	}
	if (Emitter->PathGeneration != Request.EmitterGeneration || TopologyRevision != Request.TopologyRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Emitter or surfaces changed after the trace; stale result discarded."));
	}
	ApplyCommittedPath(*Emitter, Path);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocOpticalBeamSubsystem::SolveEmitterPath(UDocBeamEmitterComponent* Emitter, FDocBeamPath& OutPath)
{
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Emitter is null"));
	}
	if (FindEmitter(Emitter->EmitterId) != Emitter)
	{
		const FDocSystemResult Registered = RegisterEmitter(Emitter);
		if (!Registered.IsSuccess())
		{
			return Registered;
		}
	}
	FDocBeamPathRequest Request;
	CreatePathRequest(Emitter->EmitterId, Request);
	TracePath(Request, OutPath);
	const FDocSystemResult Committed = CommitPath(Request, OutPath);
	if (Committed.IsSuccess())
	{
		OutPath = Emitter->CurrentPath;
	}
	return Committed;
}

FDocSystemResult UDocOpticalBeamSubsystem::RequestPathRefresh(FName EmitterId)
{
	UDocBeamEmitterComponent* Emitter = FindEmitter(EmitterId);
	if (!Emitter)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Emitter not found"));
	}
	FDocBeamPath Path;
	return SolveEmitterPath(Emitter, Path);
}

FDocSystemResult UDocOpticalBeamSubsystem::RequestAllPathsRefresh()
{
	for (const FName& Id : SortedEmitterIds())
	{
		RequestPathRefresh(Id);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocOpticalBeamSubsystem::AdvanceSimulation(float DeltaSeconds)
{
	if (DeltaSeconds < 0.0f || !FMath::IsFinite(DeltaSeconds))
	{
		return;
	}
	SimTime += DeltaSeconds;

	// Drop dead registrations.
	for (auto It = Emitters.CreateIterator(); It; ++It) { if (!It->Value.IsValid()) { DirtySince.Remove(It->Key); CachedPaths.Remove(It->Key); It.RemoveCurrent(); } }
	for (auto It = Surfaces.CreateIterator(); It; ++It) { if (!It->Value.IsValid()) { It.RemoveCurrent(); } }
	for (auto It = Receivers.CreateIterator(); It; ++It) { if (!It->Value.IsValid()) { It.RemoveCurrent(); } }

	// 1. Re-solve queued emitters within budget (round-robin by id).
	TArray<FName> Pending;
	DirtySince.GetKeys(Pending);
	Pending.Sort(FNameLexicalLess());
	const int32 Budget = FMath::Min(Pending.Num(), FMath::Max(0, MaxSolvesPerTick));
	if (Pending.Num() > 0 && Budget > 0)
	{
		const int32 Start = SolveCursor % Pending.Num();
		for (int32 k = 0; k < Budget; ++k)
		{
			RequestPathRefresh(Pending[(Start + k) % Pending.Num()]);
		}
		SolveCursor = Start + Budget;
	}

	// 2. Staleness policy: contributions from paths waiting too long cannot grant eligibility.
	TArray<FName> ReceiverIds;
	Receivers.GetKeys(ReceiverIds);
	ReceiverIds.Sort(FNameLexicalLess());
	for (const FName& ReceiverId : ReceiverIds)
	{
		UDocBeamReceiverComponent* Receiver = FindReceiver(ReceiverId);
		if (!Receiver)
		{
			continue;
		}
		for (const FDocReceiverContribution& C : TArray<FDocReceiverContribution>(Receiver->GetReceiverState().Contributions))
		{
			const double* Since = DirtySince.Find(C.EmitterId);
			const bool bStale = Since && (SimTime - *Since) > MaxPathStalenessSeconds;
			Receiver->SetContributionHeld(C.EmitterId, bStale);
		}
	}

	// 3. Receivers advance on the simulation clock, in id order.
	for (const FName& ReceiverId : ReceiverIds)
	{
		if (UDocBeamReceiverComponent* Receiver = FindReceiver(ReceiverId))
		{
			Receiver->AdvanceDwell(DeltaSeconds);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Queries and persistence
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocOpticalBeamSubsystem::QueryPath(FName EmitterId, FDocBeamPath& OutPath) const
{
	if (const FDocBeamPath* Found = CachedPaths.Find(EmitterId))
	{
		OutPath = *Found;
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Path not found for emitter"));
}

FDocSystemResult UDocOpticalBeamSubsystem::QueryReceiver(FName ReceiverId, FDocReceiverState& OutState) const
{
	if (const UDocBeamReceiverComponent* Receiver = FindReceiver(ReceiverId))
	{
		OutState = Receiver->GetReceiverState();
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Receiver not found"));
}

bool UDocOpticalBeamSubsystem::IsPathPending(FName EmitterId) const
{
	return DirtySince.Contains(EmitterId);
}

double UDocOpticalBeamSubsystem::GetPathAgeSeconds(FName EmitterId) const
{
	const FDocBeamPath* Found = CachedPaths.Find(EmitterId);
	return Found ? SimTime - Found->SolvedAtSeconds : 0.0;
}

FDocSystemResult UDocOpticalBeamSubsystem::CaptureReceiverState(FName ReceiverId, FDocReceiverSnapshot& OutSnapshot) const
{
	if (const UDocBeamReceiverComponent* Receiver = FindReceiver(ReceiverId))
	{
		OutSnapshot = Receiver->CaptureSnapshot();
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Receiver not found"));
}

FDocSystemResult UDocOpticalBeamSubsystem::RestoreReceiverState(const FDocReceiverSnapshot& Snapshot)
{
	UDocBeamReceiverComponent* Receiver = FindReceiver(Snapshot.ReceiverId);
	if (!Receiver)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Receiver not found"));
	}
	Receiver->RestoreSnapshot(Snapshot);
	MarkAllDirty(); // stale trace hits are never trusted after restore
	return FDocSystemResult::MakeSuccess();
}
