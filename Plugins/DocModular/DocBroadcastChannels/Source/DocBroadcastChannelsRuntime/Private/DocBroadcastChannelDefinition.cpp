#include "DocBroadcastChannelDefinition.h"

namespace DocBroadcastPrivate
{
	constexpr double MaxScheduleOffset = 1.0e12;
	constexpr double MaxTotalDuration = 1.0e8;
}

FDocSystemResult UDocBroadcastChannelDefinition::ValidateChannel() const
{
	if (ChannelId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Channel has no ChannelId"));
	}
	if (Schedule.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Channel schedule is empty"));
	}
	if (!FMath::IsFinite(GapDurationSeconds) || GapDurationSeconds < 0.0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Gap must be finite and >= 0"));
	}
	double PreviousEnd = 0.0;
	for (int32 Index = 0; Index < Schedule.Num(); ++Index)
	{
		const FDocBroadcastScheduleEntry& Entry = Schedule[Index];
		if (!Entry.Program)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Entry %d has no program"), Index));
		}
		const FDocSystemResult ProgramValid = Entry.Program->ValidateProgram(/*bRequireSeek*/ true);
		if (!ProgramValid.IsSuccess())
		{
			return ProgramValid;
		}
		if (!FMath::IsFinite(Entry.StartOffsetSeconds) || !FMath::IsFinite(Entry.DurationSeconds) || Entry.StartOffsetSeconds < 0.0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Entry %d has a non-finite or negative time"), Index));
		}
		if (Entry.DurationSeconds <= 0.0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Entry %d has zero length"), Index));
		}
		if (Entry.DurationSeconds > Entry.Program->DurationSeconds + KINDA_SMALL_NUMBER)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
				FString::Printf(TEXT("Entry %d slot is longer than program %s"), Index, *Entry.Program->ProgramId.ToString()));
		}
		if (Entry.StartOffsetSeconds < PreviousEnd - KINDA_SMALL_NUMBER)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
				FString::Printf(TEXT("Entry %d overlaps or is out of order"), Index));
		}
		PreviousEnd = Entry.StartOffsetSeconds + Entry.DurationSeconds;
	}
	const double Total = GetTotalDuration();
	if (!FMath::IsFinite(Total) || Total <= 0.0 || Total > DocBroadcastPrivate::MaxTotalDuration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Total schedule duration out of range"));
	}
	return FDocSystemResult::MakeSuccess();
}

double UDocBroadcastChannelDefinition::GetTotalDuration() const
{
	double MaxEnd = 0.0;
	for (const FDocBroadcastScheduleEntry& Entry : Schedule)
	{
		MaxEnd = FMath::Max(MaxEnd, Entry.StartOffsetSeconds + Entry.DurationSeconds);
	}
	return MaxEnd + GapDurationSeconds;
}

const UDocBroadcastProgramDefinition* UDocBroadcastChannelDefinition::GetEntryProgram(int32 EntryIndex) const
{
	return Schedule.IsValidIndex(EntryIndex) ? Schedule[EntryIndex].Program.Get() : nullptr;
}

bool UDocBroadcastChannelDefinition::ResolveSchedule(double ScheduleOffset, FDocBroadcastResolvedSlot& OutSlot) const
{
	OutSlot = FDocBroadcastResolvedSlot();
	const double Total = GetTotalDuration();
	if (Schedule.IsEmpty() || !FMath::IsFinite(ScheduleOffset) || !FMath::IsFinite(Total) || Total <= 0.0
		|| FMath::Abs(ScheduleOffset) > DocBroadcastPrivate::MaxScheduleOffset)
	{
		return false;
	}

	double Local = ScheduleOffset;
	int64 Loop = 0;
	if (bIsLooping)
	{
		Loop = static_cast<int64>(FMath::FloorToDouble(ScheduleOffset / Total));
		Local = ScheduleOffset - static_cast<double>(Loop) * Total;
		// Guard floating error at the wrap point.
		if (Local >= Total)
		{
			Local -= Total;
			++Loop;
		}
		if (Local < 0.0)
		{
			Local = 0.0;
		}
	}
	else if (ScheduleOffset < 0.0 || ScheduleOffset >= Total)
	{
		return false;
	}

	const double LoopBase = static_cast<double>(Loop) * Total;
	OutSlot.LoopIndex = Loop;
	double GapStart = 0.0;
	for (int32 Index = 0; Index < Schedule.Num(); ++Index)
	{
		const FDocBroadcastScheduleEntry& Entry = Schedule[Index];
		const double Start = Entry.StartOffsetSeconds;
		const double End = Start + Entry.DurationSeconds;
		if (Local < Start)
		{
			// Inside the gap before this entry.
			OutSlot.SlotStartOffset = LoopBase + GapStart;
			OutSlot.SlotEndOffset = LoopBase + Start;
			return true;
		}
		if (Local < End)
		{
			OutSlot.bHasProgram = Entry.Program != nullptr;
			OutSlot.EntryIndex = Index;
			OutSlot.ProgramCursorSeconds = Local - Start;
			OutSlot.SlotStartOffset = LoopBase + Start;
			OutSlot.SlotEndOffset = LoopBase + End;
			return true;
		}
		GapStart = End;
	}
	// Trailing gap.
	OutSlot.SlotStartOffset = LoopBase + GapStart;
	OutSlot.SlotEndOffset = LoopBase + Total;
	return true;
}
