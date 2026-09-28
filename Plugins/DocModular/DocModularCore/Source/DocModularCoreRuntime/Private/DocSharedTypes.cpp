#include "DocSharedTypes.h"
#include "DocOwnerScope.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Class.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSharedTypes)

FString FDocOwnerScope::ToString() const
{
	const UEnum* KindEnum = StaticEnum<EDocOwnerScopeKind>();
	return FString::Printf(TEXT("%s:%s:%s"),
		KindEnum ? *KindEnum->GetNameStringByValue(static_cast<int64>(Kind)) : TEXT("?"),
		*SubjectId.ToString(EGuidFormats::Digits),
		*CampaignNamespace.ToString(EGuidFormats::Digits));
}

FDocConditionResult FDocConditionResult::CombineAll(const TArray<FDocConditionResult>& Results)
{
	const FDocConditionResult* FirstUnavailable = nullptr;
	const FDocConditionResult* FirstUnsatisfied = nullptr;
	int64 MaxRevision = 0;
	for (const FDocConditionResult& R : Results)
	{
		MaxRevision = FMath::Max(MaxRevision, R.EvaluatedRevision);
		if (R.State == EDocConditionState::Unavailable && !FirstUnavailable)
		{
			FirstUnavailable = &R;
		}
		else if (R.State == EDocConditionState::Unsatisfied && !FirstUnsatisfied)
		{
			FirstUnsatisfied = &R;
		}
	}
	if (FirstUnavailable)
	{
		return *FirstUnavailable;
	}
	if (FirstUnsatisfied)
	{
		return *FirstUnsatisfied;
	}
	return Satisfied(MaxRevision);
}

FDocConditionResult FDocConditionResult::CombineAny(const TArray<FDocConditionResult>& Results)
{
	const FDocConditionResult* FirstUnavailable = nullptr;
	const FDocConditionResult* FirstUnsatisfied = nullptr;
	for (const FDocConditionResult& R : Results)
	{
		if (R.State == EDocConditionState::Satisfied)
		{
			return R;
		}
		if (R.State == EDocConditionState::Unsatisfied && !FirstUnsatisfied)
		{
			FirstUnsatisfied = &R;
		}
		else if (R.State == EDocConditionState::Unavailable && !FirstUnavailable)
		{
			FirstUnavailable = &R;
		}
	}
	if (FirstUnsatisfied)
	{
		return *FirstUnsatisfied;
	}
	if (FirstUnavailable)
	{
		return *FirstUnavailable;
	}
	// Empty OR: nothing can satisfy it.
	FDocConditionResult Empty;
	Empty.State = EDocConditionState::Unsatisfied;
	Empty.Diagnostic = TEXT("Empty OR condition");
	return Empty;
}

namespace DocCoreSerialization
{
	// Envelope: magic, CRC32 of body, body size, body (tagged struct serialization).
	static constexpr uint32 EnvelopeMagic = 0x44435331; // "DCS1"

	void EncodeStruct(const UScriptStruct* Struct, const void* Value, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		if (!Struct || !Value)
		{
			return;
		}

		TArray<uint8> Body;
		{
			FMemoryWriter Writer(Body, /*bIsPersistent*/ true);
			FObjectAndNameAsStringProxyArchive Ar(Writer, /*bLoadIfFindFails*/ false);
			const_cast<UScriptStruct*>(Struct)->SerializeItem(Ar, const_cast<void*>(Value), nullptr);
		}

		uint32 Magic = EnvelopeMagic;
		uint32 Crc = FCrc::MemCrc32(Body.GetData(), Body.Num());
		int32 Size = Body.Num();
		FMemoryWriter Header(OutBytes, true);
		Header << Magic;
		Header << Crc;
		Header << Size;
		OutBytes.Append(Body);
	}

	bool DecodeStruct(const UScriptStruct* Struct, void* Value, const TArray<uint8>& Bytes)
	{
		if (!Struct || !Value)
		{
			return false;
		}
		Struct->ClearScriptStruct(Value);

		constexpr int32 HeaderSize = sizeof(uint32) * 2 + sizeof(int32);
		if (Bytes.Num() < HeaderSize)
		{
			return false;
		}

		uint32 Magic = 0, Crc = 0;
		int32 Size = 0;
		{
			FMemoryReader Header(Bytes, true);
			Header << Magic;
			Header << Crc;
			Header << Size;
		}
		if (Magic != EnvelopeMagic || Size < 0 || Size != Bytes.Num() - HeaderSize)
		{
			return false;
		}
		if (FCrc::MemCrc32(Bytes.GetData() + HeaderSize, Size) != Crc)
		{
			return false;
		}

		// Bound any single array/string allocation to the body size: untrusted input cannot request more.
		class FBoundedReader : public FMemoryReader
		{
		public:
			FBoundedReader(const TArray<uint8>& In, int64 Max) : FMemoryReader(In, true) { ArMaxSerializeSize = Max; }
		};
		TArray<uint8> Body(Bytes.GetData() + HeaderSize, Size);
		FBoundedReader Reader(Body, FMath::Max<int64>(Size, 1));
		FObjectAndNameAsStringProxyArchive Ar(Reader, false);
		const_cast<UScriptStruct*>(Struct)->SerializeItem(Ar, Value, nullptr);
		if (Ar.IsError() || Reader.IsError())
		{
			Struct->ClearScriptStruct(Value);
			return false;
		}
		return true;
	}
}
