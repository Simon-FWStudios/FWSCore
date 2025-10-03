// EOSUnifiedAuthManager.cpp — Drop-in replacement (2025-09-14)

#include "EOSUnifiedAuthManager.h"
#include "FWSCore.h"
#include "FWSCore.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include <eos_common.h>
#include <eos_auth.h>
#include <eos_connect.h>
#include <eos_userinfo.h>
#include <memory>

#include "EOSUnifiedHelpers.h"

// Optional engine EOS manager adoption (editor/runtime builds may not have it)
#if defined(WITH_EOS_SDK_MANAGER) && WITH_EOS_SDK_MANAGER
#include "IEOSSDKManager.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#endif

// ---------- Auth scopes ----------
static constexpr EOS_EAuthScopeFlags kRequiredAuthScopes =
	  EOS_EAuthScopeFlags::EOS_AS_BasicProfile
	| EOS_EAuthScopeFlags::EOS_AS_FriendsList
	| EOS_EAuthScopeFlags::EOS_AS_Presence;

// ---------- Small UE helpers for token persistence ----------
static FString MakeTokenDir()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EOS"));
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/true);
	return Dir;
}
static FString MakeTokenPath()
{
	return FPaths::Combine(MakeTokenDir(), TEXT("eos_auth_token.txt"));
}

