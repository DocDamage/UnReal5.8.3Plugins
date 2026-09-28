#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocSystemResult.h"
#include "DocPhotoTypes.h"
#include "DocPhotoProviders.generated.h"

class UDocPhotographySubsystem;
class UWorld;
class AActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class FRenderCommandFence;

/**
 * Produces pixels for a capture ticket and reports them through UDocPhotographySubsystem::SubmitPixels / FailCapture.
 * A source never fabricates pixels it did not produce.
 */
UCLASS(Abstract)
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoImageSource : public UObject
{
	GENERATED_BODY()

public:
	virtual FDocSystemResult BeginCapture(UWorld* World, const FDocPhotoCaptureRequest& Request, int64 Ticket, UDocPhotographySubsystem* Sink)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Abstract image source"));
	}

	/** Advance pending work (called by the subsystem each update). */
	virtual void Poll() {}

	/** Drop a pending ticket; any later result for it is ignored by the subsystem anyway. */
	virtual void CancelCapture(int64 Ticket) {}
};

/**
 * Deterministic headless source (gradient pixels). For tests, dedicated servers and logic-only runs.
 * It is labelled as synthetic: records made with it carry ImageFormat "PNG" of a generated image, not the scene.
 */
UCLASS()
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoSyntheticImageSource : public UDocPhotoImageSource
{
	GENERATED_BODY()

public:
	/** When true, pixels arrive on the next Poll instead of inside BeginCapture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bDeferred = false;

	/** Added to the completion time, to exercise the frame-coherence tolerance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	float SimulatedDelaySeconds = 0.0f;

	/** Report a render failure for the next capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bFailNextCapture = false;

	virtual FDocSystemResult BeginCapture(UWorld* World, const FDocPhotoCaptureRequest& Request, int64 Ticket, UDocPhotographySubsystem* Sink) override;
	virtual void Poll() override;
	virtual void CancelCapture(int64 Ticket) override;

	int32 GetPendingCount() const { return Pending.Num(); }

	/** Delivers pixels for a ticket even if it was cancelled (simulates a late GPU callback). */
	bool DeliverLate(int64 Ticket, int32 Width, int32 Height);

private:
	struct FPendingCapture
	{
		int64 Ticket = 0;
		int32 Width = 0;
		int32 Height = 0;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UDocPhotographySubsystem> Sink;
	};

	void Complete(const FPendingCapture& Capture);
	TArray<FPendingCapture> Pending;
};

/**
 * Scene capture into a per-capture render target, completed after a render fence and read back once (on demand,
 * not per frame; the readback is a bounded stall and is not advertised as hitch-free). Unavailable with NullRHI.
 */
UCLASS()
class DOCPHOTOGRAPHYRUNTIME_API UDocSceneCapturePhotoSource : public UDocPhotoImageSource
{
	GENERATED_BODY()

public:
	virtual FDocSystemResult BeginCapture(UWorld* World, const FDocPhotoCaptureRequest& Request, int64 Ticket, UDocPhotographySubsystem* Sink) override;
	virtual void Poll() override;
	virtual void CancelCapture(int64 Ticket) override;
	virtual void BeginDestroy() override;

	/** True when real rendering is possible in this process. */
	static bool IsRenderingAvailable();

private:
	struct FPendingCapture
	{
		int64 Ticket = 0;
		TWeakObjectPtr<AActor> CaptureActor;
		TWeakObjectPtr<UTextureRenderTarget2D> Target;
		TWeakObjectPtr<UDocPhotographySubsystem> Sink;
		TSharedPtr<FRenderCommandFence> Fence;
		double StartTime = 0.0;
	};

	void Release(FPendingCapture& Capture);
	TArray<FPendingCapture> Pending;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;
};

/** Controlled blob storage. Keys are relative, validated identifiers; never arbitrary client paths. */
UCLASS(Abstract)
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoBlobStore : public UObject
{
	GENERATED_BODY()

public:
	virtual bool WriteBlob(const FString& Key, const TArray64<uint8>& Bytes, FString& OutError) { OutError = TEXT("Abstract store"); return false; }
	virtual bool ReadBlob(const FString& Key, TArray64<uint8>& OutBytes) const { return false; }
	virtual bool BlobExists(const FString& Key) const { return false; }
	virtual bool DeleteBlob(const FString& Key) { return false; }

	/** Letters, digits, '-', '_', '.', and '/' separators; no '..', no leading '/', no drive letters, <= 200 chars. */
	static bool IsValidKey(const FString& Key);
};

/** In-memory store (sessions, tests). Can simulate write failures. */
UCLASS()
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoMemoryBlobStore : public UDocPhotoBlobStore
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bFailWrites = false;

	virtual bool WriteBlob(const FString& Key, const TArray64<uint8>& Bytes, FString& OutError) override;
	virtual bool ReadBlob(const FString& Key, TArray64<uint8>& OutBytes) const override;
	virtual bool BlobExists(const FString& Key) const override;
	virtual bool DeleteBlob(const FString& Key) override;

	int32 GetBlobCount() const { return Blobs.Num(); }

private:
	TMap<FString, TArray64<uint8>> Blobs;
};

/** File store under a controlled root (Saved/DocPhotography by default). Writes a temp file, then moves it into place. */
UCLASS()
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoFileBlobStore : public UDocPhotoBlobStore
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FString RootDirectory;

	virtual bool WriteBlob(const FString& Key, const TArray64<uint8>& Bytes, FString& OutError) override;
	virtual bool ReadBlob(const FString& Key, TArray64<uint8>& OutBytes) const override;
	virtual bool BlobExists(const FString& Key) const override;
	virtual bool DeleteBlob(const FString& Key) override;

private:
	FString ResolveRoot() const;
	bool ResolvePath(const FString& Key, FString& OutPath) const;
};
