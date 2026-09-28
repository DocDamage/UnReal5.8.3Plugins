#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** Runtime module for DocModularCore. Owns no gameplay state. */
class FDocModularCoreRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
