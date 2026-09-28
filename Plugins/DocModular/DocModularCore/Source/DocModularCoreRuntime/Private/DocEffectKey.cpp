#include "DocEffectKey.h"
#include "Misc/Crc.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocEffectKey)

FString FDocEffectKey::ToString() const
{
	return FString::Printf(TEXT("%s|%s|%s|%lld|%s"),
		*Owner.ToString(),
		*CampaignEpoch.ToString(EGuidFormats::Digits),
		*ProducerInstanceId.ToString(EGuidFormats::Digits),
		TransitionOrdinal,
		*ActionId.ToString());
}

EDocReceiptCheck FDocReceiptLedger::Check(const FDocEffectKey& Key, int64 PayloadHash, FDocEffectReceipt* OutExisting) const
{
	const FDocEffectReceipt* Existing = Receipts.Find(Key);
	if (!Existing)
	{
		return EDocReceiptCheck::New;
	}
	if (OutExisting)
	{
		*OutExisting = *Existing;
	}
	return Existing->PayloadHash == PayloadHash ? EDocReceiptCheck::Duplicate : EDocReceiptCheck::Conflict;
}

bool FDocReceiptLedger::Record(const FDocEffectReceipt& Receipt)
{
	if (Receipts.Contains(Receipt.Key))
	{
		return false;
	}
	Receipts.Add(Receipt.Key, Receipt);
	Order.Add(Receipt.Key);

	if (MaxReceipts > 0)
	{
		while (Order.Num() > MaxReceipts)
		{
			Receipts.Remove(Order[0]);
			Order.RemoveAt(0);
		}
	}
	return true;
}

TArray<FDocEffectReceipt> FDocReceiptLedger::GetAll() const
{
	TArray<FDocEffectReceipt> Out;
	Out.Reserve(Order.Num());
	for (const FDocEffectKey& Key : Order)
	{
		if (const FDocEffectReceipt* R = Receipts.Find(Key))
		{
			Out.Add(*R);
		}
	}
	return Out;
}

void FDocReceiptLedger::RestoreAll(const TArray<FDocEffectReceipt>& InReceipts)
{
	Reset();
	for (const FDocEffectReceipt& R : InReceipts)
	{
		Record(R);
	}
}

int64 FDocReceiptLedger::HashBytes(const TArray<uint8>& Bytes)
{
	return static_cast<int64>(FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()));
}

int64 FDocReceiptLedger::HashString(const FString& Text)
{
	return static_cast<int64>(FCrc::StrCrc32(*Text));
}
