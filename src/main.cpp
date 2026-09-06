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

    void DumpHeadPart(const char* a_label, RE::BGSHeadPart* a_part)
    {
        if (!a_part) {
            REX::INFO("{} <null>", a_label);
            return;
        }

        REX::INFO(
            "{} form={:08X} type={} flags={} model={} textureSet={} chargen={} morph0={} morph1={} morph2={}",
            a_label,
            a_part->GetFormID(),
            static_cast<std::int32_t>(a_part->type.get()),
            static_cast<std::uint32_t>(a_part->flags.underlying()),
            a_part->GetModel() ? a_part->GetModel() : "<null-model>",
            static_cast<const void*>(a_part->textureSet),
            a_part->ChargenModel.GetModel() ? a_part->ChargenModel.GetModel() : "<null>",
            a_part->morphs[0].GetModel() ? a_part->morphs[0].GetModel() : "<null>",
            a_part->morphs[1].GetModel() ? a_part->morphs[1].GetModel() : "<null>",
            a_part->morphs[2].GetModel() ? a_part->morphs[2].GetModel() : "<null>");
    }

    void DumpHeadParts(const char* a_label, std::span<RE::BGSHeadPart*> a_parts)
    {
        REX::INFO("{} count={}", a_label, a_parts.size());
        std::size_t index = 0;
        for (auto* part : a_parts) {
            const auto itemLabel = fmt::format("{}[{}]", a_label, index++);
            DumpHeadPart(itemLabel.c_str(), part);
        }
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

        auto active = a_playerNPC->GetHeadParts(true);
        auto base = a_playerNPC->GetHeadParts(false);

        DumpHeadParts("PLAYER active BEFORE", active);
        DumpHeadParts("PLAYER base BEFORE", base);

        a_face = FindPartByType(active, RE::BGSHeadPart::HeadPartType::kFace);
        a_rear = FindPartByType(active, RE::BGSHeadPart::HeadPartType::kHeadRear);

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

        DumpHeadPart("Resolved PLAYER FACE source", a_face);
        DumpHeadPart("Resolved PLAYER REAR source", a_rear);
        return true;
    }

    void CopyHeadPartMetadata(RE::BGSHeadPart* a_duplicate, RE::BGSHeadPart* a_source)
    {
        // CreateDuplicateForm() did not preserve the BGSHeadPart-specific payload on this
        // runtime. The old RUN9 log proved that: after replacing the player list, both
        // private forms stopped matching Face/HeadRear TYPE. The same missing payload can
        // also drop the texture set and produce a dark face. Copy every rendering/chargen
        // field explicitly, then change ONLY the primary mesh path.
        static_cast<RE::TESModel*>(a_duplicate)->CopyComponent(static_cast<RE::TESModel*>(a_source));
        a_duplicate->swapForm = a_source->swapForm;
        a_duplicate->colorRemappingIndex = a_source->colorRemappingIndex;

        a_duplicate->flags = a_source->flags;
        a_duplicate->type = a_source->type;
        a_duplicate->extraParts = a_source->extraParts;
        a_duplicate->textureSet = a_source->textureSet;

        a_duplicate->ChargenModel.CopyComponent(&a_source->ChargenModel);
        for (std::size_t i = 0; i < 3; ++i) {
            a_duplicate->morphs[i].CopyComponent(&a_source->morphs[i]);
        }

        a_duplicate->colorForm = a_source->colorForm;
        a_duplicate->validRaces = a_source->validRaces;
        a_duplicate->chargenConditions.head = a_source->chargenConditions.head;
        a_duplicate->formEditorID = a_source->formEditorID;
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

        CopyHeadPartMetadata(duplicate, a_source);
        duplicate->SetModel(a_model);

        DumpHeadPart("Created metadata-preserving PRIVATE head part", duplicate);

        if (duplicate->type != a_source->type || duplicate->textureSet != a_source->textureSet) {
            REX::ERROR(
                "PRIVATE head-part metadata mismatch after copy. typeOK={} textureOK={}",
                duplicate->type == a_source->type,
                duplicate->textureSet == a_source->textureSet);
            return nullptr;
        }

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
            "PLAYER head array patched. base(face={},rear={}) active(face={},rear={})",
            base.face,
            base.rear,
            active.face,
            active.rear);

        DumpHeadParts("PLAYER active AFTER", a_playerNPC->GetHeadParts(true));
        DumpHeadParts("PLAYER base AFTER", a_playerNPC->GetHeadParts(false));
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
        REX::INFO("Rebuilding PLAYER head; relocation ID {}", id);
        doUpdate3D(a_player->currentProcess, static_cast<RE::Actor*>(a_player), kHeadFaceFlags);
        return true;
    }

    void QueueSecondRebuild(std::uint32_t a_ticks)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            RebuildPlayerHead(RE::PlayerCharacter::GetSingleton());
            return;
        }

        tasks->AddTask([a_ticks]() {
            if (a_ticks > 1) {
                QueueSecondRebuild(a_ticks - 1);
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }

            if (auto* npc = player->GetNPC()) {
                DumpHeadParts("PLAYER active BEFORE SECOND REBUILD", npc->GetHeadParts(true));
            }
            RebuildPlayerHead(player);
        });
    }

    bool ApplyToPlayer()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->currentProcess) {
            return false;
        }

        auto* playerNPC = player->GetNPC();
        if (!playerNPC) {
            return false;
        }

        REX::INFO(
            "Applying RUN11. Player actor={:08X}, Player NPC={:08X}, alternateList={}",
            player->GetFormID(),
            playerNPC->GetFormID(),
            playerNPC->UsingAlternateHeadPartList());

        RE::BGSHeadPart* sourceFace = nullptr;
        RE::BGSHeadPart* sourceRear = nullptr;
        if (!ResolvePlayerSourceParts(playerNPC, sourceFace, sourceRear)) {
            return false;
        }

        if (!EnsurePlayerHeadParts(sourceFace, sourceRear)) {
            return false;
        }

        if (!AssignPlayerOnlyHeadParts(playerNPC)) {
            return false;
        }

        if (!RebuildPlayerHead(player)) {
            return false;
        }

        QueueSecondRebuild(2);
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
        return false;
    }

    if (!F4SE::GetTaskInterface()) {
        return false;
    }

    REX::INFO("ABDOSAPlayerHead RUN11 loaded; metadata-preserving private HeadParts armed");
    return true;
}
