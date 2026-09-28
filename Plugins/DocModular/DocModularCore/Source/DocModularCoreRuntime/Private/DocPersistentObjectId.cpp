#include "DocPersistentObjectId.h"
#include "Interfaces/DocPersistentIdentity.h"
#include "Misc/SecureHash.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocPersistentObjectId)

namespace DocPersistentObjectId::Private
{
	static constexpr ANSICHAR ScopeDomain[] = "DocModular.InstanceScope.v1";

	static void AppendLE32(TArray<uint8>& Bytes, uint32 Value)
	{
		Bytes.Add(static_cast<uint8>(Value & 0xFF));
		Bytes.Add(static_cast<uint8>((Value >> 8) & 0xFF));
		Bytes.Add(static_cast<uint8>((Value >> 16) & 0xFF));
		Bytes.Add(static_cast<uint8>((Value >> 24) & 0xFF));
	}

	static uint32 ReadLE32(const uint8* Bytes)
	{
		return static_cast<uint32>(Bytes[0])
			| (static_cast<uint32>(Bytes[1]) << 8)
			| (static_cast<uint32>(Bytes[2]) << 16)
			| (static_cast<uint32>(Bytes[3]) << 24);
	}

	static void AppendGuid(TArray<uint8>& Bytes, const FGuid& Guid)
	{
		AppendLE32(Bytes, Guid.A);
		AppendLE32(Bytes, Guid.B);
		AppendLE32(Bytes, Guid.C);
		AppendLE32(Bytes, Guid.D);
	}
}

FGuid FDocPersistentObjectId::ComposeInstanceScope(const FGuid& ParentScope, const FGuid& PlacementGuid)
{
	using namespace DocPersistentObjectId::Private;

	if (!PlacementGuid.IsValid())
	{
		return FGuid();
	}

	TArray<uint8> Bytes;
	Bytes.Reserve(UE_ARRAY_COUNT(ScopeDomain) - 1 + 32);
	Bytes.Append(reinterpret_cast<const uint8*>(ScopeDomain), UE_ARRAY_COUNT(ScopeDomain) - 1);
	AppendGuid(Bytes, ParentScope);
	AppendGuid(Bytes, PlacementGuid);

	uint8 Digest[20];
	FSHA1::HashBuffer(Bytes.GetData(), static_cast<uint64>(Bytes.Num()), Digest);

	FGuid Result(ReadLE32(Digest + 0), ReadLE32(Digest + 4), ReadLE32(Digest + 8), ReadLE32(Digest + 12));
	if (!Result.IsValid())
	{
		Result.D = 1;
	}
	return Result;
}

FString FDocPersistentObjectId::ToString() const
{
	return FString::Printf(TEXT("%s:%s:%s"),
		*WorldNamespace.ToString(EGuidFormats::Digits),
		*InstanceScope.ToString(EGuidFormats::Digits),
		*LocalObjectGuid.ToString(EGuidFormats::Digits));
}

bool FDocPersistentObjectId::Parse(FStringView Text, FDocPersistentObjectId& Out)
{
	// Exactly three 32-hex-digit GUIDs separated by ':'.
	constexpr int32 GuidLen = 32;
	if (Text.Len() != GuidLen * 3 + 2 || Text[GuidLen] != TEXT(':') || Text[GuidLen * 2 + 1] != TEXT(':'))
	{
		return false;
	}

	FGuid Ns, Scope, Local;
	if (!FGuid::ParseExact(Text.Mid(0, GuidLen), EGuidFormats::Digits, Ns)
		|| !FGuid::ParseExact(Text.Mid(GuidLen + 1, GuidLen), EGuidFormats::Digits, Scope)
		|| !FGuid::ParseExact(Text.Mid(GuidLen * 2 + 2, GuidLen), EGuidFormats::Digits, Local))
	{
		return false;
	}

	Out = FDocPersistentObjectId(Ns, Scope, Local);
	return true;
}

UObject* FDocWorldObjectReference::GetResolvedObject() const
{
	UObject* Object = CachedObject.Get();
	if (!Object || !PersistentId.IsValid())
	{
		return nullptr;
	}

	if (Object->Implements<UDocPersistentIdentity>())
	{
		const FDocPersistentObjectId Current = IDocPersistentIdentity::Execute_GetDocPersistentId(Object);
		if (Current != PersistentId)
		{
			return nullptr;
		}
	}
	return Object;
}
