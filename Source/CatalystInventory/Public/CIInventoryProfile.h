#pragma once

#include "Presentation/CFMagritteBinding.h"

namespace CIInventoryProfile
{
/** Decode an inventory graph for the identity established by the caller's session. */
CATALYSTINVENTORY_API TOptional<FCFMagritteBinding> DecodeUser(const FString& Json, const FString& UserId);
}
