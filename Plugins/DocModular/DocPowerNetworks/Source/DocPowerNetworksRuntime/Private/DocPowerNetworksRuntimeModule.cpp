#include "Modules/ModuleManager.h"
#include "DocPowerNetworksLog.h"

DEFINE_LOG_CATEGORY(LogDocPowerNetworks);

class FDocPowerNetworksRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDocPowerNetworks, Log, TEXT("DocPowerNetworksRuntime module started"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDocPowerNetworks, Log, TEXT("DocPowerNetworksRuntime module shutdown"));
	}
};

IMPLEMENT_MODULE(FDocPowerNetworksRuntimeModule, DocPowerNetworksRuntime);