static bool SaveTextFileUE(const FString& Path, const FString& Data)
{
	return FFileHelper::SaveStringToFile(Data, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
}
static bool LoadTextFileUE(const FString& Path, FString& Out)
{
	return FFileHelper::LoadFileToString(Out, *Path);
}
static void DeleteFileIfExistsUE(const FString& Path)
{
	IFileManager::Get().Delete(*Path);
}

// ========================= ctor/dtor =========================

EOSUnifiedAuthManager::EOSUnifiedAuthManager()
{
}

EOSUnifiedAuthManager::~EOSUnifiedAuthManager()
{
}

// ====================== token persistence ====================

void EOSUnifiedAuthManager::SaveRefreshToken()
{
	if (RefreshToken.empty()) return;
	SaveTextFileUE(MakeTokenPath(), UTF8_TO_TCHAR(RefreshToken.c_str()));
}

void EOSUnifiedAuthManager::LoadRefreshToken()
{
	FString T;
	if (LoadTextFileUE(MakeTokenPath(), T))
	{
		RefreshToken = TCHAR_TO_UTF8(*T);
	}
	else
	{
		RefreshToken.clear();
	}
}

void EOSUnifiedAuthManager::DeleteRefreshToken()
{
	DeleteFileIfExistsUE(MakeTokenPath());
	RefreshToken.clear();
}

// ========================= lifecycle =========================

bool EOSUnifiedAuthManager::Initialize()
{
	LoadRefreshToken();

	if (!PlatformHandle)
	{
		if (!CreatePlatform())
		{
			UE_LOG(LogEOSUnifiedAuth, Error, TEXT("[Auth] Failed to create or adopt EOS Platform."));
			if (OnLoginStateChanged) OnLoginStateChanged(false, "Failed to create platform");
			return false;
		}
	}

	return true;
}

bool EOSUnifiedAuthManager::CreatePlatform()
{
	// Try to adopt an engine-owned platform first (Editor/runtime with EOSSDKManager)
#if defined(WITH_EOS_SDK_MANAGER) && WITH_EOS_SDK_MANAGER
	{
		static const FName FeatureName(TEXT("EOSSDKManager"));
		if (IModularFeatures::Get().IsModularFeatureAvailable(FeatureName))
		{
			IEOSSDKManager& Manager = IModularFeatures::Get().GetModularFeature<IEOSSDKManager>(FeatureName);
			if (Manager.IsInitialized())
			{
				for (const auto& PlatformPtr : Manager.GetActivePlatforms())
				{
					if (PlatformPtr.IsValid())
					{
						PlatformHandle   = *PlatformPtr;
						bUsingEngineEOS  = true;
						bCreatedPlatform = false;
						UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Reusing engine EOS platform via EOSSDKManager (%p)"), PlatformHandle);
						return true;
					}
				}
			}
			else
			{
				UE_LOG(LogEOSUnifiedAuth, Verbose, TEXT("[Auth] EOSSDKManager present but not initialized yet."));
			}
		}
	}
#endif

	// Initialize EOS SDK (idempotent; may return AlreadyConfigured if engine did it)
	{
		EOS_InitializeOptions Init{};
		Init.ApiVersion     = EOS_INITIALIZE_API_LATEST;
		Init.ProductName    = "EOSUnifiedSample";
		Init.ProductVersion = "1.0";

		const EOS_EResult Rc = EOS_Initialize(&Init);
		if (Rc != EOS_EResult::EOS_Success && Rc != EOS_EResult::EOS_AlreadyConfigured)
		{
			UE_LOG(LogEOSUnifiedAuth, Error, TEXT("[Auth] EOS_Initialize failed: %hs"), EOS_EResult_ToString(Rc));
			return false;
		}
		if (Rc == EOS_EResult::EOS_AlreadyConfigured)
		{
			UE_LOG(LogEOSUnifiedAuth, Verbose, TEXT("[Auth] EOS already initialized in this process."));
		}
		else
		{
			UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] EOS_Initialize succeeded; we own SDK lifetime."));
		}
	}

	// Build platform options
	std::string ProductId    = SampleConstants::ProductId;
	std::string SandboxId    = SampleConstants::SandboxId;
	std::string DeploymentId = SampleConstants::DeploymentId;
	std::string ClientId     = SampleConstants::ClientCredentialsId;
	std::string ClientSecret = SampleConstants::ClientCredentialsSecret;

	EOS_Platform_Options P{};
	P.ApiVersion     = EOS_PLATFORM_OPTIONS_API_LATEST;
	P.bIsServer      = EOS_FALSE;
	P.ProductId      = ProductId.c_str();
	P.SandboxId      = SandboxId.c_str();
	P.DeploymentId   = DeploymentId.c_str();
	P.EncryptionKey  = SampleConstants::EncryptionKey; // 64 hex chars
	P.Flags          = 0;

	// Cache dir must be absolute; keep ANSI buffer alive in OwnedCacheDirAnsi
	const FString AbsCacheDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EOS"));
	IFileManager::Get().MakeDirectory(*AbsCacheDir, true);
	OwnedCacheDirAnsi = TCHAR_TO_UTF8(*AbsCacheDir);
	P.CacheDirectory  = OwnedCacheDirAnsi.c_str();

	if (!ClientId.empty() && !ClientSecret.empty())
	{
		P.ClientCredentials.ClientId     = ClientId.c_str();
		P.ClientCredentials.ClientSecret = ClientSecret.c_str();
	}

	// RTC options: leave null unless you deliberately wire them (prevents XAudio2.9 path errors)
	P.RTCOptions = nullptr;

	if (!P.EncryptionKey || std::strlen(P.EncryptionKey) != 64)
	{
		UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] EncryptionKey must be 64 hex characters."));
	}

	PlatformHandle = EOS_Platform_Create(&P);
	if (!PlatformHandle)
	{
		UE_LOG(LogEOSUnifiedAuth, Error, TEXT("[Auth] EOS_Platform_Create failed. Check ProductId/Sandbox/Deployment/Client creds and absolute CacheDirectory."));
		return false;
	}

	bCreatedPlatform = true;
	bUsingEngineEOS  = false;

	UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] EOS Platform created: %p | CacheDir=%s"), PlatformHandle, *FString(AbsCacheDir));
	return true;
}

void EOSUnifiedAuthManager::Shutdown()
{
	// Clear user state
	UserId = nullptr;
	ProductUserId = nullptr;

	// Release platform only if we created it
	if (PlatformHandle && bCreatedPlatform)
	{
		UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Releasing EOS Platform: %p"), PlatformHandle);
		EOS_Platform_Release(PlatformHandle);
	}
	PlatformHandle   = nullptr;
	bCreatedPlatform = false;
	bUsingEngineEOS  = false;
	ClearCachedDisplayName();
}

