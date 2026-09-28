#include "DocSaveStorage.h"
#include "DocSaveLog.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------

namespace DocSaveEnvelopePrivate
{
	void SerializeHeaderFields(FArchive& Ar, FDocSaveHeader& H)
	{
		Ar << H.SuiteVersion;
		Ar << H.ProducerGameVersion;
		Ar << H.WorldNamespace;
		Ar << H.SaveRevision;
		int64 Ticks = H.Timestamp.GetTicks();
		Ar << Ticks;
		if (Ar.IsLoading()) { H.Timestamp = FDateTime(Ticks); }
		Ar << H.CompressionMode;
		Ar << H.PayloadByteCount;
		Ar << H.UncompressedByteCount;
		Ar << H.IntegrityAlgorithm;
		Ar << H.IntegrityValue;
		uint8 CategoryValue = static_cast<uint8>(H.Category);
		Ar << CategoryValue;
		if (Ar.IsLoading()) { H.Category = static_cast<EDocSaveSlotCategory>(CategoryValue); }
	}

	/** Memory reader that bounds per-item allocations (strings/arrays) read from untrusted bytes. */
	class FBoundedMemoryReader : public FMemoryReader
	{
	public:
		FBoundedMemoryReader(const TArray<uint8>& InBytes, int64 MaxItemBytes)
			: FMemoryReader(InBytes, true)
		{
			ArMaxSerializeSize = MaxItemBytes;
		}
	};

	constexpr int32 PrefixBytes = sizeof(uint32) + sizeof(int32) + sizeof(int32);
	constexpr int32 MaxHeaderBytes = 64 * 1024;
	constexpr int32 MaxStringChars = 1024;
}

bool FDocSaveEnvelope::Write(FDocSaveHeader Header, const TArray<uint8>& Body, bool bCompress, TArray<uint8>& OutFile, FString& OutError)
{
	using namespace DocSaveEnvelopePrivate;
	OutFile.Reset();

	Header.FormatVersion = FDocSaveHeader::CurrentFormatVersion;
	Header.UncompressedByteCount = Body.Num();
	Header.IntegrityAlgorithm = TEXT("CRC32");
	Header.IntegrityValue = static_cast<int64>(FCrc::MemCrc32(Body.GetData(), Body.Num()));

	TArray<uint8> Payload;
	Header.CompressionMode = 0;
	if (bCompress && Body.Num() > 0)
	{
		const int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Body.Num());
		Payload.SetNumUninitialized(Bound);
		int32 CompressedSize = Bound;
		if (FCompression::CompressMemory(NAME_Zlib, Payload.GetData(), CompressedSize, Body.GetData(), Body.Num()))
		{
			Payload.SetNum(CompressedSize);
			Header.CompressionMode = 1;
		}
		else
		{
			Payload = Body;
		}
	}
	else
	{
		Payload = Body;
	}
	Header.PayloadByteCount = Payload.Num();

	TArray<uint8> HeaderBytes;
	{
		FMemoryWriter HW(HeaderBytes, true);
		SerializeHeaderFields(HW, Header);
	}
	if (HeaderBytes.Num() > MaxHeaderBytes)
	{
		OutError = TEXT("Header too large");
		return false;
	}

	FMemoryWriter W(OutFile, true);
	uint32 Magic = FDocSaveHeader::MagicValue;
	int32 Format = Header.FormatVersion;
	int32 HeaderSize = HeaderBytes.Num();
	W << Magic;
	W << Format;
	W << HeaderSize;
	W.Serialize(HeaderBytes.GetData(), HeaderBytes.Num());
	W.Serialize(Payload.GetData(), Payload.Num());
	return true;
}

