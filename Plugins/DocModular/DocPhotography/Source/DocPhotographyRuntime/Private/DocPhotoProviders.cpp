#include "DocPhotoProviders.h"
#include "DocPhotographySubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderCommandFence.h"
#include "RHI.h"
#include "TextureResource.h"

// ---------------------------------------------------------------------------------------------
// Synthetic source
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocPhotoSyntheticImageSource::BeginCapture(UWorld* World, const FDocPhotoCaptureRequest& Request, int64 Ticket, UDocPhotographySubsystem* Sink)
{
	if (!Sink || !World)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Missing world or sink"));
	}
	FPendingCapture Capture;
	Capture.Ticket = Ticket;
	Capture.Width = Request.Width;
	Capture.Height = Request.Height;
	Capture.World = World;
	Capture.Sink = Sink;
	if (bDeferred)
	{
		Pending.Add(Capture);
	}
	else
	{
		Complete(Capture);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocPhotoSyntheticImageSource::Complete(const FPendingCapture& Capture)
{
	UDocPhotographySubsystem* Sink = Capture.Sink.Get();
	if (!Sink)
	{
		return;
	}
	if (bFailNextCapture)
	{
		bFailNextCapture = false;
		Sink->FailCapture(Capture.Ticket, TEXT("SimulatedRenderFailure"));
		return;
	}
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Capture.Width * Capture.Height);
	for (int32 Y = 0; Y < Capture.Height; ++Y)
	{
		for (int32 X = 0; X < Capture.Width; ++X)
		{
			Pixels[Y * Capture.Width + X] = FColor(static_cast<uint8>(X * 255 / FMath::Max(1, Capture.Width - 1)),
				static_cast<uint8>(Y * 255 / FMath::Max(1, Capture.Height - 1)), static_cast<uint8>((X + Y) & 0xFF), 255);
		}
	}
	const UWorld* World = Capture.World.Get();
	const double Now = (World ? World->GetTimeSeconds() : 0.0) + SimulatedDelaySeconds;
	Sink->SubmitPixels(Capture.Ticket, Capture.Width, Capture.Height, Pixels, Now);
}

void UDocPhotoSyntheticImageSource::Poll()
{
	TArray<FPendingCapture> Ready = MoveTemp(Pending);
	Pending.Reset();
	for (const FPendingCapture& Capture : Ready)
	{
		Complete(Capture);
	}
}

void UDocPhotoSyntheticImageSource::CancelCapture(int64 Ticket)
{
	Pending.RemoveAll([Ticket](const FPendingCapture& C) { return C.Ticket == Ticket; });
}

bool UDocPhotoSyntheticImageSource::DeliverLate(int64 Ticket, int32 Width, int32 Height)
{
	FPendingCapture Capture;
	Capture.Ticket = Ticket;
	Capture.Width = Width;
	Capture.Height = Height;
	UDocPhotographySubsystem* Sink = Cast<UDocPhotographySubsystem>(GetOuter());
	if (!Sink)
	{
		return false;
	}
	TArray<FColor> Pixels;
	Pixels.Init(FColor::White, Width * Height);
	return Sink->SubmitPixels(Ticket, Width, Height, Pixels, 0.0);
}

// ---------------------------------------------------------------------------------------------
// Scene capture source
// ---------------------------------------------------------------------------------------------

bool UDocSceneCapturePhotoSource::IsRenderingAvailable()
{
	return FApp::CanEverRender() && !GUsingNullRHI;
}

FDocSystemResult UDocSceneCapturePhotoSource::BeginCapture(UWorld* World, const FDocPhotoCaptureRequest& Request, int64 Ticket, UDocPhotographySubsystem* Sink)
{
	if (!IsRenderingAvailable())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("RenderingUnavailable: scene capture needs a real RHI (not NullRHI or a server)"));
	}
	if (!World || !Sink)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Missing world or sink"));
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Request.CameraRotation, Request.CameraLocation), Params);
	if (!Actor)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Could not spawn capture actor"));
	}

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->InitAutoFormat(Request.Width, Request.Height);
	Target->UpdateResourceImmediate(true);
	Targets.Add(Target);

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Actor);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->FOVAngle = Request.FOVDegrees;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->TextureTarget = Target;
	Actor->SetRootComponent(Capture);
	Capture->RegisterComponent();
	Capture->SetWorldLocationAndRotation(Request.CameraLocation, Request.CameraRotation);
	Capture->CaptureScene();

	FPendingCapture Entry;
	Entry.Ticket = Ticket;
	Entry.CaptureActor = Actor;
	Entry.Target = Target;
	Entry.Sink = Sink;
	Entry.Fence = MakeShared<FRenderCommandFence>();
	Entry.Fence->BeginFence();
	Entry.StartTime = World->GetTimeSeconds();
	Pending.Add(Entry);
	return FDocSystemResult::MakeSuccess();
}

