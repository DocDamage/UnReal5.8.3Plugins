#include "DocInventoryTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInventoryTypes)

void UDocItemDefinition::FindProblems(TArray<FString>& OutErrors) const
{
	if (ItemId.IsNone()) { OutErrors.Add(TEXT("ItemId is required")); }
	if (MaxStackSize < 1) { OutErrors.Add(FString::Printf(TEXT("Item %s: MaxStackSize must be >= 1"), *ItemId.ToString())); }
	if (WeightUnits < 0) { OutErrors.Add(FString::Printf(TEXT("Item %s: negative weight"), *ItemId.ToString())); }
	if (MaxDurability > 0 && MaxStackSize > 1 && !StackPolicy.bRequireEqualDurability)
	{
		OutErrors.Add(FString::Printf(TEXT("Item %s: mixed-durability stacks need an aggregate model (not supported)"), *ItemId.ToString()));
	}
	if (bOccupiesAllEquipSlots && EquipTags.IsEmpty()) { OutErrors.Add(FString::Printf(TEXT("Item %s: multi-slot item without EquipTags"), *ItemId.ToString())); }
}