// =========================== tick ============================

void EOSUnifiedAuthManager::Tick()
{
	if (PlatformHandle)
	{
		EOS_Platform_Tick(PlatformHandle);
		static int32 Counter = 0;
		if ((++Counter % 300) == 0) // ~5s at 60fps
		{
			UE_LOG(LogEOSUnified, Verbose, TEXT("[AuthManager] EOS_Platform_Tick OK (EAID=%p, PUID=%p)"), UserId, ProductUserId);
		}
	}
}

// ===================== login / logout ========================

void EOSUnifiedAuthManager::Login()
{
	if (!PlatformHandle)
	{
		if (OnLoginStateChanged) OnLoginStateChanged(false, "Platform handle not set");
		return;
	}

	EOS_HAuth Auth = EOS_Platform_GetAuthInterface(PlatformHandle);
	if (!Auth)
	{
		if (OnLoginStateChanged) OnLoginStateChanged(false, "Auth interface unavailable");
		return;
	}
	
	EOS_Auth_Credentials Creds{}; Creds.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
	EOS_Auth_LoginOptions Opt{};  Opt.ApiVersion   = EOS_AUTH_LOGIN_API_LATEST;
	Opt.Credentials = &Creds;
	Opt.ScopeFlags  = kRequiredAuthScopes;

	if (!RefreshToken.empty())
	{
		UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Attempting login via RefreshToken."));
		Creds.Type  = EOS_ELoginCredentialType::EOS_LCT_RefreshToken;
		Creds.Token = RefreshToken.c_str();
	}
	else
	{
		UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] No refresh token; attempting PersistentAuth."));
		Creds.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
	}

	EOS_Auth_Login(Auth, &Opt, this, &EOSUnifiedAuthManager::LoginCompleteCallback);
}

void EOSUnifiedAuthManager::LoginAccountPortal()
{
	if (!PlatformHandle) { UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] No platform.")); return; }
	EOS_HAuth Auth = EOS_Platform_GetAuthInterface(PlatformHandle);
	if (!Auth) { UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] No Auth interface.")); return; }

	EOS_Auth_Credentials Creds{}; Creds.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
	Creds.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
	
	EOS_Auth_LoginOptions Opt{}; Opt.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
	Opt.Credentials = &Creds;
	Opt.ScopeFlags  = kRequiredAuthScopes;

	bIsAccountPortalActive.store(true);
	UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Login via Account Portal..."));
	EOS_Auth_Login(Auth, &Opt, this, &EOSUnifiedAuthManager::LoginCompleteCallback);
}

void EOSUnifiedAuthManager::HandleLoginCallback(const EOS_Auth_LoginCallbackInfo* Data)
{
	bIsAccountPortalActive.store(false);

	EOS_HAuth Auth = EOS_Platform_GetAuthInterface(PlatformHandle);
	const bool bOk = (Data && Data->ResultCode == EOS_EResult::EOS_Success);

	if (!bOk)
	{
		UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] Login failed: %hs"), Data ? EOS_EResult_ToString(Data->ResultCode) : "NoData");
		if (OnLoginStateChanged) OnLoginStateChanged(false, "Login failed");
		return;
	}

	UserId = Data->LocalUserId;
	bAuthLoginComplete = true;

	// Copy auth token -> persist refresh; start Connect with access token
	EOS_Auth_Token* Tok = nullptr;
	EOS_Auth_CopyUserAuthTokenOptions Copy{}; Copy.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;
	if (EOS_Auth_CopyUserAuthToken(Auth, &Copy, UserId, &Tok) == EOS_EResult::EOS_Success && Tok)
	{
		if (Tok->RefreshToken) { RefreshToken = Tok->RefreshToken; SaveRefreshToken(); }
		if (Tok->AccessToken)  { StartConnectLogin(Tok->AccessToken); }
		else
		{
			UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] No AccessToken after Auth login."));
			if (OnLoginStateChanged) OnLoginStateChanged(false, "Auth OK but no access token for Connect");
		}
		EOS_Auth_Token_Release(Tok);
	}
	else
	{
		UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] Could not copy user auth token after Auth login."));
		if (OnLoginStateChanged) OnLoginStateChanged(false, "Could not copy user auth token");
	}

	QueryLocalUserDisplayName();

	UE_LOG(LogEOSUnified, Log, TEXT("[AuthManager] Auth login OK: EAID=%s"), *EOSUnified::EpicIdToString(UserId));
}

