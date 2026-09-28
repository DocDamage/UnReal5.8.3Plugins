#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "DocOwnerScope.h"
#include "DocSystemResult.h"
#include "DocEffectKey.generated.h"

/**
 * Identity of one externally visible effect (expansion handoff 2.7):
 *
 *     EffectKey = (OwnerScope, CampaignEpoch, ProducerInstanceId, TransitionOrdinal, ActionId)
 *
 * A producer (quest, dialogue session...) derives the key from its own committed
 * state so that a retry of the same transition produces the same key. A consumer
 * (inventory, codex, unlocks...) stores a receipt keyed by it and returns the
 * original result for a repeated key with the same payload.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocEffectKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Effect")
	FDocOwnerScope Owner;

	/** Changes when a campaign restarts, so a new playthrough never matches old receipts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Effect")
	FGuid CampaignEpoch;

	/** Stable ID of the producing instance (quest instance, dialogue session...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Effect")
	FGuid ProducerInstanceId;

	/** Monotonic transition number inside the producer instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Effect")
	int64 TransitionOrdinal = 0;

	/** Stable authored ID of the action within the transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Effect")
	FName ActionId;

	bool IsValid() const { return ProducerInstanceId.IsValid() && !ActionId.IsNone(); }

	FString ToString() const;

	friend bool operator==(const FDocEffectKey& A, const FDocEffectKey& B)
	{
		return A.Owner == B.Owner && A.CampaignEpoch == B.CampaignEpoch && A.ProducerInstanceId == B.ProducerInstanceId
			&& A.TransitionOrdinal == B.TransitionOrdinal && A.ActionId == B.ActionId;
	}
	friend bool operator!=(const FDocEffectKey& A, const FDocEffectKey& B) { return !(A == B); }
	friend uint32 GetTypeHash(const FDocEffectKey& K)
	{
		uint32 H = GetTypeHash(K.Owner);
		H = HashCombine(H, GetTypeHash(K.CampaignEpoch));
		H = HashCombine(H, GetTypeHash(K.ProducerInstanceId));
		H = HashCombine(H, ::GetTypeHash(K.TransitionOrdinal));
		H = HashCombine(H, GetTypeHash(K.ActionId));
		return H;
	}
};

/** Consumer's durable record that an effect was applied. */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocEffectReceipt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Effect")
	FDocEffectKey Key;

	/** Hash of the request payload; the same key with a different payload is a Conflict. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Effect")
	int64 PayloadHash = 0;

	/** Outcome recorded at commit time and returned to every retry. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Effect")
	FDocSystemResult Result;

	/** Consumer's state revision at commit. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Effect")
	int64 CommittedRevision = 0;
};

UENUM(BlueprintType)
enum class EDocReceiptCheck : uint8
{
	/** Never seen: apply the effect and Record() a receipt in the same commit. */
	New,
	/** Same key and payload: do not apply again; return the stored receipt. */
	Duplicate,
	/** Same key, different payload: reject with Conflict. */
	Conflict
};

/**
 * In-memory receipt store used by consumers. Persist its contents with the
 * consumer's own state (GetAll / RestoreAll) so a retry after a crash still finds
 * the receipt. Game-thread only.
 */
class DOCMODULARCORERUNTIME_API FDocReceiptLedger
{
public:
	/** 0 = unbounded. When bounded, the oldest receipts are evicted first. */
	explicit FDocReceiptLedger(int32 InMaxReceipts = 0) : MaxReceipts(InMaxReceipts) {}

	EDocReceiptCheck Check(const FDocEffectKey& Key, int64 PayloadHash, FDocEffectReceipt* OutExisting = nullptr) const;

	/** Record a committed effect. Overwrites nothing: a second Record for an existing key is ignored and returns false. */
	bool Record(const FDocEffectReceipt& Receipt);

	const FDocEffectReceipt* Find(const FDocEffectKey& Key) const { return Receipts.Find(Key); }
	int32 Num() const { return Receipts.Num(); }
	void Reset() { Receipts.Reset(); Order.Reset(); }

	TArray<FDocEffectReceipt> GetAll() const;
	void RestoreAll(const TArray<FDocEffectReceipt>& InReceipts);

	/** Stable payload hash helper (CRC32 of bytes, widened). */
	static int64 HashBytes(const TArray<uint8>& Bytes);
	static int64 HashString(const FString& Text);

private:
	TMap<FDocEffectKey, FDocEffectReceipt> Receipts;
	TArray<FDocEffectKey> Order;
	int32 MaxReceipts = 0;
};
