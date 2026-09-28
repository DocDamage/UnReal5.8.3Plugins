// CORE-06: every DocModularCore public header must compile when included first,
// from a module outside the plugin, without private-header access.
// Each block below includes exactly one public header in its own translation-unit
// position; a missing include inside a Core header surfaces here (and in the
// plugin's non-unity build).

#include "DocModularCoreRuntime.h"
#include "DocCoreLog.h"
#include "DocCoreTags.h"
#include "DocSystemResult.h"
#include "DocGameplayContext.h"
#include "DocPersistentObjectId.h"
#include "DocRequestHandle.h"
#include "DocCoreBlueprintLibrary.h"
#include "Interfaces/DocPersistentIdentity.h"
#include "Interfaces/DocGameplayTagProvider.h"
#include "Interfaces/DocWorldStateProvider.h"
#include "Interfaces/DocPlayerControlProvider.h"
#include "DocOwnerScope.h"
#include "DocSharedTypes.h"
#include "DocEffectKey.h"
#include "DocControlClaimArbiter.h"
#include "DocReferencePlayerControlProvider.h"
#include "DocPlayerControlSubsystem.h"

namespace DocCppConsumer::Private
{
	// Touch representative symbols so the linker must resolve exported Core API.
	bool ConsumerUsesCoreApi()
	{
		const FDocSystemResult Result = FDocSystemResult::MakeSuccess();
		const FGuid Scope = FDocPersistentObjectId::ComposeInstanceScope(FGuid(), FGuid(1, 2, 3, 4));
		const FDocPersistentObjectId Id(FGuid(5, 6, 7, 8), Scope, FGuid(9, 10, 11, 12));
		TDocHandleTable<int32> Table;
		const FDocOwnerScope Owner(EDocOwnerScopeKind::PlayerProfile, FGuid(1, 1, 1, 1));
		const FDocConditionResult Condition = FDocConditionResult::CombineAll({});
		FDocReceiptLedger Ledger;
		return Result.IsSuccess() && Id.IsValid() && Table.Num() == 0 && Owner.IsPersistable() && Condition.IsSatisfied()
			&& FDocReceiptLedger::HashString(TEXT("x")) != 0;
	}
}
