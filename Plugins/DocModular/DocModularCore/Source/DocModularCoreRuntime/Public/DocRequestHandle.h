#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "DocRequestHandle.generated.h"

/**
 * Opaque, transient handle to a long-lived request (a lease, session, subscription...).
 *
 * - OperationId is unique within the process for its lifetime (never reused).
 * - Epoch identifies the issuing TDocHandleTable instance/reset; a handle from a
 *   destroyed or reset table is rejected as stale.
 * - Handles are NOT durable. Both fields are Transient, so persistent (save/asset)
 *   serialization drops them and a restored handle is invalid. Use
 *   FDocPersistentObjectId for anything that must survive a session.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocRequestHandle
{
	GENERATED_BODY()

	FDocRequestHandle() = default;
	FDocRequestHandle(int64 InOperationId, int32 InEpoch)
		: OperationId(InOperationId), Epoch(InEpoch)
	{
	}

	/** True if this handle was issued at some point. Does not mean it is still active. */
	bool IsSet() const { return OperationId > 0 && Epoch > 0; }

	int64 GetOperationId() const { return OperationId; }
	int32 GetEpoch() const { return Epoch; }

	/** Reset to the unset state. */
	void Invalidate() { OperationId = 0; Epoch = 0; }

	FString ToString() const { return FString::Printf(TEXT("Op%lld@E%d"), OperationId, Epoch); }

	friend bool operator==(const FDocRequestHandle& A, const FDocRequestHandle& B)
	{
		return A.OperationId == B.OperationId && A.Epoch == B.Epoch;
	}
	friend bool operator!=(const FDocRequestHandle& A, const FDocRequestHandle& B) { return !(A == B); }
	friend uint32 GetTypeHash(const FDocRequestHandle& H)
	{
		return HashCombine(::GetTypeHash(H.OperationId), ::GetTypeHash(H.Epoch));
	}

private:
	friend struct FDocHandleAllocator;

	UPROPERTY(Transient)
	int64 OperationId = 0;

	UPROPERTY(Transient)
	int32 Epoch = 0;
};

/** Result of validating a handle against a table and scope. */
UENUM(BlueprintType)
enum class EDocHandleStatus : uint8
{
	/** Default/zero handle; never issued. */
	Invalid,
	/** Issued by this table, for this scope, and not yet released. */
	Active,
	/** Released, or issued by a different table epoch (destroyed/reset owner, old session). */
	Stale,
	/** Issued by this table but for a different scope (e.g. a different world). */
	WrongScope
};

/** Process-wide allocation of operation IDs and table epochs. Thread-safe. */
struct DOCMODULARCORERUNTIME_API FDocHandleAllocator
{
	/** Monotonic, never 0, never reused within the process. */
	static int64 NextOperationId();
	/** Monotonic, never 0, never reused within the process. */
	static int32 NextEpoch();
	static FDocRequestHandle MakeHandle(int64 OperationId, int32 Epoch) { return FDocRequestHandle(OperationId, Epoch); }
};

/**
 * Native registry of active requests keyed by handle, with scope isolation.
 *
 * Intended to be owned by a subsystem (one per world / local player). Game-thread
 * only; it holds no UObject strong references (scopes are FObjectKey).
 *
 * Contract:
 * - Add() issues a new handle bound to Scope (typically the owning UWorld).
 * - Find()/Validate() reject default, released, other-epoch and other-scope handles.
 * - Remove() is idempotent: the first call returns true, later calls return false
 *   and do nothing. Removing a handle never affects any other entry.
 * - Reset() drops every entry and moves to a new epoch; all outstanding handles
 *   become Stale. Callers must release owned resources before Reset().
 */
template <typename PayloadType>
class TDocHandleTable
{
public:
	TDocHandleTable()
		: Epoch(FDocHandleAllocator::NextEpoch())
	{
	}

	TDocHandleTable(const TDocHandleTable&) = delete;
	TDocHandleTable& operator=(const TDocHandleTable&) = delete;

