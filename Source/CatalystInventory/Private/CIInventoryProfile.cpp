#include "CIInventoryProfile.h"

#include "Misc/Compression.h"
#include "SubSystem/CCSubsystem.h"

namespace
{
constexpr uint16 ProfileOperation = 103;
constexpr int32 MaximumReplyBytes = 4080;
constexpr int32 MaximumGraphBytes = 65536;
constexpr int32 MaximumPreviewGraphBytes = 128 * 1024;
constexpr int32 ZlibHeaderBytes = 8;

TOptional<FCFMagritteBinding> DecodeUserUtf8(const std::string& Json, const FString& UserId, int32 Limit = MaximumGraphBytes,
    std::shared_ptr<const magritte::Graph>* OutGraph = nullptr)
{
    if (UserId.IsEmpty())
    {
        return {};
    }
    try
    {
        magritte::Limits Limits;
        Limits.bytes = Limit;
        auto Graph = std::make_shared<magritte::Graph>(magritte::Graph::decode(Json, Limits));
        const auto User = FCFMagritteBinding::Find(Graph, TCHAR_TO_UTF8(*UserId));
        if (!User || Graph->root != "user" || User->Describe().id != Graph->root)
        {
            return {};
        }
        const auto Profile = User->Follow("profile");
        if (!Profile || !Profile->Follow("inventory"))
        {
            return {};
        }
        if (OutGraph)
        {
            *OutGraph = MoveTemp(Graph);
        }
        return User;
    }
    catch (const magritte::Error&)
    {
        return {};
    }
}
}

TOptional<FCFMagritteBinding> CIInventoryProfile::DecodeUser(const FString& Json, const FString& UserId,
    std::shared_ptr<const magritte::Graph>* OutGraph)
{
    const auto Utf8 = StringCast<UTF8CHAR>(*Json, Json.Len());
    return DecodeUserUtf8(std::string(reinterpret_cast<const char*>(Utf8.Get()), Utf8.Length()), UserId, MaximumPreviewGraphBytes, OutGraph);
}

TOptional<FCFMagritteBinding> CIInventoryProfile::DecodeReply(TConstArrayView<uint8> Body, const FString& UserId)
{
    if (Body.IsEmpty() || Body.Num() > MaximumReplyBytes)
    {
        return {};
    }

    if (Body.Num() < 4 || FMemory::Memcmp(Body.GetData(), "ZLIB", 4) != 0)
    {
        return DecodeUserUtf8(std::string(reinterpret_cast<const char*>(Body.GetData()), Body.Num()), UserId);
    }
    if (Body.Num() <= ZlibHeaderBytes)
    {
        return {};
    }

    const uint32 UncompressedSize = uint32(Body[4]) | (uint32(Body[5]) << 8)
        | (uint32(Body[6]) << 16) | (uint32(Body[7]) << 24);
    if (UncompressedSize == 0 || UncompressedSize > MaximumGraphBytes)
    {
        return {};
    }

    std::string Json(UncompressedSize, '\0');
    if (!FCompression::UncompressMemory(NAME_Zlib, Json.data(), UncompressedSize,
        Body.GetData() + ZlibHeaderBytes, Body.Num() - ZlibHeaderBytes))
    {
        return {};
    }
    return DecodeUserUtf8(Json, UserId);
}

FCCConnectRequestResult CIInventoryProfile::RequestUser(UCCSubsystem& Connect, FName Connection, UObject& Owner,
    UWorld* World, const FString& VerifiedUserId, uint64 AuthenticatedGeneration,
    TFunction<void(const FCCConnectReply&, TOptional<FCFMagritteBinding>)> OnReply)
{
    if (VerifiedUserId.IsEmpty() || AuthenticatedGeneration == 0 || !OnReply)
    {
        return {ECCConnectResult::InvalidArgument};
    }

    return Connect.TryRequest(Connection, &Owner, World, ProfileOperation, {},
        FCCConnectReplyCallback::CreateLambda(
            [VerifiedUserId, AuthenticatedGeneration, OnReply = MoveTemp(OnReply)](const FCCConnectReply& Reply)
            {
                TOptional<FCFMagritteBinding> User;
                if (Reply.Status == 0 && Reply.Operation == ProfileOperation && Reply.Generation == AuthenticatedGeneration)
                {
                    User = DecodeReply(Reply.Body, VerifiedUserId);
                }
                OnReply(Reply, MoveTemp(User));
            }), 10.0, AuthenticatedGeneration);
}
