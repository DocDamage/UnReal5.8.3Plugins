#include "Modules/ModuleManager.h"
#include "DocAcousticSpacesLog.h"

DEFINE_LOG_CATEGORY(LogDocAcousticSpaces);

class FDocAcousticSpacesRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FDocAcousticSpacesRuntimeModule, DocAcousticSpacesRuntime)
