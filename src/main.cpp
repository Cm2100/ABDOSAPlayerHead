namespace
{
    constexpr std::uint32_t kFemaleFaceHeadPart = 0x000CFB3F;
    constexpr std::uint32_t kFemaleRearHeadPart = 0x0004D0E9;

    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";

    constexpr std::uint64_t kDoUpdate3DModelOG = 114457;
    constexpr std::uint64_t kDoUpdate3DModelNG = 2232144;

    std::string g_faceOriginal;
    std::string g_rearOriginal;
    bool g_redirectActive = false;

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

    void RestoreVanillaHeadPaths()
    {
        RE::BGSHeadPart* face = nullptr;
        RE::BGSHeadPart* rear = nullptr;
        if (!ResolveHeadParts(face, rear)) {
            REX::ERROR("Could not resolve vanilla female head parts while restoring paths");
            return;
        }

        if (!g_faceOriginal.empty()) {
            face->SetModel(g_faceOriginal.c_str());
        }
        if (!g_rearOriginal.empty()) {
            rear->SetModel(g_rearOriginal.c_str());
        }

        g_redirectActive = false;
        REX::INFO("Restored vanilla global female head model paths after player rebuild window");
    }

    void QueueRestore(std::uint32_t a_frames)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            REX::WARN("F4SE task interface unavailable; restoring paths immediately");
            RestoreVanillaHeadPaths();
            return;
        }

        tasks->AddTask([a_frames]() {
            if (a_frames > 1) {
                QueueRestore(a_frames - 1);
            } else {
                RestoreVanillaHeadPaths();
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

        REX::INFO("Calling DoUpdate3dModel relocation ID {}", id);
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

        if (g_faceOriginal.empty()) {
            g_faceOriginal = face->GetModel() ? face->GetModel() : "";
        }
        if (g_rearOriginal.empty()) {
            g_rearOriginal = rear->GetModel() ? rear->GetModel() : "";
        }

        REX::INFO("Original face model: {}", g_faceOriginal);
        REX::INFO("Original rear model: {}", g_rearOriginal);

        face->SetModel(kCustomFace);
        rear->SetModel(kCustomRear);
        g_redirectActive = true;

        REX::INFO("Temporary player rebuild redirect enabled");
        const bool rebuilt = RebuildPlayerHead(player);
        if (!rebuilt) {
            RestoreVanillaHeadPaths();
            return false;
        }

        // DoUpdate3dModel can start model/resource work that outlives this call.
        // Keep the redirected paths alive for two task ticks so the PLAYER rebuild
        // can actually consume the custom NIF paths, then restore vanilla paths.
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
            // Run on the task queue instead of inside the load-game message itself.
            QueueApply(30);
            break;
        case F4SE::MessagingInterface::kNewGame:
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

    REX::INFO("ABDOSAPlayerHead loaded; delayed player-only head redirect armed");
    return true;
}
