#include "DocBroadcastPlaybackProvider.h"
#include "DocBroadcastProgramDefinition.h"
#include "DocBroadcastSubsystem.h"

FDocBroadcastProviderCapabilities UDocBroadcastSyntheticProvider::GetCapabilities(const UDocBroadcastProgramDefinition* Program) const
{
	FDocBroadcastProviderCapabilities Caps;
	Caps.bCanSeek = bCanSeek;
	Caps.bKnownDuration = bKnownDuration;
	Caps.bSupportsAudio = true;
	Caps.bSupportsVideo = false;
	Caps.bAudible = false;
	Caps.SeekPrecisionSeconds = 0.001;
	Caps.MaxConcurrentStreams = 64;
	return Caps;
}

FDocSystemResult UDocBroadcastSyntheticProvider::OpenProgram(int64 Ticket, const UDocBroadcastProgramDefinition* Program, double StartCursorSeconds, UDocBroadcastSubsystem* Sink)
{
	if (!Program || !Sink)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No program or sink"));
	}
	++OpenCount;
	LastOpenCursorSeconds = StartCursorSeconds;
	LastOpenedProgramId = Program->ProgramId;
	const bool bWillSucceed = !FailingProgramIds.Contains(Program->ProgramId);
	if (!bDeferred)
	{
		Sink->NotifyOpenResult(Ticket, bWillSucceed, bWillSucceed ? FString() : TEXT("SyntheticOpenFailure"));
		return FDocSystemResult::MakeSuccess();
	}
	FPending Entry;
	Entry.ProgramId = Program->ProgramId;
	Entry.bWillSucceed = bWillSucceed;
	Entry.Sink = Sink;
	Pending.Add(Ticket, Entry);
	return FDocSystemResult::MakeSuccess();
}

void UDocBroadcastSyntheticProvider::CancelOpen(int64 Ticket)
{
	if (FPending* Entry = Pending.Find(Ticket))
	{
		Entry->bCancelled = true; // kept so a test can simulate the backend calling back anyway
		++CancelCount;
	}
}

bool UDocBroadcastSyntheticProvider::DeliverTicket(int64 Ticket, bool bForceSuccess)
{
	FPending Entry;
	if (!Pending.RemoveAndCopyValue(Ticket, Entry))
	{
		return false;
	}
	if (UDocBroadcastSubsystem* Sink = Entry.Sink.Get())
	{
		const bool bOk = bForceSuccess || Entry.bWillSucceed;
		return Sink->NotifyOpenResult(Ticket, bOk, bOk ? FString() : TEXT("SyntheticOpenFailure"));
	}
	return false;
}

int32 UDocBroadcastSyntheticProvider::DeliverAll()
{
	int32 Accepted = 0;
	TArray<int64> Tickets;
	for (const TPair<int64, FPending>& Kvp : Pending)
	{
		if (!Kvp.Value.bCancelled)
		{
			Tickets.Add(Kvp.Key);
		}
	}
	Tickets.Sort();
	for (int64 Ticket : Tickets)
	{
		Accepted += DeliverTicket(Ticket) ? 1 : 0;
	}
	return Accepted;
}

TArray<int64> UDocBroadcastSyntheticProvider::GetPendingTickets() const
{
	TArray<int64> Out;
	for (const TPair<int64, FPending>& Kvp : Pending)
	{
		if (!Kvp.Value.bCancelled)
		{
			Out.Add(Kvp.Key);
		}
	}
	Out.Sort();
	return Out;
}
