// EXP-04 / CORE-06 for features: every public header of every DocModular feature
// plugin must compile when included from a module outside the plugin, without
// private-header access. A missing include inside a feature header surfaces here
// (and in the non-unity build). Generated list; keep in sync when headers are added.

// DocEvents
#include "DocEventsLog.h"
#include "DocEventsSettings.h"
#include "DocEventsTypes.h"
#include "DocGameplayEventSubsystem.h"
#include "DocWaitGameplayEventAction.h"

// DocInteraction
#include "DocInteractionComponents.h"
#include "DocInteractionInputComponent.h"
#include "DocInteractionLog.h"
#include "DocInteractionProviders.h"
#include "DocInteractionRules.h"
#include "DocInteractionSubsystem.h"
#include "DocInteractionTypes.h"

// DocRegions
#include "DocRegionComponents.h"
#include "DocRegionSubsystem.h"
#include "DocRegionTypes.h"
#include "DocRegionsLog.h"

// DocTime
#include "DocTimeLog.h"
#include "DocTimeSubsystem.h"
#include "DocTimeTypes.h"

// DocStreaming
#include "DocStreamingLog.h"
#include "DocStreamingSubsystem.h"
#include "DocStreamingTypes.h"

// DocSave
#include "DocSaveGameSubsystem.h"
#include "DocSaveLog.h"
#include "DocSaveStorage.h"
#include "DocSaveTypes.h"
#include "DocSaveableComponent.h"

// DocAdaptiveAudio
#include "DocAdaptiveAudioLog.h"
#include "DocAdaptiveAudioSubsystem.h"
#include "DocAdaptiveAudioTypes.h"
#include "DocAudioPlaybackBackend.h"

// DocSequences
#include "DocSequencePlaybackBackend.h"
#include "DocSequenceSubsystem.h"
#include "DocSequenceTypes.h"
#include "DocSequencesLog.h"

// DocInspection
#include "DocInspectionLog.h"
#include "DocInspectionSubsystem.h"
#include "DocInspectionTypes.h"

// DocWorldActivation
#include "DocActivationTypes.h"
#include "DocWorldActivationLog.h"
#include "DocWorldActivationSubsystem.h"

// DocSurfaceFeedback
#include "DocSurfaceFeedbackLog.h"
#include "DocSurfaceFeedbackSubsystem.h"
#include "DocSurfaceFeedbackTypes.h"

// DocMapNavigation
#include "DocMapMath.h"
#include "DocMapNavigationLog.h"
#include "DocMapNavigationSubsystem.h"
#include "DocMapTypes.h"

// DocWeather
#include "DocWeatherLog.h"
#include "DocWeatherSubsystem.h"
#include "DocWeatherTypes.h"

// DocNPCSchedules
#include "DocNPCScheduleSubsystem.h"
#include "DocNPCSchedulesLog.h"
#include "DocScheduleTypes.h"

// DocDialogue
#include "DocDialogueLog.h"
#include "DocDialogueSubsystem.h"
#include "DocDialogueTypes.h"

// DocQuestObjectives
#include "DocObjectiveSubsystem.h"
#include "DocQuestObjectivesLog.h"
#include "DocQuestTypes.h"

// DocKnowledgeCodex
#include "DocKnowledgeCodexLog.h"
#include "DocKnowledgeSubsystem.h"
#include "DocKnowledgeTypes.h"

// DocUnlocksProgression
#include "DocUnlockSubsystem.h"
#include "DocUnlockTypes.h"
#include "DocUnlocksProgressionLog.h"

// DocInventoryItems
#include "DocInventoryItemsLog.h"
#include "DocInventoryTypes.h"
#include "DocItemSubsystem.h"

// DocGameFrameworkUI
#include "DocGameFrameworkUILog.h"
#include "DocInputPresentationSubsystem.h"
#include "DocNotificationSubsystem.h"
#include "DocSettingsSubsystem.h"
#include "DocUIManagerSubsystem.h"
#include "DocUIProviders.h"
#include "DocUITypes.h"

namespace DocCppConsumer::Private
{
	// Touch one exported symbol per late plugin so the linker resolves their APIs.
	bool ConsumerUsesFeatureApi()
	{
		const UDocUISettings* UISettings = GetDefault<UDocUISettings>();
		const UDocInventorySettings* InventorySettings = GetDefault<UDocInventorySettings>();
		FDocSettingValue Parsed;
		const bool bParsed = FDocSettingValue::FromString(EDocSettingType::Int, TEXT("3"), Parsed);
		return UISettings && InventorySettings && bParsed && UDocItemSubsystem::StaticClass() && UDocUnlockSubsystem::StaticClass()
			&& UDocKnowledgeSubsystem::StaticClass() && UDocObjectiveSubsystem::StaticClass() && UDocDialogueSubsystem::StaticClass();
	}
}
