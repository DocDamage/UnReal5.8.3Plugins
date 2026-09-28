#include "DocOpticalBeamsLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDocOpticalBeams);

class FDocOpticalBeamsRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDocOpticalBeams, Log, TEXT("DocOpticalBeamsRuntime module started"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDocOpticalBeams, Log, TEXT("DocOpticalBeamsRuntime module stopped"));
	}
};

IMPLEMENT_MODULE(FDocOpticalBeamsRuntimeModule, DocOpticalBeamsRuntime)
