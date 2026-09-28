#include "DocModularCoreRuntime.h"
#include "DocCoreLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDocCore);

void FDocModularCoreRuntimeModule::StartupModule()
{
	UE_LOG(LogDocCore, Verbose, TEXT("DocModularCoreRuntime started."));
}

void FDocModularCoreRuntimeModule::ShutdownModule()
{
	UE_LOG(LogDocCore, Verbose, TEXT("DocModularCoreRuntime shut down."));
}

IMPLEMENT_MODULE(FDocModularCoreRuntimeModule, DocModularCoreRuntime)