void EOS_CALL EOSUnifiedAuthManager::LoginCompleteCallback(const EOS_Auth_LoginCallbackInfo* Data)
{
	if (Data && Data->ClientData)
	{
		static_cast<EOSUnifiedAuthManager*>(Data->ClientData)->HandleLoginCallback(Data);
	}
}

void EOSUnifiedAuthManager::StartConnectLogin(const char* accessToken)
{
	UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Start Connect login..."));

	EOS_HConnect Conn = EOS_Platform_GetConnectInterface(PlatformHandle);
	if (!Conn)
	{
		if (OnLoginStateChanged) OnLoginStateChanged(false, "Connect interface unavailable");
		return;
	}

	EOS_Connect_Credentials CC{}; CC.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	CC.Type  = EOS_EExternalCredentialType::EOS_ECT_EPIC;
	CC.Token = accessToken;

	EOS_Connect_LoginOptions LO{}; LO.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	LO.Credentials = &CC;

	EOS_Connect_Login(Conn, &LO, this,
		[](const EOS_Connect_LoginCallbackInfo* Info)
		{
			auto* Self = static_cast<EOSUnifiedAuthManager*>(Info ? Info->ClientData : nullptr);
			if (!Self) return;

			UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Connect login callback: %hs"), EOS_EResult_ToString(Info->ResultCode));

			if (Info->ResultCode == EOS_EResult::EOS_Success && EOS_ProductUserId_IsValid(Info->LocalUserId))
			{
				Self->ProductUserId = Info->LocalUserId;
				Self->bConnectLoginComplete = true;

				Self->QueryLocalUserDisplayName();

				if (Self->OnLoginStateChanged) Self->OnLoginStateChanged(true, "Connect login successful");
				Self->LogCurrentUserDisplayName();
				UE_LOG(LogEOSUnified, Log, TEXT("[AuthManager] Connect login OK: PUID=%s"),
					*EOSUnified::ProductUserIdToString_Safe(Self->ProductUserId));
			}
			else
			{
				if (Self->OnLoginStateChanged) Self->OnLoginStateChanged(false, "Connect login failed");
			}
		});
}

void EOSUnifiedAuthManager::Logout()
{
	DeleteRefreshToken();

	if (PlatformHandle)
	{
		EOS_HAuth Auth = EOS_Platform_GetAuthInterface(PlatformHandle);
		if (Auth && UserId)
		{
			EOS_Auth_LogoutOptions O{}; O.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST; O.LocalUserId = UserId;
			EOS_Auth_Logout(Auth, &O, nullptr, nullptr);
		}
	}

	UserId = nullptr;
	if (OnLoginStateChanged) OnLoginStateChanged(false, "Logged out");
}

void EOSUnifiedAuthManager::HardLogout(std::atomic<bool>* done)
{
	UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] HardLogout begin"));

	// 1) Connect logout
	if (ProductUserId)
	{
		EOS_HConnect C = EOS_Platform_GetConnectInterface(PlatformHandle);
		EOS_Connect_LogoutOptions O{}; O.ApiVersion = EOS_CONNECT_LOGOUT_API_LATEST; O.LocalUserId = ProductUserId;
		EOS_Connect_Logout(C, &O, this,
			[](const EOS_Connect_LogoutCallbackInfo* Data)
			{
				auto* Self = static_cast<EOSUnifiedAuthManager*>(Data->ClientData);
				UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Connect_Logout rc=%hs"), EOS_EResult_ToString(Data->ResultCode));
				Self->ProductUserId = nullptr;
				Self->OnConnectLogoutComplete(Data, nullptr);
			});
	}
	else
	{
		OnConnectLogoutComplete(nullptr, done);
	}
}

