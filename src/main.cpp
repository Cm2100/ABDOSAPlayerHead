namespace
{
    constexpr std::uint32_t kFemaleFaceHeadPart = 0x000CFB3F;
    constexpr std::uint32_t kFemaleRearHeadPart = 0x0004D0E9;

    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";

    constexpr std::uint64_t kDoUpdate3DModelOG = 114457;
    constexpr std::uint64_t kDoUpdate3DModelNG = 2232144;

    RE::BGSHeadPart* g_playerFacePart = nullptr;
    RE::BGSHeadPart* g_playerRearPart = nullptr;

    [[nodiscard]] std::uint64_t GetDoUpdate3DModelID()
    {
        const auto runtime = REX::FModule::GetExecutingModule().GetFileVersion();
        return runtime >= REL::Version{ 1, 10, 980, 0 } ? kDoUpdate3DModelNG : kDoUpdate3DModelOG;
    }

    [[nodiscard]] bool ResolveVanillaHeadParts(RE::BGSHeadPart*& a_face, RE::BGSHeadPart*& a_rear)
    {
        a_face = RE::TESForm::GetFormByID<RE::BGSHeadPart>(kFemaleFaceHeadPart);
        a_rear = RE::TESForm::GetFormByID<RE::BGSHeadPart>(kFemaleRearHeadPart);
        return a_face && a_rear;
    }

    [[nodiscard]] RE::BGSHeadPart* DuplicateHeadPart(RE::BGSHeadPart* a_source, const char* a_model)
    {
        if (!a_source) {
            return nullptr;
        }

        auto* duplicateForm = a_source->CreateDuplicateForm(false, nullptr);
        auto* duplicate = duplicateForm ? duplicateForm->As<RE::BGSHeadPart>() : nullptr;
        if (!duplicate || duplicate == a_source) {
            REX::ERROR("Could not create an independent player-only BGSHeadPart duplicate");
            return nullptr;
        }

        duplicate->SetModel(a_model);
        return duplicate;
    }

    [[nodiscard]] bool EnsurePlayerHeadParts(RE::BGSHeadPart* a_vanillaFace, RE::BGSHeadPart* a_vanillaRear)
    {
        if (!g_playerFacePart) {
            g_playerFacePart = DuplicateHeadPart(a_vanillaFace, kCustomFace);
        }
        if (!g_playerRearPart) {
            g_playerRearPart = DuplicateHeadPart(a_vanillaRear, kCustomRear);
        }

        if (!g_playerFacePart || !g_playerRearPart) {
            return false;
        }

        REX::INFO(
            "Player-only head parts ready. Face form {:08X}, rear form {:08X}",
            g_playerFacePart->GetFormID(),
            g_playerRearPart->GetFormID());
        return true;
    }

    [[nodiscard]] bool PatchHeadPartSpan(
        std::span<RE::BGSHeadPart*> a_parts,
        RE::BGSHeadPart* a_vanillaFace,
        RE::BGSHeadPart* a_vanillaRear)
    {
        bool hasFace = false;
        bool hasRear = false;

        for (auto& part : a_parts) {
            if (!part) {
                continue;
            }

            if (part == g_playerFacePart) {
                hasFace = true;
                continue;
            }
            if (part == g_playerRearPart) {
                hasRear = true;
                continue;
            }

            if (part == a_vanillaFace || part->GetFormID() == kFemaleFaceHeadPart) {
                part = g_playerFacePart;
                hasFace = true;
            } else if (part == a_vanillaRear || part->GetFormID() == kFemaleRearHeadPart) {
                part = g_playerRearPart;
                hasRear = true;
            }
        }

        return hasFace && hasRear;
    }

    [[nodiscard]] bool AssignPlayerOnlyHeadParts(
        RE::TESNPC* a_playerNPC,
        RE::BGSHeadPart* a_vanillaFace,
        RE::BGSHeadPart* a_vanillaRear)
    {
        if (!a_playerNPC) {
            return false;
        }

        bool baseOK = PatchHeadPartSpan(a_playerNPC->GetHeadParts(false), a_vanillaFace, a_vanillaRear);

        // If the player is currently using an alternate head-part list (race/chargen path),
        // patch that list too. When no alternate list is active this simply sees the same
        // base list and is harmless.
        bool activeOK = PatchHeadPartSpan(a_playerNPC->GetHeadParts(true), a_vanillaFace, a_vanillaRear);

        return baseOK || activeOK;
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

        REX::INFO("Rebuilding PLAYER head with private head-part forms; relocation ID {}", id);
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

        auto* playerNPC = player->GetNPC();
        if (!playerNPC) {
            REX::ERROR("Could not resolve Player NPC base");
            return false;
        }

        RE::BGSHeadPart* vanillaFace = nullptr;
        RE::BGSHeadPart* vanillaRear = nullptr;
        if (!ResolveVanillaHeadParts(vanillaFace, vanillaRear)) {
            REX::ERROR("Could not resolve vanilla female head parts");
            return false;
        }

        if (!EnsurePlayerHeadParts(vanillaFace, vanillaRear)) {
            return false;
        }

        if (!AssignPlayerOnlyHeadParts(playerNPC, vanillaFace, vanillaRear)) {
            REX::ERROR("Player NPC head-part array did not contain the vanilla female face/rear records");
            return false;
        }

        // IMPORTANT: shared Fallout4.esm HDPT records are NEVER modified here.
        // NPCs therefore keep BaseFemaleHead.nif / FemaleheadRear.nif permanently,
        // while only Player NPC points at our duplicated BGSHeadPart forms.
        REX::INFO("PLAYER now owns private face/rear head-part pointers; shared NPC HDPTs untouched");
        return RebuildPlayerHead(player);
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

    REX::INFO("ABDOSAPlayerHead loaded; true player-only duplicated HDPT architecture armed");
    return true;
}
