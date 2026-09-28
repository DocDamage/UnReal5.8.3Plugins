#include "DocRhythmChallengesLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDocRhythm);

class FDocRhythmChallengesRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDocRhythm, Log, TEXT("DocRhythmChallengesRuntime module started"));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDocRhythm, Log, TEXT("DocRhythmChallengesRuntime module stopped"));
	}
};

IMPLEMENT_MODULE(FDocRhythmChallengesRuntimeModule, DocRhythmChallengesRuntime)
