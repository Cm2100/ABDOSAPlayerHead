namespace
{
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

    [[nodiscard]] bool ResolvePlayerSourceParts(
        RE::TESNPC* a_playerNPC,
        RE::BGSHeadPart*& a_face,
        RE::BGSHeadPart*& a_rear)
    {
        if (!a_playerNPC) {
            return false;
        }

        // Prefer the ACTIVE list. This is important because LooksMenu/race/chargen can
        // give the player a different HeadRear record than Fallout4.esm|04D0E9.
        auto active = a_playerNPC->GetHeadParts(true);
        a_face = FindPartByType(active, RE::BGSHeadPart::HeadPartType::kFace);
        a_rear = FindPartByType(active, RE::BGSHeadPart::HeadPartType::kHeadRear);

        // Fall back to the base NPC list only for whichever type was not present.
        auto base = a_playerNPC->GetHeadParts(false);
        if (!a_face) {
            a_face = FindPartByType(base, RE::BGSHeadPart::HeadPartType::kFace);
        }
        if (!a_rear) {
            a_rear = FindPartByType(base, RE::BGSHeadPart::HeadPartType::kHeadRear);
        }

        if (!a_face || !a_rear) {
            REX::ERROR(
                "Could not resolve player source head parts by TYPE. face={}, rear={}",
                a_face != nullptr,
                a_rear != nullptr);
            return false;
        }

        REX::INFO(
            "Resolved PLAYER source parts by TYPE. face={:08X}, rear={:08X}",
            a_face->GetFormID(),
            a_rear->GetFormID());
        return true;
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

    [[nodiscard]] bool EnsurePlayerHeadParts(RE::BGSHeadPart* a_sourceFace, RE::BGSHeadPart* a_sourceRear)
    {
        if (!g_playerFacePart) {
            g_playerFacePart = DuplicateHeadPart(a_sourceFace, kCustomFace);
        }
        if (!g_playerRearPart) {
            g_playerRearPart = DuplicateHeadPart(a_sourceRear, kCustomRear);
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

    struct PatchResult
    {
        bool face{ false };
        bool rear{ false };
    };

    [[nodiscard]] PatchResult PatchHeadPartSpan(std::span<RE::BGSHeadPart*> a_parts)
    {
        PatchResult result{};

        for (auto& part : a_parts) {
            if (!part) {
                continue;
            }

            if (part == g_playerFacePart) {
                result.face = true;
                continue;
            }
            if (part == g_playerRearPart) {
                result.rear = true;
                continue;
            }

            // Do NOT depend on Fallout4.esm FormIDs here. Replace whichever Face and
            // HeadRear records the PLAYER actually owns, including LooksMenu/custom ones.
            if (part->type == RE::BGSHeadPart::HeadPartType::kFace) {
                part = g_playerFacePart;
                result.face = true;
            } else if (part->type == RE::BGSHeadPart::HeadPartType::kHeadRear) {
                part = g_playerRearPart;
                result.rear = true;
            }
        }

        return result;
    }

    [[nodiscard]] bool AssignPlayerOnlyHeadParts(RE::TESNPC* a_playerNPC)
    {
        if (!a_playerNPC) {
            return false;
        }

        const auto base = PatchHeadPartSpan(a_playerNPC->GetHeadParts(false));
        const auto active = PatchHeadPartSpan(a_playerNPC->GetHeadParts(true));

        const bool faceOK = base.face || active.face;
        const bool rearOK = base.rear || active.rear;

        REX::INFO(
            "PLAYER head array patched by TYPE. base(face={},rear={}) active(face={},rear={})",
            base.face,
            base.rear,
            active.face,
            active.rear);

        return faceOK && rearOK;
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

        RE::BGSHeadPart* sourceFace = nullptr;
        RE::BGSHeadPart* sourceRear = nullptr;
        if (!ResolvePlayerSourceParts(playerNPC, sourceFace, sourceRear)) {
            return false;
        }

        if (!EnsurePlayerHeadParts(sourceFace, sourceRear)) {
            return false;
        }

        if (!AssignPlayerOnlyHeadParts(playerNPC)) {
            REX::ERROR("Player NPC head-part arrays did not expose both Face and HeadRear types");
            return false;
        }

        // No shared Fallout4.esm HDPT model path is changed at any point.
        // NPCs keep whatever normal Face/HeadRear records they already use.
        // Only the PLAYER's head-part pointers are replaced with private duplicates.
        REX::INFO("PLAYER owns private Face/HeadRear parts; all NPC head-part records untouched");
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

    REX::INFO("ABDOSAPlayerHead loaded; player-only head split by HeadPart TYPE armed");
    return true;
}
