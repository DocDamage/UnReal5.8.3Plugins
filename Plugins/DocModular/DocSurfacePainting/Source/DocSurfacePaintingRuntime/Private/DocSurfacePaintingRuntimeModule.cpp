#include "Modules/ModuleManager.h"
#include "DocSurfacePaintingLog.h"

DEFINE_LOG_CATEGORY(LogDocSurfacePainting);

class FDocSurfacePaintingRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FDocSurfacePaintingRuntimeModule, DocSurfacePaintingRuntime)
