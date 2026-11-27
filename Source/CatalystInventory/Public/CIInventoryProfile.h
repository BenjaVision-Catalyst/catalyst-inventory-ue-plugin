#pragma once

#include "CCConnectTypes.h"
#include "Presentation/CFMagritteBinding.h"

class UCCSubsystem;

namespace CIInventoryProfile
{
/** Decode an inventory graph for the identity established by the caller's session. */
CATALYSTINVENTORY_API TOptional<FCFMagritteBinding> DecodeUser(const FString& Json, const FString& UserId);
/** Decode a successful operation 103 reply, including the bounded ZLIB envelope. */
CATALYSTINVENTORY_API TOptional<FCFMagritteBinding> DecodeReply(TConstArrayView<uint8> Body, const FString& UserId);

/** The host supplies the identity and generation of its already authenticated connection. */
CATALYSTINVENTORY_API FCCConnectRequestResult RequestUser(UCCSubsystem& Connect, FName Connection, UObject& Owner,
    UWorld* World, const FString& VerifiedUserId, uint64 AuthenticatedGeneration,
    TFunction<void(const FCCConnectReply&, TOptional<FCFMagritteBinding>)> OnReply);
}
