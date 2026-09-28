#include "DocInteractionTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionTypes)

namespace DocInteractionTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Interaction, "Interaction", "Root of interaction intent tags.");
	UE_DEFINE_GAMEPLAY_TAG(Use, "Interaction.Use");
	UE_DEFINE_GAMEPLAY_TAG(Activate, "Interaction.Activate");
	UE_DEFINE_GAMEPLAY_TAG(Deactivate, "Interaction.Deactivate");
	UE_DEFINE_GAMEPLAY_TAG(Toggle, "Interaction.Toggle");
	UE_DEFINE_GAMEPLAY_TAG(Open, "Interaction.Open");
	UE_DEFINE_GAMEPLAY_TAG(Close, "Interaction.Close");
	UE_DEFINE_GAMEPLAY_TAG(Pickup, "Interaction.Pickup");
	UE_DEFINE_GAMEPLAY_TAG(Drop, "Interaction.Drop");
	UE_DEFINE_GAMEPLAY_TAG(Push, "Interaction.Push");
	UE_DEFINE_GAMEPLAY_TAG(Pull, "Interaction.Pull");
	UE_DEFINE_GAMEPLAY_TAG(Rotate, "Interaction.Rotate");
	UE_DEFINE_GAMEPLAY_TAG(Read, "Interaction.Read");
	UE_DEFINE_GAMEPLAY_TAG(Inspect, "Interaction.Inspect");
	UE_DEFINE_GAMEPLAY_TAG(Enter, "Interaction.Enter");
	UE_DEFINE_GAMEPLAY_TAG(Exit, "Interaction.Exit");
	UE_DEFINE_GAMEPLAY_TAG(Sit, "Interaction.Sit");
	UE_DEFINE_GAMEPLAY_TAG(Talk, "Interaction.Talk");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Purchase, "Interaction.Purchase", "Intent only; no economy is implemented.");
	UE_DEFINE_GAMEPLAY_TAG(Hold, "Interaction.Hold");
	UE_DEFINE_GAMEPLAY_TAG(Repair, "Interaction.Repair");
	UE_DEFINE_GAMEPLAY_TAG(Unlock, "Interaction.Unlock");
	UE_DEFINE_GAMEPLAY_TAG(Custom, "Interaction.Custom");

	UE_DEFINE_GAMEPLAY_TAG(Error_OutOfRange, "Doc.Error.Interaction.OutOfRange");
	UE_DEFINE_GAMEPLAY_TAG(Error_NotFacing, "Doc.Error.Interaction.NotFacing");
	UE_DEFINE_GAMEPLAY_TAG(Error_MissingTags, "Doc.Error.Interaction.MissingTags");
	UE_DEFINE_GAMEPLAY_TAG(Error_BlockedTags, "Doc.Error.Interaction.BlockedTags");
	UE_DEFINE_GAMEPLAY_TAG(Error_Cooldown, "Doc.Error.Interaction.Cooldown");
	UE_DEFINE_GAMEPLAY_TAG(Error_AlreadyUsed, "Doc.Error.Interaction.AlreadyUsed");
	UE_DEFINE_GAMEPLAY_TAG(Error_Reserved, "Doc.Error.Interaction.Reserved");
	UE_DEFINE_GAMEPLAY_TAG(Error_Disabled, "Doc.Error.Interaction.Disabled");
	UE_DEFINE_GAMEPLAY_TAG(Error_TargetLost, "Doc.Error.Interaction.TargetLost");
	UE_DEFINE_GAMEPLAY_TAG(Error_NoAuthority, "Doc.Error.Interaction.NoAuthority");
	UE_DEFINE_GAMEPLAY_TAG(Error_MissingInterface, "Doc.Error.Interaction.MissingInterface");
	UE_DEFINE_GAMEPLAY_TAG(Error_NoLineOfSight, "Doc.Error.Interaction.NoLineOfSight");
	UE_DEFINE_GAMEPLAY_TAG(Error_ActionFailed, "Doc.Error.Interaction.ActionFailed");

	UE_DEFINE_GAMEPLAY_TAG(Phase_Started, "Interaction.Phase.Started");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Completed, "Interaction.Phase.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Cancelled, "Interaction.Phase.Cancelled");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Repeated, "Interaction.Phase.Repeated");
}