void EOSUnifiedAuthManager::OnConnectLogoutComplete(const EOS_Connect_LogoutCallbackInfo*, std::atomic<bool>* done)
{
	// 2) Auth logout
	if (UserId)
	{
		EOS_HAuth A = EOS_Platform_GetAuthInterface(PlatformHandle);
		EOS_Auth_LogoutOptions O{}; O.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST; O.LocalUserId = UserId;
		EOS_Auth_Logout(A, &O, this,
			[](const EOS_Auth_LogoutCallbackInfo* Data)
			{
				auto* Self = static_cast<EOSUnifiedAuthManager*>(Data->ClientData);
				UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] Auth_Logout rc=%hs"), EOS_EResult_ToString(Data->ResultCode));
				Self->UserId = nullptr;
				Self->OnAuthLogoutComplete(Data, nullptr);
			});
	}
	else
	{
		OnAuthLogoutComplete(nullptr, done);
	}
}

void EOSUnifiedAuthManager::OnAuthLogoutComplete(const EOS_Auth_LogoutCallbackInfo*, std::atomic<bool>* done)
{
	// 3) Revoke persistent auth on server (if we still have token)
	EOS_HAuth A = EOS_Platform_GetAuthInterface(PlatformHandle);
	EOS_Auth_DeletePersistentAuthOptions Del{}; Del.ApiVersion = EOS_AUTH_DELETEPERSISTENTAUTH_API_LATEST;
	Del.RefreshToken = RefreshToken.empty() ? nullptr : RefreshToken.c_str();

	EOS_Auth_DeletePersistentAuth(A, &Del, this,
		[](const EOS_Auth_DeletePersistentAuthCallbackInfo* Data)
		{
			auto* Self = static_cast<EOSUnifiedAuthManager*>(Data->ClientData);
			UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] DeletePersistentAuth rc=%hs"), EOS_EResult_ToString(Data->ResultCode));
			Self->OnDeletePersistentAuthComplete(Data, nullptr);
		});
}

void EOSUnifiedAuthManager::OnDeletePersistentAuthComplete(const EOS_Auth_DeletePersistentAuthCallbackInfo*, std::atomic<bool>* done)
{
	DeleteRefreshToken();
	bAuthLoginComplete    = false;
	bConnectLoginComplete = false;

	if (OnLoginStateChanged) OnLoginStateChanged(false, "Hard logout completed");
	if (done) done->store(true);
	UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] HardLogout complete"));
}

// ======================== diagnostics ========================

