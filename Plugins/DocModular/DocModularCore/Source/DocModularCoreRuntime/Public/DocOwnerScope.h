#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "DocOwnerScope.generated.h"

/** What kind of subject owns a record (expansion handoff 2.3). */
UENUM(BlueprintType)
enum class EDocOwnerScopeKind : uint8
{
	/** Not set. Never valid for a mutation. */
	None,
	/** A saved local or authenticated player profile. */
	PlayerProfile,
	/** State shared by everyone in one world/campaign. */
	SharedWorld,
	/** A party/group. */
	Party,
	/** Transient session-only state; never persisted. */
	Session
};

/**
 * Durable owner of feature state (quests, inventory, codex, unlocks, discovery...).
 *
 * - SubjectId is a stable, saved identifier of the subject (a local profile GUID,
 *   a party GUID...). It is never a player index, controller ID, or array slot.
 * - CampaignNamespace separates independent campaigns/worlds that share a profile.
 *
 * Authorization: a scope received from a client is a claim, not proof. Features
 * resolve the caller's allowed scope from trusted host context.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocOwnerScope
{
	GENERATED_BODY()

	FDocOwnerScope() = default;
	FDocOwnerScope(EDocOwnerScopeKind InKind, const FGuid& InSubjectId, const FGuid& InCampaignNamespace = FGuid())
		: Kind(InKind), SubjectId(InSubjectId), CampaignNamespace(InCampaignNamespace)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Owner")
	EDocOwnerScopeKind Kind = EDocOwnerScopeKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Owner")
	FGuid SubjectId;

	/** Optional campaign/world namespace. Zero = not namespaced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Owner")
	FGuid CampaignNamespace;

	bool IsValid() const { return Kind != EDocOwnerScopeKind::None && SubjectId.IsValid(); }

	/** Session scopes are never written to durable storage. */
	bool IsPersistable() const { return IsValid() && Kind != EDocOwnerScopeKind::Session; }

	FString ToString() const;

	friend bool operator==(const FDocOwnerScope& A, const FDocOwnerScope& B)
	{
		return A.Kind == B.Kind && A.SubjectId == B.SubjectId && A.CampaignNamespace == B.CampaignNamespace;
	}
	friend bool operator!=(const FDocOwnerScope& A, const FDocOwnerScope& B) { return !(A == B); }
	friend uint32 GetTypeHash(const FDocOwnerScope& S)
	{
		return HashCombine(HashCombine(::GetTypeHash(static_cast<uint8>(S.Kind)), GetTypeHash(S.SubjectId)), GetTypeHash(S.CampaignNamespace));
	}
	friend FArchive& operator<<(FArchive& Ar, FDocOwnerScope& S)
	{
		uint8 KindValue = static_cast<uint8>(S.Kind);
		Ar << KindValue;
		S.Kind = static_cast<EDocOwnerScopeKind>(KindValue);
		Ar << S.SubjectId;
		Ar << S.CampaignNamespace;
		return Ar;
	}
};
