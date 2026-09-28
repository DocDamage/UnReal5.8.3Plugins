#include "DocCoreTags.h"

namespace DocCoreTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error, "Doc.Error", "Root for DocModular error categories.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Unset, "Doc.Error.Unset", "A result was never assigned an outcome. Always a failure.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Unsupported, "Doc.Error.Unsupported", "Capability not available (for example, its bridge is not installed).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_NotReady, "Doc.Error.NotReady", "Service or content exists but is not ready yet.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_NotFound, "Doc.Error.NotFound", "Requested definition, object, or record does not exist.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_InvalidConfiguration, "Doc.Error.InvalidConfiguration", "User-authored configuration is invalid.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_PermissionDenied, "Doc.Error.PermissionDenied", "Caller lacks authority or authorization.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Cancelled, "Doc.Error.Cancelled", "Operation was cancelled.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_TimedOut, "Doc.Error.TimedOut", "Operation exceeded its configured timeout.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_ExecutionFailed, "Doc.Error.ExecutionFailed", "Operation ran and failed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_InvalidInput, "Doc.Error.InvalidInput", "Request values are invalid (not an authored-configuration error).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Unavailable, "Doc.Error.Unavailable", "An optional provider or dependency is absent or unavailable at runtime.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Conflict, "Doc.Error.Conflict", "Stale revision, concurrent modification, or conflicting reservation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Storage, "Doc.Error.Storage", "Persistence/storage failure. Use with Outcome Failed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Handle_Invalid, "Doc.Error.Handle.Invalid", "Handle was never issued (zero/default).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Handle_Stale, "Doc.Error.Handle.Stale", "Handle was released, or came from a previous table epoch/session.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Handle_WrongScope, "Doc.Error.Handle.WrongScope", "Handle belongs to a different world or owner scope.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Identity_Invalid, "Doc.Error.Identity.Invalid", "Persistent identity is missing or malformed.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control, "Doc.Control", "Root for local-player control capabilities.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Camera, "Doc.Control.Camera", "View target / camera ownership.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Input, "Doc.Control.Input", "All gameplay input.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Input_Movement, "Doc.Control.Input.Movement", "Movement input only.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Input_Look, "Doc.Control.Input.Look", "Look input only.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Pause, "Doc.Control.Pause", "Game pause request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_HUD, "Doc.Control.HUD", "HUD visibility request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Cursor, "Doc.Control.Cursor", "Mouse cursor visibility request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Control_Focus, "Doc.Control.Focus", "UI/input focus ownership.");
}