void EOSUnifiedAuthManager::LogCurrentUserDisplayName()
{
	if (!PlatformHandle || !UserId)
	{
		UE_LOG(LogEOSUnifiedAuth, Verbose, TEXT("[Auth] No user yet for display name query."));
		return;
	}

	EOS_HUserInfo UI = EOS_Platform_GetUserInfoInterface(PlatformHandle);
	if (!UI)
	{
		UE_LOG(LogEOSUnifiedAuth, Warning, TEXT("[Auth] No UserInfo interface."));
		return;
	}

	EOS_UserInfo_QueryUserInfoOptions Q{}; Q.ApiVersion = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
	Q.LocalUserId = UserId; Q.TargetUserId = UserId;

	EOS_UserInfo_QueryUserInfo(UI, &Q, UI,
		[](const EOS_UserInfo_QueryUserInfoCallbackInfo* Data)
		{
			if (!Data || Data->ResultCode != EOS_EResult::EOS_Success) return;

			EOS_HUserInfo UIH = static_cast<EOS_HUserInfo>(Data->ClientData);
			EOS_UserInfo* Info = nullptr;

			EOS_UserInfo_CopyUserInfoOptions C{}; C.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
			C.LocalUserId = Data->LocalUserId; C.TargetUserId = Data->TargetUserId;

			if (EOS_UserInfo_CopyUserInfo(UIH, &C, &Info) == EOS_EResult::EOS_Success && Info)
			{
				char EpicId[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = {}; int32_t Len = (int32)sizeof(EpicId);
				if (Info->UserId && EOS_EpicAccountId_IsValid(Info->UserId))
					EOS_EpicAccountId_ToString(Info->UserId, EpicId, &Len);

				UE_LOG(LogEOSUnifiedAuth, Log, TEXT("[Auth] DisplayName: %hs | EpicUserId: %hs"),
					Info->DisplayName ? Info->DisplayName : "<none>", EpicId);

				EOS_UserInfo_Release(Info);
			}
		});
}

void EOSUnifiedAuthManager::QueryLocalUserDisplayName()
{
    if (!PlatformHandle || !UserId) { return; }

    EOS_HUserInfo UI = EOS_Platform_GetUserInfoInterface(PlatformHandle);

    EOS_UserInfo_QueryUserInfoOptions Q{};
    Q.ApiVersion   = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
    Q.LocalUserId  = UserId;   // querying as self
    Q.TargetUserId = UserId;   // target is also self

    struct FCtx { EOSUnifiedAuthManager* Self; };
    auto* Ctx = new FCtx{ this };

    EOS_UserInfo_QueryUserInfo(UI, &Q, Ctx,
        [](const EOS_UserInfo_QueryUserInfoCallbackInfo* Data)
        {
            std::unique_ptr<FCtx> Holder(static_cast<FCtx*>(Data->ClientData));
            EOSUnifiedAuthManager* Self = Holder->Self;
            if (!Self) return;

            if (Data->ResultCode != EOS_EResult::EOS_Success)
            {
                UE_LOG(LogEOSUnified, Verbose, TEXT("[AuthManager] QueryUserInfo failed: %s"),
                       UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
                return;
            }

            // Copy the info to read the display name
            EOS_HUserInfo UI2 = EOS_Platform_GetUserInfoInterface(Self->PlatformHandle);
            EOS_UserInfo* Info = nullptr;

            EOS_UserInfo_CopyUserInfoOptions C{};
            C.ApiVersion   = EOS_USERINFO_COPYUSERINFO_API_LATEST;
            C.LocalUserId  = Self->UserId;
            C.TargetUserId = Self->UserId;

            if (EOS_UserInfo_CopyUserInfo(UI2, &C, &Info) == EOS_EResult::EOS_Success && Info)
            {
                const char* Name = Info->DisplayName; // may be null
                Self->CachedDisplayName = Name ? UTF8_TO_TCHAR(Name) : TEXT("");
                EOS_UserInfo_Release(Info);

                UE_LOG(LogEOSUnified, Log, TEXT("[AuthManager] Cached display name: %s"),
                       *Self->CachedDisplayName);

                // If you want, notify the Subsystem via an existing auth event or a new callback.
                if (Self->OnAuthDisplayNameCached) // optional std::function delegate if you have one
                {
                    Self->OnAuthDisplayNameCached(Self->CachedDisplayName);
                }
            }
            else
            {
                UE_LOG(LogEOSUnified, Verbose, TEXT("[AuthManager] CopyUserInfo returned no data."));
            }
        });
}


bool EOSUnifiedAuthManager::IsLoggedIn() const
{
	return bIsLoggedIn && UserId;
}

int32 EOSUnifiedAuthManager::GetLocalUserNum() const
{
	return CachedLocalUserNum;
}

FString EOSUnifiedAuthManager::GetEpicAccountIdString() const
{
	return UserId ? *EOSUnified::EpicIdToString(UserId) : TEXT("");
}

FString EOSUnifiedAuthManager::GetDeploymentOrSandboxId() const
{
	return CachedDeploymentOrSandbox;
}


void EOSUnifiedAuthManager::SetCachedDisplayName(const FString& InName)
{
	CachedDisplayName = InName;
}

void EOSUnifiedAuthManager::SetCachedDeploymentOrSandbox(const FString& InNamespace)
{
	CachedDeploymentOrSandbox = InNamespace;
}