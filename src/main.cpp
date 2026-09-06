#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";
    constexpr auto kTargetFaceName = "FemaleHeadHuman";
    constexpr auto kTargetRearName = "FemaleHeadHumanRearTEMP";
    constexpr auto kDynamicRTTIName = "BSDynamicTriShape";

    std::atomic_bool g_appliedThisLoad{ false };
    std::atomic_bool g_loggedSourceFailure{ false };

    RE::NiPointer<RE::NiNode> g_faceSourceRoot;
    RE::NiPointer<RE::NiNode> g_rearSourceRoot;
    RE::NiPointer<RE::NiAVObject> g_faceSourceShape;
    RE::NiPointer<RE::NiAVObject> g_rearSourceShape;
    RE::NiPointer<RE::NiAVObject> g_oldPlayerFaceShape;
    RE::NiPointer<RE::NiAVObject> g_oldPlayerRearShape;

    [[nodiscard]] const char* RTTIName(RE::NiAVObject* a_object)
    {
        if (!a_object) {
            return "<null>";
        }
        const auto* rtti = a_object->GetRTTI();
        return rtti && rtti->GetName() ? rtti->GetName() : "<no-rtti>";
    }

    [[nodiscard]] bool IsDynamicTriShape(RE::NiAVObject* a_object)
    {
        return a_object && _stricmp(RTTIName(a_object), kDynamicRTTIName) == 0;
    }

    RE::NiAVObject* FindFirstDynamicShape(RE::NiAVObject* a_object)
    {
        if (!a_object) {
            return nullptr;
        }
        if (IsDynamicTriShape(a_object)) {
            return a_object;
        }

        auto* node = netimmerse_cast<RE::NiNode*>(a_object);
        if (!node) {
            return nullptr;
        }

        for (auto& child : node->children) {
            if (child) {
                if (auto* found = FindFirstDynamicShape(child.get())) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool LoadPrivateFaceGenModel(const char* a_path, RE::NiPointer<RE::NiNode>& a_root)
    {
        if (a_root) {
            return true;
        }

        RE::BSModelDB::DBTraits::ArgsType args{};
        args.loadLevel = 0;
        args.prepareAfterLoad = 1;
        args.faceGenModel = 1;
        args.useErrorMarker = 0;
        args.performProcess = 1;
        args.createFadeNode = 0;
        args.loadTextures = 1;

        const auto result = RE::BSModelDB::Demand(a_path, std::addressof(a_root), args);
        if (result == RE::BSResource::ErrorCode::kBusy) {
            return false;
        }
        if (result != RE::BSResource::ErrorCode::kNone || !a_root) {
            if (!g_loggedSourceFailure.exchange(true)) {
                REX::ERROR(
                    "RUN15 failed to load private FaceGen model. path={} error={} root={}",
                    a_path,
                    static_cast<std::uint32_t>(result),
                    static_cast<const void*>(a_root.get()));
            }
            return false;
        }

        REX::INFO(
            "RUN15 private FaceGen model loaded. path={} root={} rootType={}",
            a_path,
            static_cast<const void*>(a_root.get()),
            RTTIName(a_root.get()));
        return true;
    }

    [[nodiscard]] bool FindDirectChildIndex(RE::NiNode* a_parent, RE::NiAVObject* a_child, std::uint16_t& a_index)
    {
        if (!a_parent || !a_child) {
            return false;
        }
        for (std::uint16_t i = 0; i < a_parent->children.capacity(); ++i) {
            auto& slot = a_parent->children[i];
            if (slot && slot.get() == a_child) {
                a_index = i;
                return true;
            }
        }
        return false;
    }

    void CopyPlayerBindingAndMaterial(RE::NiAVObject* a_source, RE::NiAVObject* a_target)
    {
        a_source->name = a_target->name;
        a_source->local = a_target->local;
        a_source->world = a_target->world;
        a_source->previousWorld = a_target->previousWorld;
        a_source->worldBound = a_target->worldBound;
        a_source->flags = a_target->flags;
        a_source->userData = a_target->userData;
        a_source->fadeAmount = a_target->fadeAmount;
        a_source->multType = a_target->multType;
        a_source->meshLODFadingLevel = a_target->meshLODFadingLevel;
        a_source->currentMeshLODLevel = a_target->currentMeshLODLevel;
        a_source->previousMeshLODLevel = a_target->previousMeshLODLevel;

        auto* sourceGeometry = netimmerse_cast<RE::BSGeometry*>(a_source);
        auto* targetGeometry = netimmerse_cast<RE::BSGeometry*>(a_target);
        if (!sourceGeometry || !targetGeometry) {
            return;
        }

        sourceGeometry->properties[0] = targetGeometry->properties[0];
        sourceGeometry->properties[1] = targetGeometry->properties[1];
        sourceGeometry->skinInstance = targetGeometry->skinInstance;
        sourceGeometry->registered = targetGeometry->registered;
    }

    [[nodiscard]] bool TryGraftPlayerRenderedHead()
    {
        if (g_appliedThisLoad.load(std::memory_order_acquire)) {
            return true;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* rawFaceNode = player ? player->GetFaceNodeSkinned() : nullptr;
        if (!player || !player->Get3D() || !rawFaceNode) {
            return false;
        }

        // CommonLibF4 forward-declares BSFaceGenNiNode here; its runtime base is NiNode.
        // Work only through the NiNode interface so no FacePart/FaceGen record is touched.
        auto* faceNode = reinterpret_cast<RE::NiNode*>(rawFaceNode);
        auto* targetFace = faceNode->GetObjectByName(RE::BSFixedString(kTargetFaceName));
        auto* targetRear = faceNode->GetObjectByName(RE::BSFixedString(kTargetRearName));
        if (!targetFace || !targetRear) {
            return false;
        }

        if (!IsDynamicTriShape(targetFace) || !IsDynamicTriShape(targetRear)) {
            REX::ERROR(
                "RUN15 player target shapes are not dynamic. face={} type={} rear={} type={}",
                static_cast<const void*>(targetFace), RTTIName(targetFace),
                static_cast<const void*>(targetRear), RTTIName(targetRear));
            return false;
        }

        if (!LoadPrivateFaceGenModel(kCustomFace, g_faceSourceRoot) ||
            !LoadPrivateFaceGenModel(kCustomRear, g_rearSourceRoot)) {
            return false;
        }

        auto* sourceFace = FindFirstDynamicShape(g_faceSourceRoot.get());
        auto* sourceRear = FindFirstDynamicShape(g_rearSourceRoot.get());
        if (!sourceFace || !sourceRear) {
            if (!g_loggedSourceFailure.exchange(true)) {
                REX::ERROR(
                    "RUN15 private models did not produce BSDynamicTriShape. faceSource={} rearSource={}",
                    static_cast<const void*>(sourceFace),
                    static_cast<const void*>(sourceRear));
            }
            return false;
        }

        std::uint16_t faceIndex = 0;
        std::uint16_t rearIndex = 0;
        if (!FindDirectChildIndex(faceNode, targetFace, faceIndex) ||
            !FindDirectChildIndex(faceNode, targetRear, rearIndex)) {
            REX::ERROR("RUN15 target face/rear were not direct children of the player's FaceGen node");
            return false;
        }

        g_faceSourceShape.reset(sourceFace);
        g_rearSourceShape.reset(sourceRear);
        g_oldPlayerFaceShape.reset(targetFace);
        g_oldPlayerRearShape.reset(targetRear);

        CopyPlayerBindingAndMaterial(sourceFace, targetFace);
        CopyPlayerBindingAndMaterial(sourceRear, targetRear);

        if (sourceFace->parent) {
            sourceFace->parent->DetachChild(sourceFace);
        }
        if (sourceRear->parent) {
            sourceRear->parent->DetachChild(sourceRear);
        }

        faceNode->SetAt(faceIndex, sourceFace);
        faceNode->SetAt(rearIndex, sourceRear);

        sourceFace->PostAttachUpdate();
        sourceRear->PostAttachUpdate();
        sourceFace->UpdateWorldBound();
        sourceRear->UpdateWorldBound();

        g_appliedThisLoad.store(true, std::memory_order_release);

        REX::INFO(
            "RUN15 PLAYER rendered-head graft APPLIED. faceNode={} faceIndex={} oldFace={} newFace={} rearIndex={} oldRear={} newRear={}",
            static_cast<const void*>(faceNode), faceIndex,
            static_cast<const void*>(targetFace), static_cast<const void*>(sourceFace),
            rearIndex,
            static_cast<const void*>(targetRear), static_cast<const void*>(sourceRear));
        REX::INFO(
            "RUN15 types: newFaceType={} newRearType={}",
            RTTIName(sourceFace), RTTIName(sourceRear));
        REX::INFO("RUN15 did not modify HeadPart records, FaceGen texture paths, materials on disk, or any NPC actor.");
        return true;
    }

    void QueueApply(std::uint32_t a_attempts)
    {
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            return;
        }
        tasks->AddTask([a_attempts]() {
            if (TryGraftPlayerRenderedHead()) {
                return;
            }
            if (a_attempts > 1) {
                QueueApply(a_attempts - 1);
            } else {
                REX::ERROR("RUN15 timed out waiting for the player's final female FaceGen node");
            }
        });
    }

    void ResetForLoad()
    {
        g_appliedThisLoad.store(false, std::memory_order_release);
        g_loggedSourceFailure.store(false, std::memory_order_release);
        g_faceSourceShape.reset();
        g_rearSourceShape.reset();
        g_oldPlayerFaceShape.reset();
        g_oldPlayerRearShape.reset();
        g_faceSourceRoot.reset();
        g_rearSourceRoot.reset();
        QueueApply(1800);
    }

    void MessageHandler(F4SE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }
        switch (a_message->type) {
        case F4SE::MessagingInterface::kPostLoadGame:
        case F4SE::MessagingInterface::kNewGame:
            ResetForLoad();
            break;
        default:
            break;
        }
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    const auto messaging = F4SE::GetMessagingInterface();
    const auto tasks = F4SE::GetTaskInterface();
    if (!messaging || !tasks) {
        return false;
    }

    messaging->RegisterListener(MessageHandler);
    REX::INFO(
        "ABDOSAPlayerHead RUN15 loaded; POST-BUILD PLAYER rendered-head graft. No BSModelDB detour, no HeadPart duplication, no texture-path edits.");
    return true;
}
