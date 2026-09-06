#include <MinHook.h>

#include <atomic>
#include <cstring>

namespace
{
    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";
    constexpr auto kVanillaFaceSuffix = "BaseFemaleHead_faceBones.nif";
    constexpr auto kVanillaRearSuffix = "FemaleheadRear_faceBones.nif";

    constexpr std::uint64_t kDoUpdate3DModelOG = 114457;
    constexpr std::uint64_t kDoUpdate3DModelNG = 2232144;

    using Args = RE::BSModelDB::DBTraits::ArgsType;
    using Demand1Fn = RE::BSResource::ErrorCode (*)(const char*, RE::BSModelDB::Handle&, const Args&);
    using Demand2Fn = RE::BSResource::ErrorCode (*)(const char*, RE::NiPointer<RE::NiNode>*, const Args&);

    Demand1Fn g_originalDemand1 = nullptr;
    Demand2Fn g_originalDemand2 = nullptr;

    std::atomic_bool g_redirectWindow{ false };
    std::atomic_uint32_t g_faceRedirects{ 0 };
    std::atomic_uint32_t g_rearRedirects{ 0 };
    std::atomic_bool g_appliedThisLoad{ false };

    [[nodiscard]] std::uint64_t GetDoUpdate3DModelID()
    {
        const auto runtime = REX::FModule::GetExecutingModule().GetFileVersion();
        return runtime >= REL::Version{ 1, 10, 980, 0 } ? kDoUpdate3DModelNG : kDoUpdate3DModelOG;
    }

    [[nodiscard]] bool EndsWithI(const char* a_value, const char* a_suffix)
    {
        if (!a_value || !a_suffix) {
            return false;
        }

        const auto valueLen = std::strlen(a_value);
        const auto suffixLen = std::strlen(a_suffix);
        if (valueLen < suffixLen) {
            return false;
        }

        return _stricmp(a_value + valueLen - suffixLen, a_suffix) == 0;
    }

    [[nodiscard]] const char* RedirectPath(const char* a_name, const Args& a_args)
    {
        if (!a_name || !g_redirectWindow.load(std::memory_order_acquire) || !a_args.faceGenModel) {
            return a_name;
        }

        if (EndsWithI(a_name, kVanillaFaceSuffix)) {
            const auto hit = ++g_faceRedirects;
            REX::INFO("RUN13 redirect FACE request #{}: {} -> {}", hit, a_name, kCustomFace);
            return kCustomFace;
        }

        if (EndsWithI(a_name, kVanillaRearSuffix)) {
            const auto hit = ++g_rearRedirects;
            REX::INFO("RUN13 redirect REAR request #{}: {} -> {}", hit, a_name, kCustomRear);
            return kCustomRear;
        }

        return a_name;
    }

    RE::BSResource::ErrorCode Demand1Hook(const char* a_name, RE::BSModelDB::Handle& a_result, const Args& a_args)
    {
        return g_originalDemand1(RedirectPath(a_name, a_args), a_result, a_args);
    }

    RE::BSResource::ErrorCode Demand2Hook(const char* a_name, RE::NiPointer<RE::NiNode>* a_result, const Args& a_args)
    {
        return g_originalDemand2(RedirectPath(a_name, a_args), a_result, a_args);
    }

    [[nodiscard]] bool InstallModelHooks()
    {
        const auto init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            REX::ERROR("RUN13 MH_Initialize failed: {}", static_cast<int>(init));
            return false;
        }

        REL::Relocation<std::uintptr_t> demand1{ RE::ID::BSModelDB::Demand1 };
        REL::Relocation<std::uintptr_t> demand2{ RE::ID::BSModelDB::Demand2 };

        const auto create1 = MH_CreateHook(
            reinterpret_cast<LPVOID>(demand1.address()),
            reinterpret_cast<LPVOID>(&Demand1Hook),
            reinterpret_cast<LPVOID*>(&g_originalDemand1));
        if (create1 != MH_OK && create1 != MH_ERROR_ALREADY_CREATED) {
            REX::ERROR("RUN13 MH_CreateHook Demand1 failed: {}", static_cast<int>(create1));
            return false;
        }

        const auto create2 = MH_CreateHook(
            reinterpret_cast<LPVOID>(demand2.address()),
            reinterpret_cast<LPVOID>(&Demand2Hook),
            reinterpret_cast<LPVOID*>(&g_originalDemand2));
        if (create2 != MH_OK && create2 != MH_ERROR_ALREADY_CREATED) {
            REX::ERROR("RUN13 MH_CreateHook Demand2 failed: {}", static_cast<int>(create2));
            return false;
        }