void UDocSceneCapturePhotoSource::Poll()
{
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		FPendingCapture& Entry = Pending[i];
		if (Entry.Fence.IsValid() && !Entry.Fence->IsFenceComplete())
		{
			continue;
		}
		UDocPhotographySubsystem* Sink = Entry.Sink.Get();
		UTextureRenderTarget2D* Target = Entry.Target.Get();
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (Sink && Resource)
		{
			TArray<FColor> Pixels;
			if (Resource->ReadPixels(Pixels) && Pixels.Num() == Target->SizeX * Target->SizeY)
			{
				const AActor* Actor = Entry.CaptureActor.Get();
				const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
				Sink->SubmitPixels(Entry.Ticket, Target->SizeX, Target->SizeY, Pixels, World ? World->GetTimeSeconds() : Entry.StartTime);
			}
			else
			{
				Sink->FailCapture(Entry.Ticket, TEXT("ReadbackFailed"));
			}
		}
		Release(Entry);
		Pending.RemoveAt(i);
	}
}

void UDocSceneCapturePhotoSource::Release(FPendingCapture& Entry)
{
	if (AActor* Actor = Entry.CaptureActor.Get())
	{
		Actor->Destroy();
	}
	if (UTextureRenderTarget2D* Target = Entry.Target.Get())
	{
		Targets.Remove(Target);
		Target->ReleaseResource();
	}
}

void UDocSceneCapturePhotoSource::CancelCapture(int64 Ticket)
{
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		if (Pending[i].Ticket == Ticket)
		{
			if (Pending[i].Fence.IsValid())
			{
				Pending[i].Fence->Wait(); // resources are released only after the render thread is done with them
			}
			Release(Pending[i]);
			Pending.RemoveAt(i);
		}
	}
}

void UDocSceneCapturePhotoSource::BeginDestroy()
{
	for (FPendingCapture& Entry : Pending)
	{
		if (Entry.Fence.IsValid())
		{
			Entry.Fence->Wait();
		}
	}
	Pending.Reset();
	Super::BeginDestroy();
}

// ---------------------------------------------------------------------------------------------
// Blob stores
// ---------------------------------------------------------------------------------------------

bool UDocPhotoBlobStore::IsValidKey(const FString& Key)
{
	if (Key.IsEmpty() || Key.Len() > 200 || Key.StartsWith(TEXT("/")) || Key.Contains(TEXT("..")) || Key.Contains(TEXT(":")) || Key.Contains(TEXT("\\")))
	{
		return false;
	}
	for (const TCHAR C : Key)
	{
		if (!(FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_') || C == TEXT('.') || C == TEXT('/')))
		{
			return false;
		}
	}
	return true;
}

bool UDocPhotoMemoryBlobStore::WriteBlob(const FString& Key, const TArray64<uint8>& Bytes, FString& OutError)
{
	if (!IsValidKey(Key))
	{
		OutError = TEXT("Invalid blob key");
		return false;
	}
	if (bFailWrites)
	{
		OutError = TEXT("Simulated storage write failure");
		return false;
	}
	Blobs.Add(Key, Bytes);
	return true;
}

bool UDocPhotoMemoryBlobStore::ReadBlob(const FString& Key, TArray64<uint8>& OutBytes) const
{
	if (const TArray64<uint8>* Found = Blobs.Find(Key))
	{
		OutBytes = *Found;
		return true;
	}
	return false;
}

bool UDocPhotoMemoryBlobStore::BlobExists(const FString& Key) const
{
	return Blobs.Contains(Key);
}

bool UDocPhotoMemoryBlobStore::DeleteBlob(const FString& Key)
{
	return Blobs.Remove(Key) > 0;
}

FString UDocPhotoFileBlobStore::ResolveRoot() const
{
	const FString Root = RootDirectory.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DocPhotography")) : RootDirectory;
	return FPaths::ConvertRelativePathToFull(Root);
}

bool UDocPhotoFileBlobStore::ResolvePath(const FString& Key, FString& OutPath) const
{
	if (!IsValidKey(Key))
	{
		return false;
	}
	const FString Root = ResolveRoot();
	OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Root, Key));
	FPaths::NormalizeFilename(OutPath);
	FString NormalizedRoot = Root;
	FPaths::NormalizeDirectoryName(NormalizedRoot);
	return OutPath.StartsWith(NormalizedRoot + TEXT("/"));
}

bool UDocPhotoFileBlobStore::WriteBlob(const FString& Key, const TArray64<uint8>& Bytes, FString& OutError)
{
	FString Path;
	if (!ResolvePath(Key, Path))
	{
		OutError = TEXT("Invalid blob key");
		return false;
	}
	const FString Temp = Path + TEXT(".tmp");
	if (!FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *Temp))
	{
		IFileManager::Get().Delete(*Temp, false, true, true);
		OutError = TEXT("Could not write temporary file");
		return false;
	}
	if (!IFileManager::Get().Move(*Path, *Temp, /*bReplace*/ true))
	{
		IFileManager::Get().Delete(*Temp, false, true, true);
		OutError = TEXT("Could not move file into place");
		return false;
	}
	return true;
}

bool UDocPhotoFileBlobStore::ReadBlob(const FString& Key, TArray64<uint8>& OutBytes) const
{
	FString Path;
	return ResolvePath(Key, Path) && FFileHelper::LoadFileToArray(OutBytes, *Path);
}

bool UDocPhotoFileBlobStore::BlobExists(const FString& Key) const
{
	FString Path;
	return ResolvePath(Key, Path) && FPaths::FileExists(Path);
}

bool UDocPhotoFileBlobStore::DeleteBlob(const FString& Key)
{
	FString Path;
	return ResolvePath(Key, Path) && IFileManager::Get().Delete(*Path, false, true, true); // only inside the controlled root
}
