#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "GameplayTagContainer.h"
#include "DocGameplayContext.generated.h"

class UWorld;
class AActor;
class ULocalPlayer;

/** Authority of the code path that built a context. */
UENUM(BlueprintType)
enum class EDocNetAuthority : uint8
{
	/** No world, or authority could not be determined. Treat as "no authority". */
	Unknown,
	/** Standalone game (no networking). Has authority. */
	Standalone,
	/** Server / listen server, or the instigator actor has authority. */
	Authority,
	/** Client without authority over the instigator. */
	Remote
};

/**
 * Explicit context passed into DocModular operations.
 *
 * Ownership: holds only weak references and owns nothing. A context is a value
 * describing a request; it must not be cached across worlds or map travel.
 * World is required for any world-scoped operation. There is no implicit GWorld or
 * player-index-0 fallback anywhere in DocModular.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocGameplayContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Doc|Context")
	TWeakObjectPtr<UWorld> World;

	/** Actor on whose behalf the request is made (player pawn, AI, scripted actor...). Optional. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Context")
	TWeakObjectPtr<AActor> Instigator;

	/** Actor the request is about. Optional. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Context")
	TWeakObjectPtr<AActor> Target;

	/** Local player for presentation/per-player operations. Optional; never inferred. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Context")
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;

	UPROPERTY(BlueprintReadWrite, Category = "Doc|Context")
	FGameplayTagContainer ContextTags;

	/** Optional correlation ID for diagnostics (0 = none). */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Context")
	int64 CorrelationId = 0;

	/** Build a context from an explicit world. Instigator/Target may be null. */
	static FDocGameplayContext Make(UWorld* InWorld, AActor* InInstigator = nullptr, AActor* InTarget = nullptr);

	/**
	 * True when World is alive and every set actor reference is alive and belongs to
	 * that same world. Use to reject cross-world or stale contexts.
	 */
	bool IsValidForWorld(const UWorld* ExpectedWorld) const;

	/** Authority of this context, from the world's net mode and the instigator's role. */
	EDocNetAuthority ResolveAuthority() const;

	/** True for Standalone or Authority. */
	bool HasAuthority() const;
};
