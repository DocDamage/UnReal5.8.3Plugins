#pragma once

#include "CoreMinimal.h"
#include "DocSaveTypes.h"

/**
 * Envelope codec (handoff 13.3, 13.7). Pure functions, safe on worker threads:
 * they operate on byte arrays only and never touch UObjects.
 *
 * File layout (little-endian):
 *   uint32 Magic, int32 FormatVersion, int32 HeaderByteCount, <header fields>,
 *   <payload bytes (compressed or raw) of PayloadByteCount>
 * Integrity = CRC32 of the uncompressed body. Checksums detect corruption; they do
 * not prove authenticity.
 */
struct DOCSAVERUNTIME_API FDocSaveEnvelope
{
	struct FLimits
	{
		int64 MaxFileBytes = 256LL * 1024 * 1024;
		int64 MaxUncompressedBytes = 512LL * 1024 * 1024;
	};

	/** Build file bytes from an already-encoded body. */
	static bool Write(FDocSaveHeader Header, const TArray<uint8>& Body, bool bCompress, TArray<uint8>& OutFile, FString& OutError);

	/** Validate bounds/integrity and return the header and decoded body bytes. Never allocates beyond the limits. */
	static bool Read(const TArray<uint8>& File, const FLimits& Limits, FDocSaveHeader& OutHeader, TArray<uint8>& OutBody, FString& OutError);

	/** Read only the header (for revision checks). */
	static bool ReadHeader(const TArray<uint8>& File, FDocSaveHeader& OutHeader, FString& OutError);
};

/** Storage backend (handoff 13.5). Implementations must be callable from a worker thread. */
class DOCSAVERUNTIME_API IDocSaveStorageBackend
{
public:
	virtual ~IDocSaveStorageBackend() = default;

	/** Commit bytes for Slot. Must not leave a partially written file as the slot's current save. */
	virtual bool WriteSlot(const FString& Slot, const TArray<uint8>& Bytes, FString& OutError) = 0;
	virtual bool ReadSlot(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) = 0;
	virtual bool SlotExists(const FString& Slot) const = 0;
	/** Last-known-good copy kept by WriteSlot, if any. */
	virtual bool ReadBackup(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) = 0;
	virtual bool DeleteSlot(const FString& Slot, FString& OutError) = 0;
	/** True when WriteSlot replaces the file atomically on this platform (advertised only when implemented). */
	virtual bool SupportsAtomicReplace() const = 0;
};

/**
 * Local file backend: <Saved>/<Folder>/<Slot>.docsav
 * WriteSlot: write <Slot>.tmp → read back and compare → move current to <Slot>.bak →
 * move tmp to <Slot>.docsav. A failure before the final move leaves the previous
 * save intact; the .bak keeps the last-known-good copy.
 */
class DOCSAVERUNTIME_API FDocLocalFileSaveBackend final : public IDocSaveStorageBackend
{
public:
	explicit FDocLocalFileSaveBackend(const FString& InDirectory);

	virtual bool WriteSlot(const FString& Slot, const TArray<uint8>& Bytes, FString& OutError) override;
	virtual bool ReadSlot(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) override;
	virtual bool SlotExists(const FString& Slot) const override;
	virtual bool ReadBackup(const FString& Slot, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) override;
	virtual bool DeleteSlot(const FString& Slot, FString& OutError) override;
	virtual bool SupportsAtomicReplace() const override { return false; } // rename-based; not fault-tested as atomic

	/** Slot names: 1..64 chars of [A-Za-z0-9_-]. Rejects paths and traversal. */
	static bool IsValidSlotName(const FString& Slot);

	/** Test hook: fail after writing the temp file (simulates a crash before commit). */
	bool bFailBeforeCommitForTesting = false;

	FString GetDirectory() const { return Directory; }

private:
	FString PathFor(const FString& Slot, const TCHAR* Extension) const;
	bool ReadFileBounded(const FString& Path, TArray<uint8>& OutBytes, int64 MaxBytes, FString& OutError) const;

	FString Directory;
};