	FDocRequestHandle Add(const UObject* Scope, PayloadType Payload)
	{
		const int64 Id = FDocHandleAllocator::NextOperationId();
		Entries.Add(Id, FEntry{ FObjectKey(Scope), MoveTemp(Payload) });
		return FDocHandleAllocator::MakeHandle(Id, Epoch);
	}

	EDocHandleStatus Validate(const FDocRequestHandle& Handle, const UObject* Scope) const
	{
		if (!Handle.IsSet())
		{
			return EDocHandleStatus::Invalid;
		}
		if (Handle.GetEpoch() != Epoch)
		{
			return EDocHandleStatus::Stale;
		}
		const FEntry* Entry = Entries.Find(Handle.GetOperationId());
		if (!Entry)
		{
			return EDocHandleStatus::Stale;
		}
		if (Entry->Scope != FObjectKey(Scope))
		{
			return EDocHandleStatus::WrongScope;
		}
		return EDocHandleStatus::Active;
	}

	PayloadType* Find(const FDocRequestHandle& Handle, const UObject* Scope)
	{
		return Validate(Handle, Scope) == EDocHandleStatus::Active ? &Entries[Handle.GetOperationId()].Payload : nullptr;
	}

	const PayloadType* Find(const FDocRequestHandle& Handle, const UObject* Scope) const
	{
		return Validate(Handle, Scope) == EDocHandleStatus::Active ? &Entries[Handle.GetOperationId()].Payload : nullptr;
	}

	/** Idempotent release. Optionally moves the payload out so the caller can clean it up. */
	bool Remove(const FDocRequestHandle& Handle, const UObject* Scope, PayloadType* OutRemoved = nullptr)
	{
		if (Validate(Handle, Scope) != EDocHandleStatus::Active)
		{
			return false;
		}
		FEntry Removed = Entries.FindAndRemoveChecked(Handle.GetOperationId());
		if (OutRemoved)
		{
			*OutRemoved = MoveTemp(Removed.Payload);
		}
		return true;
	}

	/** Remove all entries bound to Scope (e.g. on world teardown). Returns count removed. */
	int32 RemoveAllForScope(const UObject* Scope)
	{
		const FObjectKey Key(Scope);
		int32 Removed = 0;
		for (auto It = Entries.CreateIterator(); It; ++It)
		{
			if (It.Value().Scope == Key)
			{
				It.RemoveCurrent();
				++Removed;
			}
		}
		return Removed;
	}

	void Reset()
	{
		Entries.Reset();
		Epoch = FDocHandleAllocator::NextEpoch();
	}

	int32 Num() const { return Entries.Num(); }
	int32 GetEpoch() const { return Epoch; }

	/** Remove every entry for which Pred(Handle, const Payload&) returns true. Returns count removed. */
	template <typename PredicateType>
	int32 RemoveIf(PredicateType&& Pred)
	{
		int32 Removed = 0;
		for (auto It = Entries.CreateIterator(); It; ++It)
		{
			if (Pred(FDocHandleAllocator::MakeHandle(It.Key(), Epoch), static_cast<const PayloadType&>(It.Value().Payload)))
			{
				It.RemoveCurrent();
				++Removed;
			}
		}
		return Removed;
	}

	/** Visit active entries with mutable payload access. The visitor must not add or remove entries. */
	template <typename FuncType>
	void ForEachMutable(FuncType&& Func)
	{
		for (TPair<int64, FEntry>& Pair : Entries)
		{
			Func(FDocHandleAllocator::MakeHandle(Pair.Key, Epoch), Pair.Value.Payload);
		}
	}

	/** Visit active entries. The visitor must not add or remove entries. */
	template <typename FuncType>
	void ForEach(FuncType&& Func) const
	{
		for (const TPair<int64, FEntry>& Pair : Entries)
		{
			Func(FDocHandleAllocator::MakeHandle(Pair.Key, Epoch), Pair.Value.Payload);
		}
	}

private:
	struct FEntry
	{
		FObjectKey Scope;
		PayloadType Payload;
	};

	TMap<int64, FEntry> Entries;
	int32 Epoch = 0;
};
