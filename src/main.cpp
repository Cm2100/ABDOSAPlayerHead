namespace
{
    constexpr std::uint32_t kFemaleFaceHeadPart = 0x000CFB3F;
    constexpr std::uint32_t kFemaleRearHeadPart = 0x0004D0E9;

    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";

    std::string g_faceOriginal;
    std::string g_rearOriginal;

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

        // Redirect only while the player's 3D rebuild is executed synchronously.
        face->SetModel(kCustomFace);
        rear->SetModel(kCustomRear);

        player->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kHead);
        player->Set3DUpdateFlag(RE::RESET_3D_FLAGS::kFace);

        if (player->currentProcess) {
            player->currentProcess->Update3DModel(player);
        } else {
            REX::WARN("Player AIProcess is not ready; head rebuild deferred");
            face->SetModel(g_faceOriginal.c_str());
            rear->SetModel(g_rearOriginal.c_str());
            return false;
        }

        face->SetModel(g_faceOriginal.c_str());
        rear->SetModel(g_rearOriginal.c_str());

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
