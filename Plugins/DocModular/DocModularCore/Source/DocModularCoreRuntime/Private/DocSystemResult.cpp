#include "DocSystemResult.h"
#include "DocCoreTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSystemResult)

// EDocResultOutcome is append-only (DECISIONS D-004, D-012). Pin numeric values so a
// reorder that would change saved/replicated values fails to compile.
static_assert(static_cast<uint8>(EDocResultOutcome::Unset) == 0);
static_assert(static_cast<uint8>(EDocResultOutcome::Succeeded) == 1);
static_assert(static_cast<uint8>(EDocResultOutcome::Unsupported) == 2);
static_assert(static_cast<uint8>(EDocResultOutcome::NotReady) == 3);
static_assert(static_cast<uint8>(EDocResultOutcome::NotFound) == 4);
static_assert(static_cast<uint8>(EDocResultOutcome::InvalidConfiguration) == 5);
static_assert(static_cast<uint8>(EDocResultOutcome::PermissionDenied) == 6);
static_assert(static_cast<uint8>(EDocResultOutcome::Cancelled) == 7);
static_assert(static_cast<uint8>(EDocResultOutcome::TimedOut) == 8);
static_assert(static_cast<uint8>(EDocResultOutcome::Failed) == 9);
static_assert(static_cast<uint8>(EDocResultOutcome::NoChange) == 10);
static_assert(static_cast<uint8>(EDocResultOutcome::InvalidInput) == 11);
static_assert(static_cast<uint8>(EDocResultOutcome::Unavailable) == 12);
static_assert(static_cast<uint8>(EDocResultOutcome::Conflict) == 13);

FDocSystemResult FDocSystemResult::MakeSuccess(int64 InOperationId)
{
	FDocSystemResult Result;
	Result.Outcome = EDocResultOutcome::Succeeded;
	Result.OperationId = InOperationId;
	return Result;
}

FDocSystemResult FDocSystemResult::MakeNoChange(FString InDiagnostic, int64 InOperationId)
{
	FDocSystemResult Result;
	Result.Outcome = EDocResultOutcome::NoChange;
	Result.Diagnostic = MoveTemp(InDiagnostic);
	Result.OperationId = InOperationId;
	return Result;
}

FDocSystemResult FDocSystemResult::MakeFailure(
	EDocResultOutcome InOutcome,
	FString InDiagnostic,
	FGameplayTag InErrorTag,
	FText InUserMessage,
	int64 InOperationId)
{
	if (!ensureMsgf(!IsSuccessOutcome(InOutcome) && InOutcome != EDocResultOutcome::Unset,
		TEXT("FDocSystemResult::MakeFailure called with a non-failure outcome; converting to Failed.")))
	{
		InOutcome = EDocResultOutcome::Failed;
	}

	FDocSystemResult Result;
	Result.Outcome = InOutcome;
	Result.ErrorTag = InErrorTag.IsValid() ? InErrorTag : GetDefaultErrorTag(InOutcome);
	Result.Diagnostic = MoveTemp(InDiagnostic);
	Result.UserMessage = MoveTemp(InUserMessage);
	Result.OperationId = InOperationId;
	return Result;
}

FGameplayTag FDocSystemResult::GetDefaultErrorTag(EDocResultOutcome InOutcome)
{
	switch (InOutcome)
	{
	case EDocResultOutcome::Succeeded:				return FGameplayTag();
	case EDocResultOutcome::NoChange:				return FGameplayTag();
	case EDocResultOutcome::Unset:					return DocCoreTags::Error_Unset;
	case EDocResultOutcome::Unsupported:			return DocCoreTags::Error_Unsupported;
	case EDocResultOutcome::NotReady:				return DocCoreTags::Error_NotReady;
	case EDocResultOutcome::NotFound:				return DocCoreTags::Error_NotFound;
	case EDocResultOutcome::InvalidConfiguration:	return DocCoreTags::Error_InvalidConfiguration;
	case EDocResultOutcome::PermissionDenied:		return DocCoreTags::Error_PermissionDenied;
	case EDocResultOutcome::Cancelled:				return DocCoreTags::Error_Cancelled;
	case EDocResultOutcome::TimedOut:				return DocCoreTags::Error_TimedOut;
	case EDocResultOutcome::Failed:					return DocCoreTags::Error_ExecutionFailed;
	case EDocResultOutcome::InvalidInput:			return DocCoreTags::Error_InvalidInput;
	case EDocResultOutcome::Unavailable:			return DocCoreTags::Error_Unavailable;
	case EDocResultOutcome::Conflict:				return DocCoreTags::Error_Conflict;
	default:										return DocCoreTags::Error_ExecutionFailed;
	}
}

FString FDocSystemResult::ToString() const
{
	const UEnum* OutcomeEnum = StaticEnum<EDocResultOutcome>();
	const FString OutcomeName = OutcomeEnum ? OutcomeEnum->GetNameStringByValue(static_cast<int64>(Outcome)) : TEXT("?");
	return FString::Printf(TEXT("[%s] Op=%lld Tag=%s %s"),
		*OutcomeName,
		OperationId,
		*ErrorTag.ToString(),
		*Diagnostic);
}