bool FDocSaveEnvelope::ReadHeader(const TArray<uint8>& File, FDocSaveHeader& OutHeader, FString& OutError)
{
	using namespace DocSaveEnvelopePrivate;
	if (File.Num() < PrefixBytes)
	{
		OutError = TEXT("File truncated (no prefix)");
		return false;
	}
	FMemoryReader R(File, true);
	uint32 Magic = 0;
	int32 Format = 0, HeaderSize = 0;
	R << Magic;
	R << Format;
	R << HeaderSize;
	if (Magic != FDocSaveHeader::MagicValue)
	{
		OutError = TEXT("Not a DocSave file (bad magic)");
		return false;
	}
	if (Format > FDocSaveHeader::CurrentFormatVersion || Format < 1)
	{
		OutError = FString::Printf(TEXT("Unsupported format version %d (this build supports up to %d)"), Format, FDocSaveHeader::CurrentFormatVersion);
		return false;
	}
	if (HeaderSize <= 0 || HeaderSize > MaxHeaderBytes || PrefixBytes + int64(HeaderSize) > File.Num())
	{
		OutError = TEXT("Header size out of bounds");
		return false;
	}
	TArray<uint8> HeaderBytes(File.GetData() + PrefixBytes, HeaderSize);
	FBoundedMemoryReader HR(HeaderBytes, MaxStringChars * sizeof(TCHAR)); // bounds string allocations
	SerializeHeaderFields(HR, OutHeader);
	if (HR.IsError())
	{
		OutError = TEXT("Header corrupt");
		return false;
	}
	OutHeader.FormatVersion = Format;
	return true;
}

