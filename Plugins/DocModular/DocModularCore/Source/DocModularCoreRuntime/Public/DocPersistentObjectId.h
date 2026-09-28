#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "UObject/WeakObjectPtr.h"
#include "DocPersistentObjectId.generated.h"

/**
 * Durable identity of a world object (handoff 4.3):
 *
 *     PersistentObjectId = (WorldNamespace, InstanceScope, LocalObjectGuid)
 *
 * - WorldNamespace: identifies the authored world / save namespace. Required.
 * - InstanceScope: identifies which placement of a level/template the object
 *   belongs to. The zero GUID is the world root scope (object placed directly in
 *   the world). Nested placements are built with ComposeInstanceScope(), so two
 *   placements of the same level instance yield different scopes even though the
 *   authored LocalObjectGuid inside them is identical.
 * - LocalObjectGuid: authored (serialized with the actor) or assigned once at
 *   runtime spawn. Required.
 *
 * Never derive any part of this from transforms, labels, names, cell indices,
 * load/spawn order, pointers, or NetGUIDs.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocPersistentObjectId
{
	GENERATED_BODY()

	FDocPersistentObjectId() = default;
	FDocPersistentObjectId(const FGuid& InWorldNamespace, const FGuid& InInstanceScope, const FGuid& InLocalObjectGuid)
		: WorldNamespace(InWorldNamespace), InstanceScope(InInstanceScope), LocalObjectGuid(InLocalObjectGuid)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Identity")
	FGuid WorldNamespace;

	/** Zero = world root scope. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Identity")
	FGuid InstanceScope;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Identity")
	FGuid LocalObjectGuid;

	/** Namespace and local GUID are required. The root instance scope (zero) is valid. */
	bool IsValid() const { return WorldNamespace.IsValid() && LocalObjectGuid.IsValid(); }

	bool IsInRootScope() const { return !InstanceScope.IsValid(); }

	/**
	 * Deterministically derive the scope of a placement nested inside ParentScope.
	 *
	 * Algorithm (version 1, part of the save format; never change in place):
	 *   bytes  = ASCII "DocModular.InstanceScope.v1"
	 *          + ParentScope.A,B,C,D as little-endian uint32
	 *          + PlacementGuid.A,B,C,D as little-endian uint32
	 *   digest = SHA-1(bytes)
	 *   result = digest[0..15] read as four little-endian uint32 (A,B,C,D)
	 *   if result is all zero, D = 1 (zero is reserved for the root scope)
	 *
	 * PlacementGuid must be a stable, serialized GUID of the placement (for example
	 * the level-instance actor's own authored GUID). Returns an invalid (zero) GUID
	 * when PlacementGuid is invalid. Collisions are not assumed impossible: callers
	 * that register IDs (Save, validators) must still detect duplicates.
	 */
	static FGuid ComposeInstanceScope(const FGuid& ParentScope, const FGuid& PlacementGuid);

	/** "Namespace:Scope:Local" using 32-digit GUID text. Stable; used by Parse(). */
	FString ToString() const;

	/** Parse the ToString() form. Returns false (and leaves Out unchanged) on any malformed input. */
	static bool Parse(FStringView Text, FDocPersistentObjectId& Out);

	/** Binary serialization of the three GUIDs in fixed order. */
	friend FArchive& operator<<(FArchive& Ar, FDocPersistentObjectId& Id)
	{
		Ar << Id.WorldNamespace;
		Ar << Id.InstanceScope;
		Ar << Id.LocalObjectGuid;
		return Ar;
	}

	friend bool operator==(const FDocPersistentObjectId& A, const FDocPersistentObjectId& B)
	{
		return A.WorldNamespace == B.WorldNamespace && A.InstanceScope == B.InstanceScope && A.LocalObjectGuid == B.LocalObjectGuid;
	}
	friend bool operator!=(const FDocPersistentObjectId& A, const FDocPersistentObjectId& B) { return !(A == B); }

	friend uint32 GetTypeHash(const FDocPersistentObjectId& Id)
	{
		return HashCombine(HashCombine(GetTypeHash(Id.WorldNamespace), GetTypeHash(Id.InstanceScope)), GetTypeHash(Id.LocalObjectGuid));
	}
};

/**
 * Reference to a world object by persistent identity, with an optional weak
 * runtime cache. Equality and hashing use PersistentId only; the cached pointer is
 * never identity. The cache may be null (object unloaded) without the reference
 * being invalid. Resolving an unloaded object requires a registry owned by a
 * feature (e.g. DocSave); Core never force-loads anything.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocWorldObjectReference
{
	GENERATED_BODY()

	FDocWorldObjectReference() = default;
	explicit FDocWorldObjectReference(const FDocPersistentObjectId& InId, UObject* InResolved = nullptr)
		: PersistentId(InId), CachedObject(InResolved)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Identity")
	FDocPersistentObjectId PersistentId;

	/** Non-owning runtime cache. Transient: never saved. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Doc|Identity")
	TWeakObjectPtr<UObject> CachedObject;

	bool IsValid() const { return PersistentId.IsValid(); }

	/**
	 * Return the cached object only if it is alive and (when it implements
	 * IDocPersistentIdentity) still reports the same persistent ID. Returns null
	 * otherwise; a null result does not mean the object does not exist.
	 */
	UObject* GetResolvedObject() const;

	friend bool operator==(const FDocWorldObjectReference& A, const FDocWorldObjectReference& B) { return A.PersistentId == B.PersistentId; }
	friend bool operator!=(const FDocWorldObjectReference& A, const FDocWorldObjectReference& B) { return !(A == B); }
	friend uint32 GetTypeHash(const FDocWorldObjectReference& R) { return GetTypeHash(R.PersistentId); }
};
