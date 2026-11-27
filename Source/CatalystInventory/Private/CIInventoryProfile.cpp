#include "CIInventoryProfile.h"

TOptional<FCFMagritteBinding> CIInventoryProfile::DecodeUser(const FString& Json, const FString& UserId)
{
    if (UserId.IsEmpty())
    {
        return {};
    }
    try
    {
        const auto Utf8 = StringCast<UTF8CHAR>(*Json, Json.Len());
        magritte::Limits Limits;
        Limits.bytes = 65536;
        auto Graph = std::make_shared<magritte::Graph>(
            magritte::Graph::decode(std::string(reinterpret_cast<const char*>(Utf8.Get()), Utf8.Length()), Limits));
        const auto User = FCFMagritteBinding::Find(Graph, TCHAR_TO_UTF8(*UserId));
        if (!User || Graph->root != "user" || User->Describe().id != Graph->root)
        {
            return {};
        }
        const auto Profile = User->Follow("profile");
        return Profile && Profile->Follow("inventory") ? User : TOptional<FCFMagritteBinding>();
    }
    catch (const magritte::Error&)
    {
        return {};
    }
}