bool FDocSaveEnvelope::Read(const TArray<uint8>& File, const FLimits& Limits, FDocSaveHeader& OutHeader, TArray<uint8>& OutBody, FString& OutError)
{
	using namespace DocSaveEnvelopePrivate;
	OutBody.Reset();
	if (File.Num() > Limits.MaxFileBytes)
	{
		OutError = TEXT("File exceeds MaxFileBytes");
		return false;
	}
	if (!ReadHeader(File, OutHeader, OutError))
	{
		return false;
	}
	int32 HeaderSize = 0;
	{
		FMemoryReader R(File, true);
		uint32 Magic; int32 Format;
		R << Magic; R << Format; R << HeaderSize;
	}
	const int64 PayloadOffset = PrefixBytes + int64(HeaderSize);
	if (OutHeader.PayloadByteCount < 0 || PayloadOffset + OutHeader.PayloadByteCount != File.Num())
	{
		OutError = TEXT("Payload size does not match file length (truncated or padded)");
		return false;
	}
	if (OutHeader.UncompressedByteCount < 0 || OutHeader.UncompressedByteCount > Limits.MaxUncompressedBytes)
	{
		OutError = TEXT("Declared uncompressed size exceeds limit");
		return false;
	}

	const uint8* PayloadData = File.GetData() + PayloadOffset;
	if (OutHeader.CompressionMode == 1)
	{
		OutBody.SetNumUninitialized(static_cast<int32>(OutHeader.UncompressedByteCount));
		if (!FCompression::UncompressMemory(NAME_Zlib, OutBody.GetData(), OutHeader.UncompressedByteCount, PayloadData, OutHeader.PayloadByteCount))
		{
			OutBody.Reset();
			OutError = TEXT("Decompression failed");
			return false;
		}
	}
	else if (OutHeader.CompressionMode == 0)
	{
		if (OutHeader.PayloadByteCount != OutHeader.UncompressedByteCount)
		{
			OutError = TEXT("Uncompressed payload size mismatch");
			return false;
		}
		OutBody.Append(PayloadData, static_cast<int32>(OutHeader.PayloadByteCount));
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown compression mode %d"), OutHeader.CompressionMode);
		return false;
	}

	if (OutHeader.IntegrityAlgorithm != TEXT("CRC32")
		|| static_cast<int64>(FCrc::MemCrc32(OutBody.GetData(), OutBody.Num())) != OutHeader.IntegrityValue)
	{
		OutBody.Reset();
		OutError = TEXT("Integrity check failed (corrupt data)");
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Local file backend
// ---------------------------------------------------------------------------

FDocLocalFileSaveBackend::FDocLocalFileSaveBackend(const FString& InDirectory)
	: Directory(InDirectory)
{
}

bool FDocLocalFileSaveBackend::IsValidSlotName(const FString& Slot)
{
	if (Slot.Len() < 1 || Slot.Len() > 64)
	{
		return false;
	}
	for (const TCHAR C : Slot)
	{
		const bool bOk = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-';
		if (!bOk)
		{
			return false;
		}
	}
	return true;
}

FString FDocLocalFileSaveBackend::PathFor(const FString& Slot, const TCHAR* Extension) const
{
	return FPaths::Combine(Directory, Slot + Extension);
}

bool FDocLocalFileSaveBackend::ReadFileBounded(const FString& Path, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) const
{
	IFileManager& FM = IFileManager::Get();
	const int64 Size = FM.FileSize(*Path);
	if (Size < 0)
	{
		OutError = TEXT("Slot not found");
		return false;
	}
	if (Size > MaxBytes)
	{
		OutError = TEXT("Save file larger than the configured maximum");
		return false;
	}
	if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
	{
		OutError = TEXT("Read failed");
		return false;
	}
	return true;
}

bool FDocLocalFileSaveBackend::WriteSlot(const FString& Slot, const TArray<uint8>& Bytes, FString& OutError)
{
	if (!IsValidSlotName(Slot))
	{
		OutError = TEXT("Invalid slot name");
		return false;
	}
	IFileManager& FM = IFileManager::Get();
	FM.MakeDirectory(*Directory, true);
	const FString Final = PathFor(Slot, TEXT(".docsav"));
	const FString Temp = PathFor(Slot, TEXT(".tmp"));
	const FString Backup = PathFor(Slot, TEXT(".bak"));

	if (!FFileHelper::SaveArrayToFile(Bytes, *Temp))
	{
		OutError = TEXT("Temp write failed");
		return false;
	}
	// Verify what hit the disk before it can become the current save.
	TArray<uint8> Check;
	if (!FFileHelper::LoadFileToArray(Check, *Temp) || Check != Bytes)
	{
		FM.Delete(*Temp, false, true, true);
		OutError = TEXT("Temp verification failed");
		return false;
	}
	if (bFailBeforeCommitForTesting)
	{
		OutError = TEXT("Simulated failure before commit");
		return false; // temp file left behind, current save untouched
	}
	if (FM.FileExists(*Final))
	{
		if (!FM.Move(*Backup, *Final, /*Replace*/ true, true, false, true))
		{
			OutError = TEXT("Could not rotate the previous save to backup");
			return false;
		}
	}
	if (!FM.Move(*Final, *Temp, true, true, false, true))
	{
		// Put the last-known-good back.
		FM.Copy(*Final, *Backup, true, true);
		OutError = TEXT("Commit move failed; previous save restored from backup");
		return false;
	}
	return true;
}

bool FDocLocalFileSaveBackend::ReadSlot(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError)
{
	if (!IsValidSlotName(Slot))
	{
		OutError = TEXT("Invalid slot name");
		return false;
	}
	return ReadFileBounded(PathFor(Slot, TEXT(".docsav")), OutBytes, MaxBytes, OutError);
}

bool FDocLocalFileSaveBackend::SlotExists(const FString& Slot) const
{
	return IsValidSlotName(Slot) && IFileManager::Get().FileExists(*PathFor(Slot, TEXT(".docsav")));
}

bool FDocLocalFileSaveBackend::ReadBackup(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError)
{
	if (!IsValidSlotName(Slot))
	{
		OutError = TEXT("Invalid slot name");
		return false;
	}
	return ReadFileBounded(PathFor(Slot, TEXT(".bak")), OutBytes, MaxBytes, OutError);
}

bool FDocLocalFileSaveBackend::DeleteSlot(const FString& Slot, FString& OutError)
{
	if (!IsValidSlotName(Slot))
	{
		OutError = TEXT("Invalid slot name");
		return false;
	}
	IFileManager& FM = IFileManager::Get();
	bool bOk = true;
	for (const TCHAR* Ext : { TEXT(".docsav"), TEXT(".bak"), TEXT(".tmp") })
	{
		const FString Path = PathFor(Slot, Ext);
		if (FM.FileExists(*Path))
		{
			bOk &= FM.Delete(*Path, false, true, true);
		}
	}
	if (!bOk)
	{
		OutError = TEXT("Delete failed");
	}
	return bOk;
}
