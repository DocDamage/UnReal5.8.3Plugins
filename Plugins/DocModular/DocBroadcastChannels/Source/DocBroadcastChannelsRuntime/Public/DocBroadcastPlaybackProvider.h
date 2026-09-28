#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocSystemResult.h"
#include "DocBroadcastTypes.h"
#include "DocBroadcastPlaybackProvider.generated.h"

class UDocBroadcastProgramDefinition;
class UDocBroadcastSubsystem;

/**
 * Playback backend seam (the spec's IDocBroadcastPlaybackProvider).
 *
 * OpenProgram starts an asynchronous open. Accepting the request is not readiness: the provider must later
 * call UDocBroadcastSubsystem::NotifyOpenResult with the same ticket. Tickets the subsystem no longer waits
 * for (replaced, cancelled, released) are ignored, so a late callback can never start a stale program.
 */
UCLASS(Abstract)
class DOCBROADCASTCHANNELSRUNTIME_API UDocBroadcastPlaybackProvider : public UObject
{
	GENERATED_BODY()

public:
	virtual FDocBroadcastProviderCapabilities GetCapabilities(const UDocBroadcastProgramDefinition* Program) const
	{
		return FDocBroadcastProviderCapabilities();
	}

	virtual FDocSystemResult OpenProgram(int64 Ticket, const UDocBroadcastProgramDefinition* Program, double StartCursorSeconds, UDocBroadcastSubsystem* Sink)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Abstract playback provider"));
	}

	virtual void CancelOpen(int64 Ticket) {}

	/** Called by the subsystem each advance so deferred providers can deliver results. */
	virtual void Poll() {}
};

/**
 * Logic-only provider used by default and in tests. It produces no sound (bAudible = false).
 * Results are delivered immediately unless bDeferred is set, in which case tests deliver them explicitly.
 */
UCLASS()
class DOCBROADCASTCHANNELSRUNTIME_API UDocBroadcastSyntheticProvider : public UDocBroadcastPlaybackProvider
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bDeferred = false;

	UPROPERTY()
	bool bCanSeek = true;

	UPROPERTY()
	bool bKnownDuration = true;

	/** Programs whose opens fail. */
	UPROPERTY()
	TArray<FName> FailingProgramIds;

	UPROPERTY()
	int32 OpenCount = 0;

	UPROPERTY()
	int32 CancelCount = 0;

	UPROPERTY()
	double LastOpenCursorSeconds = 0.0;

	UPROPERTY()
	FName LastOpenedProgramId = NAME_None;

	virtual FDocBroadcastProviderCapabilities GetCapabilities(const UDocBroadcastProgramDefinition* Program) const override;
	virtual FDocSystemResult OpenProgram(int64 Ticket, const UDocBroadcastProgramDefinition* Program, double StartCursorSeconds, UDocBroadcastSubsystem* Sink) override;
	virtual void CancelOpen(int64 Ticket) override;

	/** Deferred mode: delivers one pending result even if it was cancelled (simulates a late backend callback). */
	bool DeliverTicket(int64 Ticket, bool bForceSuccess = false);
	/** Deferred mode: delivers every pending (non-cancelled) result. */
	int32 DeliverAll();
	TArray<int64> GetPendingTickets() const;

private:
	struct FPending
	{
		FName ProgramId;
		bool bWillSucceed = true;
		bool bCancelled = false;
		TWeakObjectPtr<UDocBroadcastSubsystem> Sink;
	};
	TMap<int64, FPending> Pending;
};
