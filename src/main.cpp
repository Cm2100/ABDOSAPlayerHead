#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    constexpr auto kCustomFace = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\BaseFemaleHead_faceBones.nif";
    constexpr auto kCustomRear = "Actors\\Character\\CharacterAssets\\ABDOSAPlayerHead\\FemaleheadRear_faceBones.nif";
    constexpr auto kVanillaFace = "Actors\\Character\\CharacterAssets\\BaseFemaleHead_faceBones.nif";
    constexpr auto kVanillaRear = "Actors\\Character\\CharacterAssets\\FaceParts\\FemaleheadRear_faceBones.nif";
    constexpr auto kTargetFaceName = "FemaleHeadHuman";
    constexpr auto kTargetRearName = "FemaleHeadHumanRearTEMP";
    constexpr auto kDynamicRTTIName = "BSDynamicTriShape";

    constexpr std::ptrdiff_t kOffNumVertices = 0x164;
    constexpr std::ptrdiff_t kOffDynamicDataSize = 0x170;
    constexpr std::ptrdiff_t kOffDynamicVertices = 0x180;
    constexpr std::uint32_t kExpectedDynamicStride = 12;

    struct DynamicView
    {
        std::uint8_t* data{ nullptr };
        std::uint32_t size{ 0 };
        std::uint16_t vertices{ 0 };
    };

    using Delta3 = std::array<float, 3>;

    std::atomic_bool g_loggedFailure{ false };
    std::atomic_bool g_armForLoad{ false };
    std::atomic_bool g_taskQueued{ false };
    std::atomic_bool g_appliedThisLoad{ false };
    std::atomic_bool g_menuSinkRegistered{ false };

    RE::NiPointer<RE::NiNode> g_customFaceRoot;
    RE::NiPointer<RE::NiNode> g_customRearRoot;
    RE::NiPointer<RE::NiNode> g_vanillaFaceRoot;
    RE::NiPointer<RE::NiNode> g_vanillaRearRoot;

    std::vector<Delta3> g_faceDelta;
    std::vector<Delta3> g_rearDelta;

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

    [[nodiscard]] DynamicView GetDynamicView(RE::NiAVObject* a_object)
    {
        DynamicView out{};
        if (!IsDynamicTriShape(a_object)) {
            return out;
        }
        auto* base = reinterpret_cast<std::uint8_t*>(a_object);
        out.vertices = *reinterpret_cast<std::uint16_t*>(base + kOffNumVertices);
        out.size = *reinterpret_cast<std::uint32_t*>(base + kOffDynamicDataSize);
        out.data = *reinterpret_cast<std::uint8_t**>(base + kOffDynamicVertices);
        if (!out.data || out.vertices == 0 || out.size != static_cast<std::uint32_t>(out.vertices) * kExpectedDynamicStride) {
            return {};
        }
        return out;
    }

    [[nodiscard]] std::uint64_t HashBytes(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (std::uint32_t i = 0; i < a_size; ++i) {
            hash ^= a_data[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] float HalfToFloat(std::uint16_t h)
    {
        const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
        std::uint32_t exp = (h >> 10) & 0x1Fu;
        std::uint32_t mant = h & 0x03FFu;
        std::uint32_t bits = 0;

        if (exp == 0) {
            if (mant == 0) {
                bits = sign;
            } else {
                exp = 1;
                while ((mant & 0x0400u) == 0) {
                    mant <<= 1;
                    --exp;
                }
                mant &= 0x03FFu;
                const std::uint32_t exp32 = exp + (127u - 15u);
                bits = sign | (exp32 << 23) | (mant << 13);
            }
        } else if (exp == 31) {
            bits = sign | 0x7F800000u | (mant << 13);
        } else {
            const std::uint32_t exp32 = exp + (127u - 15u);
            bits = sign | (exp32 << 23) | (mant << 13);
        }
        return std::bit_cast<float>(bits);
    }

    [[nodiscard]] std::uint16_t FloatToHalf(float f)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
        const std::uint32_t sign = (bits >> 16) & 0x8000u;
        const std::uint32_t exp = (bits >> 23) & 0xFFu;
        const std::uint32_t mant = bits & 0x7FFFFFu;

        if (exp == 255) {
            return static_cast<std::uint16_t>(sign | (mant ? 0x7E00u : 0x7C00u));
        }

        const int newExp = static_cast<int>(exp) - 127 + 15;
        if (newExp >= 31) {
            return static_cast<std::uint16_t>(sign | 0x7C00u);
        }
        if (newExp <= 0) {
            if (newExp < -10) {
                return static_cast<std::uint16_t>(sign);
            }
            std::uint32_t m = mant | 0x800000u;
            const int shift = 14 - newExp;
            std::uint32_t halfMant = m >> shift;
            if ((m >> (shift - 1)) & 1u) {
                ++halfMant;
            }
            return static_cast<std::uint16_t>(sign | (halfMant & 0x03FFu));
        }

        std::uint32_t halfMant = mant >> 13;
        if (mant & 0x00001000u) {
            ++halfMant;
            if (halfMant & 0x0400u) {
                halfMant = 0;
                if (newExp + 1 >= 31) {
                    return static_cast<std::uint16_t>(sign | 0x7C00u);
                }
                return static_cast<std::uint16_t>(sign | ((newExp + 1) << 10));
            }
        }
        return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(newExp) << 10) | (halfMant & 0x03FFu));
    }

    [[nodiscard]] bool LoadFaceGenModel(const char* a_path, RE::NiPointer<RE::NiNode>& a_root)
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
        args.loadTextures = 0;

        const auto result = RE::BSModelDB::Demand(a_path, std::addressof(a_root), args);
        if (result == RE::BSResource::ErrorCode::kBusy) {
            return false;
        }
        if (result != RE::BSResource::ErrorCode::kNone || !a_root) {
            if (!g_loggedFailure.exchange(true)) {
                REX::ERROR("RUN17 model load failed. path={} error={} root={}",
                    a_path, static_cast<std::uint32_t>(result), static_cast<const void*>(a_root.get()));
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] bool BuildDelta(RE::NiAVObject* a_custom, RE::NiAVObject* a_vanilla, std::vector<Delta3>& a_out, const char* a_label)
    {
        const auto custom = GetDynamicView(a_custom);
        const auto vanilla = GetDynamicView(a_vanilla);
        if (!custom.data || !vanilla.data || custom.vertices != vanilla.vertices || custom.size != vanilla.size) {
            REX::ERROR("RUN17 {} incompatible dynamic geometry. customVerts={} customSize={} vanillaVerts={} vanillaSize={}",
                a_label, custom.vertices, custom.size, vanilla.vertices, vanilla.size);
            return false;
        }

        a_out.resize(custom.vertices);
        float maxDelta = 0.0f;
        double sumDelta = 0.0;
        for (std::uint16_t i = 0; i < custom.vertices; ++i) {
            const auto* c = custom.data + static_cast<std::uint32_t>(i) * kExpectedDynamicStride;
            const auto* v = vanilla.data + static_cast<std::uint32_t>(i) * kExpectedDynamicStride;
            for (int axis = 0; axis < 3; ++axis) {
                const auto ch = *reinterpret_cast<const std::uint16_t*>(c + axis * 2);
                const auto vh = *reinterpret_cast<const std::uint16_t*>(v + axis * 2);
                const float d = HalfToFloat(ch) - HalfToFloat(vh);
                a_out[i][axis] = d;
                maxDelta = (std::max)(maxDelta, std::abs(d));
                sumDelta += std::abs(d);
            }
        }

        REX::INFO("RUN17 {} delta ready. vertices={} bytes={} maxAbsDelta={} meanAbsDelta={} customHash={:016X} vanillaHash={:016X}",
            a_label, custom.vertices, custom.size, maxDelta,
            static_cast<float>(sumDelta / (static_cast<double>(custom.vertices) * 3.0)),
            HashBytes(custom.data, custom.size), HashBytes(vanilla.data, vanilla.size));

        if (maxDelta < 0.0001f) {
            REX::ERROR("RUN17 {} custom-vs-vanilla delta is effectively ZERO; refusing to patch", a_label);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool PrepareDeltas()
    {
        if (!g_faceDelta.empty() && !g_rearDelta.empty()) {
            return true;
        }

        if (!LoadFaceGenModel(kCustomFace, g_customFaceRoot) ||
            !LoadFaceGenModel(kCustomRear, g_customRearRoot) ||
            !LoadFaceGenModel(kVanillaFace, g_vanillaFaceRoot) ||
            !LoadFaceGenModel(kVanillaRear, g_vanillaRearRoot)) {
            return false;
        }

        auto* customFace = FindFirstDynamicShape(g_customFaceRoot.get());
        auto* customRear = FindFirstDynamicShape(g_customRearRoot.get());
        auto* vanillaFace = FindFirstDynamicShape(g_vanillaFaceRoot.get());
        auto* vanillaRear = FindFirstDynamicShape(g_vanillaRearRoot.get());
        if (!customFace || !customRear || !vanillaFace || !vanillaRear) {
            if (!g_loggedFailure.exchange(true)) {
                REX::ERROR("RUN17 could not resolve all four dynamic source geometries");
            }
            return false;
        }

        std::vector<Delta3> faceDelta;
        std::vector<Delta3> rearDelta;
        if (!BuildDelta(customFace, vanillaFace, faceDelta, "FACE") ||
            !BuildDelta(customRear, vanillaRear, rearDelta, "REAR")) {
            return false;
        }

        g_faceDelta = std::move(faceDelta);
        g_rearDelta = std::move(rearDelta);
        return true;
    }

    [[nodiscard]] bool PatchOneOnce(RE::NiAVObject* a_target, const std::vector<Delta3>& a_delta, const char* a_label)
    {
        auto view = GetDynamicView(a_target);
        if (!view.data || view.vertices != a_delta.size()) {
            REX::ERROR("RUN17 {} target mismatch. targetVerts={} targetSize={} deltaVerts={}",
                a_label, view.vertices, view.size, a_delta.size());
            return false;
        }

        const auto beforeHash = HashBytes(view.data, view.size);
        for (std::uint16_t i = 0; i < view.vertices; ++i) {
            auto* p = view.data + static_cast<std::uint32_t>(i) * kExpectedDynamicStride;
            for (int axis = 0; axis < 3; ++axis) {
                auto* h = reinterpret_cast<std::uint16_t*>(p + axis * 2);
                const float current = HalfToFloat(*h);
                *h = FloatToHalf(current + a_delta[i][axis]);
            }
        }
        const auto afterHash = HashBytes(view.data, view.size);
        REX::INFO("RUN17 {} one-shot patch. before={:016X} after={:016X}", a_label, beforeHash, afterHash);
        return beforeHash != afterHash;
    }

    [[nodiscard]] bool ApplyPlayerDeltaOnce()
    {
        if (g_appliedThisLoad.load(std::memory_order_acquire)) {
            return true;
        }
        if (!PrepareDeltas()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* rawFaceNode = player ? player->GetFaceNodeSkinned() : nullptr;
        if (!player || !player->Get3D() || !rawFaceNode) {
            REX::INFO("RUN17 player FaceGen node not ready at one-shot trigger");
            return false;
        }

        auto* faceNode = reinterpret_cast<RE::NiNode*>(rawFaceNode);
        auto* targetFace = faceNode->GetObjectByName(RE::BSFixedString(kTargetFaceName));
        auto* targetRear = faceNode->GetObjectByName(RE::BSFixedString(kTargetRearName));
        if (!targetFace || !targetRear || !IsDynamicTriShape(targetFace) || !IsDynamicTriShape(targetRear)) {
            REX::INFO("RUN17 player dynamic head shapes not ready at one-shot trigger");
            return false;
        }

        const bool faceOK = PatchOneOnce(targetFace, g_faceDelta, "FACE");
        const bool rearOK = PatchOneOnce(targetRear, g_rearDelta, "REAR");
        if (!faceOK || !rearOK) {
            return false;
        }

        g_appliedThisLoad.store(true, std::memory_order_release);
        REX::INFO("RUN17 PLAYER one-shot in-place sculpt delta APPLIED. face={} rear={}",
            static_cast<const void*>(targetFace), static_cast<const void*>(targetRear));
        return true;
    }

    void QueueOneShot(const char* a_reason)
    {
        if (!g_armForLoad.load(std::memory_order_acquire) || g_appliedThisLoad.load(std::memory_order_acquire)) {
            return;
        }
        if (g_taskQueued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            g_taskQueued.store(false, std::memory_order_release);
            return;
        }

        REX::INFO("RUN17 queueing ONE task after {}", a_reason ? a_reason : "menu event");
        tasks->AddTask([]() {
            const bool ok = ApplyPlayerDeltaOnce();
            g_taskQueued.store(false, std::memory_order_release);
            if (ok) {
                g_armForLoad.store(false, std::memory_order_release);
            } else {
                REX::INFO("RUN17 one-shot was not ready; no recursion/requeue performed");
            }
        });
    }

    class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent& a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!g_armForLoad.load(std::memory_order_acquire) || g_appliedThisLoad.load(std::memory_order_acquire)) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto* name = a_event.menuName.c_str();
            if (!name) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // Primary trigger: world loading screen has closed.
            if (!a_event.opening && _stricmp(name, "LoadingMenu") == 0) {
                QueueOneShot("LoadingMenu close");
            }
            // Fallback trigger if the first task ran slightly too early.
            else if (a_event.opening && _stricmp(name, "HUDMenu") == 0) {
                QueueOneShot("HUDMenu open");
            }
            // Manual safe retry opportunity without any loop.
            else if (!a_event.opening && (_stricmp(name, "PauseMenu") == 0 || _stricmp(name, "PipboyMenu") == 0)) {
                QueueOneShot("later menu close");
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    MenuSink g_menuSink;

    void RegisterMenuSink()
    {
        if (g_menuSinkRegistered.load(std::memory_order_acquire)) {
            return;
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&g_menuSink);
            g_menuSinkRegistered.store(true, std::memory_order_release);
            REX::INFO("RUN17 MenuOpenCloseEvent sink registered");
        }
    }

    void ArmForCurrentLoad()
    {
        g_loggedFailure.store(false, std::memory_order_release);
        g_taskQueued.store(false, std::memory_order_release);
        g_appliedThisLoad.store(false, std::memory_order_release);
        g_armForLoad.store(true, std::memory_order_release);
        REX::INFO("RUN17 armed; waiting for LoadingMenu close/HUDMenu open. No task loop exists.");
    }

    void MessageHandler(F4SE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        switch (a_message->type) {
        case F4SE::MessagingInterface::kGameDataReady:
            RegisterMenuSink();
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
        case F4SE::MessagingInterface::kNewGame:
            RegisterMenuSink();
            ArmForCurrentLoad();
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
    REX::INFO("ABDOSAPlayerHead RUN17 loaded; event-driven ONE-SHOT player FaceGen delta. No recursive task scheduling.");
    return true;
}