        const auto enable = MH_EnableHook(MH_ALL_HOOKS);
        if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
            REX::ERROR("RUN13 MH_EnableHook failed: {}", static_cast<int>(enable));
            return false;
        }

        REX::INFO(
            "RUN13 BSModelDB MinHook detours installed. Demand1={} Demand2={}",
            reinterpret_cast<const void*>(demand1.address()),
            reinterpret_cast<const void*>(demand2.address()));
        return true;
    }

    [[nodiscard]] RE::BGSHeadPart* FindPartByType(
        std::span<RE::BGSHeadPart*> a_parts,
        RE::BGSHeadPart::HeadPartType a_type)
    {
        for (auto* part : a_parts) {
            if (part && part->type == a_type) {
                return part;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool PlayerFemaleHeadReady(RE::PlayerCharacter* a_player)
    {
        if (!a_player || !a_player->Get3D() || !a_player->GetFaceNodeSkinned()) {
            return false;
        }

        auto* npc = a_player->GetNPC();
        if (!npc) {
            return false;
        }

        auto active = npc->GetHeadParts(true);
        auto* face = FindPartByType(active, RE::BGSHeadPart::HeadPartType::kFace);
        auto* rear = FindPartByType(active, RE::BGSHeadPart::HeadPartType::kHeadRear);
        if (!face || !rear) {
            return false;
        }

        const auto* faceCG = face->ChargenModel.GetModel();
        const auto* rearCG = rear->ChargenModel.GetModel();
        const bool femaleFace = faceCG && EndsWithI(faceCG, kVanillaFaceSuffix);
        const bool femaleRear = rearCG && EndsWithI(rearCG, kVanillaRearSuffix);

        if (!femaleFace || !femaleRear) {
            REX::INFO(
                "RUN13 waiting for female player head. faceCG={} rearCG={}",
                faceCG ? faceCG : "<null>",
                rearCG ? rearCG : "<null>");
        }
        return femaleFace && femaleRear;
    }

    bool RebuildPlayerHeadWithRedirect()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!PlayerFemaleHeadReady(player) || !player->currentProcess) {
            return false;
        }

        if (g_appliedThisLoad.exchange(true)) {
            return true;
        }

        g_faceRedirects.store(0);
        g_rearRedirects.store(0);
        g_redirectWindow.store(true, std::memory_order_release);

        constexpr auto kHeadFaceFlags = static_cast<RE::RESET_3D_FLAGS>(
            static_cast<std::uint16_t>(RE::RESET_3D_FLAGS::kHead) |
            static_cast<std::uint16_t>(RE::RESET_3D_FLAGS::kFace));

        player->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kHead);
        player->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kFace);

        using Update3DFn = void (*)(RE::AIProcess*, RE::Actor*, RE::RESET_3D_FLAGS);
        const auto id = GetDoUpdate3DModelID();
        REL::Relocation<Update3DFn> doUpdate3D{ REL::ID(id) };

        REX::INFO("RUN13 starting PLAYER-only head rebuild; redirect window ARMED. relocation={}", id);
        doUpdate3D(player->currentProcess, static_cast<RE::Actor*>(player), kHeadFaceFlags);

        REX::INFO(
            "RUN13 DoUpdate3DModel returned. faceRedirects={} rearRedirects={}",
            g_faceRedirects.load(),
            g_rearRedirects.load());

        return true;
    }

    void QueueCloseWindow(std::uint32_t a_ticks)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            g_redirectWindow.store(false, std::memory_order_release);
            return;
        }

        tasks->AddTask([a_ticks]() {
            if (a_ticks > 1) {
                QueueCloseWindow(a_ticks - 1);
                return;
            }

            g_redirectWindow.store(false, std::memory_order_release);
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* faceNode = player ? player->GetFaceNodeSkinned() : nullptr;
            REX::INFO(
                "RUN13 redirect window CLOSED. faceRedirects={} rearRedirects={} faceNode={}",
                g_faceRedirects.load(),
                g_rearRedirects.load(),
                static_cast<const void*>(faceNode));
        });
    }

    void QueueApply(std::uint32_t a_attempts)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            if (RebuildPlayerHeadWithRedirect()) {
                QueueCloseWindow(4);
            }
            return;
        }

        tasks->AddTask([a_attempts]() {
            if (RebuildPlayerHeadWithRedirect()) {
                QueueCloseWindow(4);
            } else if (a_attempts > 1) {
                QueueApply(a_attempts - 1);
            }
        });
    }

    void MessageHandler(F4SE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        switch (a_message->type) {
        case F4SE::MessagingInterface::kPostLoadGame:
        case F4SE::MessagingInterface::kNewGame:
            g_appliedThisLoad.store(false);
            QueueApply(120);
            break;
        default:
            break;
        }
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    if (!InstallModelHooks()) {
        return false;
    }

    if (const auto messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener(MessageHandler);
    } else {
        return false;
    }

    if (!F4SE::GetTaskInterface()) {
        return false;
    }

    REX::INFO(
        "ABDOSAPlayerHead RUN13 loaded; PLAYER-SCOPED FaceGen model-request redirect. No HeadPart records or texture paths are modified.");
    return true;
}
