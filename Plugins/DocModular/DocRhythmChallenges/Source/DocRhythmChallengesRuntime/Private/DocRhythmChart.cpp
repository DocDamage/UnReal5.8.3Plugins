#include "DocRhythmChart.h"
#include "Misc/SecureHash.h"

TArray<FDocRhythmNote> UDocRhythmChart::GetSortedNotes() const
{
	TArray<FDocRhythmNote> Sorted = Notes;
	Sorted.StableSort([](const FDocRhythmNote& A, const FDocRhythmNote& B)
	{
		if (A.TimeOffsetUs != B.TimeOffsetUs)
		{
			return A.TimeOffsetUs < B.TimeOffsetUs;
		}
		return A.NoteId.LexicalLess(B.NoteId);
	});
	return Sorted;
}

FString UDocRhythmChart::ComputeContentHash() const
{
	FString Signature = FString::Printf(TEXT("%s|%lld|%lld|%lld|%.4f|%d|%s|%lld"),
		*ChartId.ToString(), DurationUs, ChartOffsetUs, CountInUs, BPM, LaneCount, *AudioSourceIdentifier, AudioDurationUs);
	for (const FDocRhythmNote& Note : GetSortedNotes())
	{
		Signature += FString::Printf(TEXT("|%s:%d:%d:%lld:%lld"),
			*Note.NoteId.ToString(), Note.LaneId, static_cast<int32>(Note.Kind), Note.TimeOffsetUs, Note.DurationUs);
	}
	return FMD5::HashAnsiString(*Signature);
}

int64 UDocRhythmChart::BeatToMicroseconds(double Beat) const
{
	if (!FMath::IsFinite(Beat) || !FMath::IsFinite(BPM) || BPM <= 0.0f)
	{
		return 0;
	}
	const double Us = Beat * 60000000.0 / static_cast<double>(BPM);
	return static_cast<int64>(Us < 0.0 ? -FMath::FloorToDouble(-Us + 0.5) : FMath::FloorToDouble(Us + 0.5)) + ChartOffsetUs;
}

int64 UDocRhythmChart::CalculateMaxPossibleScore(const UDocRhythmJudgmentProfile* InProfile) const
{
	const UDocRhythmJudgmentProfile* ProfileToUse = InProfile ? InProfile : DefaultJudgmentProfile.Get();
	const int64 PerfectPoints = ProfileToUse ? static_cast<int64>(ProfileToUse->PerfectPoints) : 1000;
	return static_cast<int64>(FMath::Min(Notes.Num(), MaxNotes)) * PerfectPoints;
}

FDocSystemResult UDocRhythmChart::ValidateChart() const
{
	if (ChartId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ChartId is None"));
	}
	if (DurationUs <= 0 || DurationUs > MaxDurationUs)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Chart DurationUs must be in (0, 1h]"));
	}
	if (CountInUs < 0 || CountInUs > 60000000)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("CountInUs must be in [0, 60s]"));
	}
	if (!FMath::IsFinite(BPM) || BPM <= 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Chart BPM must be positive"));
	}
	if (LaneCount <= 0 || LaneCount > 16)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Chart LaneCount must be in [1, 16]"));
	}
	if (Notes.Num() == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Chart has no notes"));
	}
	if (Notes.Num() > MaxNotes)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Chart exceeds %d notes"), MaxNotes));
	}
	if (AudioDurationUs > 0 && FMath::Abs(AudioDurationUs - DurationUs) > 1000)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Audio duration %lld does not match chart duration %lld (notes are never rescaled)"), AudioDurationUs, DurationUs));
	}

	TSet<FName> SeenIds;
	for (int32 Index = 0; Index < Notes.Num(); ++Index)
	{
		const FDocRhythmNote& Note = Notes[Index];
		if (Note.NoteId.IsNone())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Note at index %d has no NoteId"), Index));
		}
		bool bAlreadySeen = false;
		SeenIds.Add(Note.NoteId, &bAlreadySeen);
		if (bAlreadySeen)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Duplicate NoteId: %s"), *Note.NoteId.ToString()));
		}
		if (Note.LaneId < 0 || Note.LaneId >= LaneCount)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Note %s has invalid LaneId %d"), *Note.NoteId.ToString(), Note.LaneId));
		}
		if (Note.TimeOffsetUs < 0 || Note.TimeOffsetUs > DurationUs)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Note %s time out of bounds"), *Note.NoteId.ToString()));
		}
		if (Note.Kind == EDocRhythmNoteKind::Hold)
		{
			if (Note.DurationUs <= 0)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Hold %s must end after it starts"), *Note.NoteId.ToString()));
			}
			if (Note.DurationUs > DurationUs - Note.TimeOffsetUs)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Hold %s extends past the chart"), *Note.NoteId.ToString()));
			}
		}
		else if (Note.DurationUs != 0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Tap %s must have zero duration"), *Note.NoteId.ToString()));
		}
	}

	// Same-lane ambiguity: simultaneous notes, or anything inside a hold's span.
	const TArray<FDocRhythmNote> Sorted = GetSortedNotes();
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		const FDocRhythmNote& A = Sorted[i];
		const int64 AEnd = A.TimeOffsetUs + A.DurationUs;
		for (int32 j = i + 1; j < Sorted.Num(); ++j)
		{
			const FDocRhythmNote& B = Sorted[j];
			if (B.TimeOffsetUs > AEnd)
			{
				break;
			}
			if (A.LaneId != B.LaneId)
			{
				continue;
			}
			if (B.TimeOffsetUs == A.TimeOffsetUs || (A.Kind == EDocRhythmNoteKind::Hold && B.TimeOffsetUs <= AEnd))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
					FString::Printf(TEXT("Ambiguous same-lane notes %s and %s"), *A.NoteId.ToString(), *B.NoteId.ToString()));
			}
		}
	}
	return FDocSystemResult::MakeSuccess();
}
