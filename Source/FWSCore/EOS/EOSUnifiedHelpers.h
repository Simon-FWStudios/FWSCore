// EOSUnifiedHelpers.h — Drop-in replacement (2025-09-14)
#pragma once

#include "CoreMinimal.h"
// (Optional, but safe) pull basic EOS types if not already present in translation units.
#include <eos_sdk.h>
#include <eos_userinfo.h>
#include <eos_presence.h>
#include <eos_lobby.h>

//
// Lightweight helpers for UTF8 <-> TCHAR and common EOS ID conversions,
// plus tiny RAII guards for EOS-owned handles that require *_Release.
//
namespace EOSUnified
{
	// ----- String helpers -----
	inline const char* ToUtf8(const FString& S) { return TCHAR_TO_UTF8(*S); }
	inline FString     FromUtf8(const char* S)  { return S ? UTF8_TO_TCHAR(S) : TEXT(""); }

	// ----- EpicAccountId <-> string -----
	inline EOS_EpicAccountId EpicIdFromString(const FString& In)
	{
		if (In.IsEmpty()) return nullptr;
		return EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*In));
	}

	inline FString EpicIdToString(EOS_EpicAccountId Id)
	{
		if (!Id) return TEXT("");
		char Buffer[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = {};
		int32 Len = static_cast<int32>(sizeof(Buffer));
		const EOS_EResult Rc = EOS_EpicAccountId_ToString(Id, Buffer, &Len);
		return (Rc == EOS_EResult::EOS_Success) ? UTF8_TO_TCHAR(Buffer) : TEXT("");
	}

	// ----- ProductUserId -> string (safe) -----
	inline FString ProductUserIdToString_Safe(EOS_ProductUserId Puid)
	{
		if (!Puid) return TEXT("");
		char Buf[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
		int32 Len = static_cast<int32>(sizeof(Buf));
		const EOS_EResult Rc = EOS_ProductUserId_ToString(Puid, Buf, &Len);
		return (Rc == EOS_EResult::EOS_Success) ? UTF8_TO_TCHAR(Buf) : TEXT("");
	}

	// ----- Friend/Presence readable text -----
	inline FString FriendStatusToString(int32 Status)
	{
		switch (Status)
		{
		case 0:  return TEXT("Unknown");
		case 1:  return TEXT("Not Friends");
		case 2:  return TEXT("Invite Sent");
		case 3:  return TEXT("Invite Received");
		case 4:  return TEXT("Friends");
		case 5:  return TEXT("Blocked");
		default: return FString::Printf(TEXT("Status(%d)"), Status);
		}
	}

	inline FString PresenceStatusToString(int32 Presence)
	{
		switch (Presence)
		{
		case 0:  return TEXT("Offline");
		case 1:  return TEXT("Online");
		case 2:  return TEXT("Away");
		case 3:  return TEXT("Do Not Disturb");
		case 4:  return TEXT("Joinable");
		case 5:  return TEXT("In Lobby");
		case 6:  return TEXT("In Match");
		default: return FString::Printf(TEXT("Presence(%d)"), Presence);
		}
	}

	// ----- RAII wrappers for EOS objects requiring release -----
	struct FUserInfoPtr
	{
		EOS_UserInfo* Ptr = nullptr;
		~FUserInfoPtr() { if (Ptr) { EOS_UserInfo_Release(Ptr); Ptr = nullptr; } }
	};

	struct FLobbyDetails
	{
		EOS_HLobbyDetails Handle = nullptr;
		~FLobbyDetails() { if (Handle) { EOS_LobbyDetails_Release(Handle); Handle = nullptr; } }
	};

	struct FDetailsInfo
	{
		EOS_LobbyDetails_Info* Ptr = nullptr;
		~FDetailsInfo() { if (Ptr) { EOS_LobbyDetails_Info_Release(Ptr); Ptr = nullptr; } }
	};

	struct FLobbySearch
	{
		EOS_HLobbySearch Handle = nullptr;
		~FLobbySearch() { if (Handle) { EOS_LobbySearch_Release(Handle); Handle = nullptr; } }
	};
}
