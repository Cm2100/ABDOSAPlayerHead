namespace
{
    constexpr std::uint32_t kFemaleFaceHeadPart = 0x000CFB3F;
    constexpr std::uint32_t kFemaleRearHeadPart = 0x0004D0E9;

    // PLAYER-only temporary redirect targets.
    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";

    // Explicit vanilla paths for every other female character.
    constexpr auto kVanillaFace = "Actors\\Character\\CharacterAssets\\BaseFemaleHead.nif";
    constexpr auto kVanillaRear = "Actors\\Character\\CharacterAssets\\FaceParts\\FemaleheadRear.nif";

    constexpr std::uint64_t kDoUpdate3DModelOG = 114457;
    constexpr std::uint64_t kDoUpdate3DModelNG = 2232144;

    [[nodiscard]] std::uint64_t GetDoUpdate3DModelID()
    {
        const auto runtime = REX::FModule::GetExecutingModule().GetFileVersion();
        return runtime >= REL::Version{ 1, 10, 980, 0 } ? kDoUpdate3DModelNG : kDoUpdate3DModelOG;
    }

    [[nodiscard]] bool ResolveHeadParts(RE::BGSHeadPart*& a_face, RE::BGSHeadPart*& a_rear)
    {
        auto* faceForm = RE::TESForm::GetFormByID(kFemaleFaceHeadPart);
        auto* rearForm = RE::TESForm::GetFormByID(kFemaleRearHeadPart);
        a_face = faceForm ? faceForm->As<RE::BGSHeadPart>() : nullptr;
        a_rear = rearForm ? rearForm->As<RE::BGSHeadPart>() : nullptr;
        return a_face && a_rear;
    }

    void RestoreNPCVanillaHeadPaths()
    {
        RE::BGSHeadPart* face = nullptr;
        RE::BGSHeadPart* rear = nullptr;
        if (!ResolveHeadParts(face, rear)) {
            REX::ERROR("Could not resolve female head parts while restoring NPC vanilla paths");
            return;
        }

        // Do not restore some mod-provided/global value. Force the exact vanilla female
        // front/rear meshes so all NPC females remain on the normal Fallout 4 head.
        face->SetModel(kVanillaFace);
        rear->SetModel(kVanillaRear);

        REX::INFO("NPC female head paths forced to vanilla: {} | {}", kVanillaFace, kVanillaRear);
    }

    void QueueRestore(std::uint32_t a_ticks)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            RestoreNPCVanillaHeadPaths();
            return;
        }

        tasks->AddTask([a_ticks]() {
            if (a_ticks > 1) {
                QueueRestore(a_ticks - 1);
            } else {
                RestoreNPCVanillaHeadPaths();
            }
        });
    }

    bool RebuildPlayerHead(RE::PlayerCharacter* a_player)
    {
        if (!a_player || !a_player->currentProcess) {
            REX::WARN("Player AIProcess is not ready; head rebuild deferred");
            return false;
        }

        constexpr auto kHeadFaceFlags = static_cast<RE::RESET_3D_FLAGS>(
            static_cast<std::uint16_t>(RE::RESET_3D_FLAGS::kHead) |
            static_cast<std::uint16_t>(RE::RESET_3D_FLAGS::kFace));

        a_player->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kHead);
        a_player->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kFace);

        using update3d_t = void (*)(RE::AIProcess*, RE::Actor*, RE::RESET_3D_FLAGS);
        const auto id = GetDoUpdate3DModelID();
        REL::Relocation<update3d_t> doUpdate3D{ REL::ID(id) };

        REX::INFO("Calling DoUpdate3dModel relocation ID {} for PLAYER", id);
        doUpdate3D(a_player->currentProcess, static_cast<RE::Actor*>(a_player), kHeadFaceFlags);
        return true;
    }

    bool ApplyToPlayer()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->currentProcess) {
            REX::WARN("Player or AIProcess not ready");
            return false;
        }

        RE::BGSHeadPart* face = nullptr;
        RE::BGSHeadPart* rear = nullptr;
        if (!ResolveHeadParts(face, rear)) {
            REX::ERROR("Could not resolve vanilla female head parts");
            return false;
        }

        // Temporarily point the two shared female head-part records at the custom meshes.
        // Only the PLAYER is rebuilt during this window.
        face->SetModel(kCustomFace);
        rear->SetModel(kCustomRear);

        REX::INFO("PLAYER redirect enabled: {} | {}", kCustomFace, kCustomRear);

        if (!RebuildPlayerHead(player)) {
            RestoreNPCVanillaHeadPaths();
            return false;
        }

        // Give the player's resource request enough task ticks to consume the custom NIFs,
        // then hard-restore the shared records to the exact vanilla female meshes.
        QueueRestore(2);
        return true;
    }

    void QueueApply(std::uint32_t a_attempts)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            ApplyToPlayer();
            return;
        }

        tasks->AddTask([a_attempts]() {
            if (!ApplyToPlayer() && a_attempts > 1) {
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
            // First guarantee NPCs are on vanilla paths, then rebuild only the player.
            RestoreNPCVanillaHeadPaths();
            QueueApply(30);
            break;
        case F4SE::MessagingInterface::kNewGame:
            RestoreNPCVanillaHeadPaths();
            QueueApply(30);
            break;
        default:
            break;
        }
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    if (const auto messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener(MessageHandler);
    } else {
        REX::ERROR("F4SE messaging interface unavailable");
        return false;
    }

    if (!F4SE::GetTaskInterface()) {
        REX::ERROR("F4SE task interface unavailable");
        return false;
    }

    REX::INFO("ABDOSAPlayerHead loaded; player custom head + NPC vanilla head split armed");
    return true;
}
