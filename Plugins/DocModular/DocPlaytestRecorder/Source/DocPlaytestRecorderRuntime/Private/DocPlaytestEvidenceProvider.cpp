#include "DocPlaytestEvidenceProvider.h"

bool UDocSamplePlaytestProvider::CaptureSnapshot(FDocDiagnosticSnapshot& OutSnapshot, FString& OutError)
{
	if (bSimulateFailure)
	{
		OutError = TEXT("Simulated provider snapshot failure");
		return false;
	}

	OutSnapshot.SnapshotId = FGuid::NewGuid();
	OutSnapshot.Timestamp = FDateTime::UtcNow();
	OutSnapshot.ProviderName = GetProviderName();
	OutSnapshot.DataJson = CustomPayloadData;
	return true;
}

void UDocSamplePlaytestProvider::NotifyWorldEpochBoundary(const FString& WorldName)
{
	EpochNotificationCount++;
	LastNotifiedWorld = WorldName;
}
