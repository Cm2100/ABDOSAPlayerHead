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

    [[nodiscard]] std::uint64_t GetDoUpdate3DModelID()
    {
        const auto runtime = REL::Module::get().version();
        return runtime >= REL::Version{ 1, 10, 980, 0 } ? kDoUpdate3DModelNG : kDoUpdate3DModelOG;
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

        REX::INFO("Calling synchronous DoUpdate3dModel relocation ID {}", id);
        doUpdate3D(a_player->currentProcess, static_cast<RE::Actor*>(a_player), kHeadFaceFlags);
        return true;
    }

    bool ApplyToPlayer()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            REX::WARN("Player singleton not ready");
            return false;
        }

        auto* faceForm = RE::TESForm::GetFormByID(kFemaleFaceHeadPart);
        auto* rearForm = RE::TESForm::GetFormByID(kFemaleRearHeadPart);
        auto* face = faceForm ? faceForm->As<RE::BGSHeadPart>() : nullptr;
        auto* rear = rearForm ? rearForm->As<RE::BGSHeadPart>() : nullptr;
        if (!face || !rear) {
            REX::ERROR("Could not resolve vanilla female head parts");
            return false;
        }

        if (g_faceOriginal.empty()) {
            g_faceOriginal = face->GetModel() ? face->GetModel() : "";
        }
        if (g_rearOriginal.empty()) {
            g_rearOriginal = rear->GetModel() ? rear->GetModel() : "";
        }

        // Redirect only while the PLAYER'S head rebuild is executed synchronously.
        // The global vanilla HDPT model paths are restored immediately afterwards,
        // so NPCs keep using the vanilla front/rear head meshes.
        face->SetModel(kCustomFace);
        rear->SetModel(kCustomRear);

        const bool rebuilt = RebuildPlayerHead(player);

        face->SetModel(g_faceOriginal.c_str());
        rear->SetModel(g_rearOriginal.c_str());

        if (!rebuilt) {
            return false;
        }

        REX::INFO("Applied custom front/rear head meshes to player and restored vanilla global paths");
        return true;
    }

    void MessageHandler(F4SE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        switch (a_message->type) {
        case F4SE::MessagingInterface::kGameDataReady:
            if (static_cast<bool>(a_message->data)) {
                ApplyToPlayer();
            }
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
            ApplyToPlayer();
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

    REX::INFO("ABDOSAPlayerHead loaded");
    return true;
}
