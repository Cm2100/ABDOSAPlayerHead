namespace
{
    constexpr std::uint32_t kMaxDumpNodes = 700;
    constexpr std::uint32_t kMaxDumpDepth = 16;

    [[nodiscard]] const char* SafeName(RE::NiAVObject* a_object)
    {
        if (!a_object) {
            return "<null>";
        }
        const auto& name = a_object->GetName();
        return name.empty() ? "<unnamed>" : name.c_str();
    }

    [[nodiscard]] const char* SafeType(RE::NiAVObject* a_object)
    {
        if (!a_object) {
            return "<null>";
        }
        const auto* rtti = a_object->GetRTTI();
        return rtti && rtti->GetName() ? rtti->GetName() : "<no-rtti>";
    }

    void DumpTree(RE::NiAVObject* a_object, std::uint32_t a_depth, std::uint32_t& a_count, const char* a_tag)
    {
        if (!a_object || a_count >= kMaxDumpNodes || a_depth > kMaxDumpDepth) {
            return;
        }

        ++a_count;
        REX::INFO(
            "{} depth={} addr={} parent={} type={} name={}",
            a_tag,
            a_depth,
            static_cast<const void*>(a_object),
            static_cast<const void*>(a_object->parent),
            SafeType(a_object),
            SafeName(a_object));

        auto* node = a_object->IsNode();
        if (!node) {
            return;
        }

        for (auto& child : node->children) {
            if (child) {
                DumpTree(child.get(), a_depth + 1, a_count, a_tag);
            }
        }
    }

    void DumpHeadParts(const char* a_label, std::span<RE::BGSHeadPart*> a_parts)
    {
        REX::INFO("{} count={}", a_label, a_parts.size());
        std::size_t index = 0;
        for (auto* part : a_parts) {
            if (!part) {
                REX::INFO("{}[{}] <null>", a_label, index++);
                continue;
            }

            REX::INFO(
                "{}[{}] form={:08X} type={} model={} textureSet={} chargen={}",
                a_label,
                index++,
                part->GetFormID(),
                static_cast<std::int32_t>(part->type.get()),
                part->GetModel() ? part->GetModel() : "<null-model>",
                static_cast<const void*>(part->textureSet),
                part->ChargenModel.GetModel() ? part->ChargenModel.GetModel() : "<null>");
        }
    }

    [[nodiscard]] bool DumpPlayerHeadState()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            REX::WARN("RUN12 diagnostic: Player singleton not ready");
            return false;
        }

        auto* player3D = player->Get3D();
        auto* faceSkinned = player->GetFaceNodeSkinned();
        auto* faceNode = player->GetFaceNode();

        REX::INFO(
            "RUN12 diagnostic: player={:08X} 3D={} faceSkinned={} faceNode={}",
            player->GetFormID(),
            static_cast<const void*>(player3D),
            static_cast<const void*>(faceSkinned),
            static_cast<const void*>(faceNode));

        if (auto* npc = player->GetNPC()) {
            REX::INFO(
                "RUN12 diagnostic: Player NPC={:08X} alternateHeadParts={}",
                npc->GetFormID(),
                npc->UsingAlternateHeadPartList());
            DumpHeadParts("RUN12 PLAYER active HeadParts", npc->GetHeadParts(true));
            DumpHeadParts("RUN12 PLAYER base HeadParts", npc->GetHeadParts(false));
        }

        if (!player3D || !faceSkinned) {
            REX::WARN("RUN12 diagnostic: player 3D/face node not ready yet");
            return false;
        }

        std::uint32_t faceCount = 0;
        auto* faceAsAV = reinterpret_cast<RE::NiAVObject*>(faceSkinned);
        REX::INFO("RUN12 FACE TREE BEGIN");
        DumpTree(faceAsAV, 0, faceCount, "RUN12 FACE");
        REX::INFO("RUN12 FACE TREE END nodes={}", faceCount);

        std::uint32_t fullCount = 0;
        REX::INFO("RUN12 PLAYER 3D TREE BEGIN");
        DumpTree(player3D, 0, fullCount, "RUN12 3D");
        REX::INFO("RUN12 PLAYER 3D TREE END nodes={}", fullCount);

        REX::INFO(
            "RUN12 DIAGNOSTIC COMPLETE. No HeadPart, FaceGen, texture, mesh path, or rendered node was modified.");
        return true;
    }

    void QueueDump(std::uint32_t a_attempts)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            DumpPlayerHeadState();
            return;
        }

        tasks->AddTask([a_attempts]() {
            if (!DumpPlayerHeadState() && a_attempts > 1) {
                QueueDump(a_attempts - 1);
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
            QueueDump(30);
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

    REX::INFO("ABDOSAPlayerHead RUN12 loaded; SAFE rendered-head diagnostics only");
    return true;
}
