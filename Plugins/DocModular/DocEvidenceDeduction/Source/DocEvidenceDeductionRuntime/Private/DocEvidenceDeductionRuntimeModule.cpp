#include "Modules/ModuleManager.h"
#include "DocEvidenceDeductionLog.h"

DEFINE_LOG_CATEGORY(LogDocEvidenceDeduction);

class FDocEvidenceDeductionRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDocEvidenceDeduction, Log, TEXT("DocEvidenceDeductionRuntime module started"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDocEvidenceDeduction, Log, TEXT("DocEvidenceDeductionRuntime module shutdown"));
	}
};

IMPLEMENT_MODULE(FDocEvidenceDeductionRuntimeModule, DocEvidenceDeductionRuntime);
