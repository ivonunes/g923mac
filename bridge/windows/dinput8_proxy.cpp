#include "bridge_client.hpp"
#include "ffb_bridge_protocol.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dinput.h>
#include <windows.h>

void* operator new(std::size_t size) {
    return HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size));
}

void operator delete(void* pointer) noexcept {
    if (pointer) {
        HeapFree(GetProcessHeap(), 0, pointer);
    }
}

void operator delete(void* pointer, std::size_t) noexcept {
    if (pointer) {
        HeapFree(GetProcessHeap(), 0, pointer);
    }
}

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DllCanUnloadNowFn = HRESULT(WINAPI*)();
using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using DllRegisterServerFn = HRESULT(WINAPI*)();
using DllUnregisterServerFn = HRESULT(WINAPI*)();

constexpr int kMaxEffects = 64;
constexpr DWORD kSyntheticDrivingType = DI8DEVTYPE_DRIVING | (DI8DEVTYPEDRIVING_THREEPEDALS << 8);
constexpr ULONGLONG kPeriodicUpdateIntervalUs = 4000ULL;
constexpr double kTwoPi = 6.28318530717958647692;

HMODULE g_real_dinput8 = nullptr;
HMODULE g_this_module = nullptr;
DirectInput8CreateFn g_real_create = nullptr;
DllCanUnloadNowFn g_real_can_unload = nullptr;
DllGetClassObjectFn g_real_get_class_object = nullptr;
DllRegisterServerFn g_real_register_server = nullptr;
DllUnregisterServerFn g_real_unregister_server = nullptr;
BridgeClient g_bridge_client;
volatile LONG g_bridge_announced = 0;

inline LONG abs_long(LONG value) {
    return (value < 0) ? -value : value;
}

inline LONG clamp_long(LONG value, LONG minimum, LONG maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

inline DWORD max_dword(DWORD a, DWORD b) {
    return (a > b) ? a : b;
}

inline std::uint8_t max_u8(std::uint8_t a, std::uint8_t b) {
    return (a > b) ? a : b;
}

inline DWORD clamp_dword(DWORD value, DWORD minimum, DWORD maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

inline DWORD min_dword(DWORD a, DWORD b) {
    return (a < b) ? a : b;
}

inline int min_int(int a, int b) {
    return (a < b) ? a : b;
}

inline bool is_game_controller_type(DWORD dev_type) {
    const DWORD base_type = GET_DIDEVICE_TYPE(dev_type);
    return base_type == DI8DEVTYPE_JOYSTICK || base_type == DI8DEVTYPE_GAMEPAD || base_type == DI8DEVTYPE_DRIVING ||
           base_type == DIDEVTYPE_JOYSTICK;
}

bool is_guid_equal(REFGUID a, REFGUID b) {
    return InlineIsEqualGUID(a, b) != FALSE;
}

const char* effect_guid_name(REFGUID guid) {
    if (is_guid_equal(guid, GUID_ConstantForce)) {
        return "ConstantForce";
    }
    if (is_guid_equal(guid, GUID_RampForce)) {
        return "RampForce";
    }
    if (is_guid_equal(guid, GUID_Square)) {
        return "Square";
    }
    if (is_guid_equal(guid, GUID_Sine)) {
        return "Sine";
    }
    if (is_guid_equal(guid, GUID_Triangle)) {
        return "Triangle";
    }
    if (is_guid_equal(guid, GUID_SawtoothUp)) {
        return "SawtoothUp";
    }
    if (is_guid_equal(guid, GUID_SawtoothDown)) {
        return "SawtoothDown";
    }
    if (is_guid_equal(guid, GUID_Spring)) {
        return "Spring";
    }
    if (is_guid_equal(guid, GUID_Damper)) {
        return "Damper";
    }
    if (is_guid_equal(guid, GUID_Inertia)) {
        return "Inertia";
    }
    if (is_guid_equal(guid, GUID_Friction)) {
        return "Friction";
    }
    return "Unknown";
}

bool is_property_key(REFGUID prop, ULONG_PTR key) {
    return reinterpret_cast<ULONG_PTR>(&prop) == key;
}

void append_proxy_log(const char* message) {
    char module_path[MAX_PATH] = {0};
    if (!g_this_module || GetModuleFileNameA(g_this_module, module_path, MAX_PATH) == 0) {
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        return;
    }

    char* separator = std::strrchr(module_path, '\\');
    if (!separator) {
        separator = std::strrchr(module_path, '/');
    }
    if (separator) {
        separator[1] = '\0';
    } else {
        module_path[0] = '\0';
    }

    std::strncat(module_path, "g923mac_proxy.log", MAX_PATH - std::strlen(module_path) - 1);

    const DWORD attrs = GetFileAttributesA(module_path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        HANDLE file = CreateFileA(
            module_path,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file != INVALID_HANDLE_VALUE) {
            char line[1024] = {0};
            std::snprintf(line, sizeof(line), "%s\r\n", message);
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
            CloseHandle(file);
        }
    }

    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

void append_proxy_logf(const char* format, ...) {
    char buffer[1024] = {0};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    append_proxy_log(buffer);
}

const char* directinput_iid_name(REFIID riid) {
    if (InlineIsEqualGUID(riid, IID_IDirectInput8W) != FALSE) {
        return "IDirectInput8W";
    }
    if (InlineIsEqualGUID(riid, IID_IDirectInput8A) != FALSE) {
        return "IDirectInput8A";
    }
    if (InlineIsEqualGUID(riid, IID_IUnknown) != FALSE) {
        return "IUnknown";
    }
    return "other";
}

void announce_bridge_connection() {
    if (InterlockedCompareExchange(&g_bridge_announced, 1, 0) != 0) {
        return;
    }

    const bool connected = g_bridge_client.send_hello("G923FFBProxy", GetCurrentProcessId());
    append_proxy_logf("bridge hello %s", connected ? "succeeded" : "failed");
}

bool ensure_real_dinput_loaded() {
    if (g_real_dinput8) {
        return true;
    }

    wchar_t system_dir[MAX_PATH] = {0};
    if (GetSystemDirectoryW(system_dir, MAX_PATH) == 0) {
        append_proxy_log("GetSystemDirectoryW failed");
        return false;
    }

    wchar_t dll_path[MAX_PATH] = {0};
    lstrcpynW(dll_path, system_dir, MAX_PATH);
    lstrcatW(dll_path, L"\\dinput8.dll");
    g_real_dinput8 = LoadLibraryW(dll_path);
    if (!g_real_dinput8) {
        append_proxy_log("LoadLibraryW(system dinput8.dll) failed");
        return false;
    }

    const FARPROC create_proc = GetProcAddress(g_real_dinput8, "DirectInput8Create");
    const FARPROC can_unload_proc = GetProcAddress(g_real_dinput8, "DllCanUnloadNow");
    const FARPROC get_class_object_proc = GetProcAddress(g_real_dinput8, "DllGetClassObject");
    const FARPROC register_server_proc = GetProcAddress(g_real_dinput8, "DllRegisterServer");
    const FARPROC unregister_server_proc = GetProcAddress(g_real_dinput8, "DllUnregisterServer");

    std::memcpy(&g_real_create, &create_proc, sizeof(g_real_create));
    std::memcpy(&g_real_can_unload, &can_unload_proc, sizeof(g_real_can_unload));
    std::memcpy(&g_real_get_class_object, &get_class_object_proc, sizeof(g_real_get_class_object));
    std::memcpy(&g_real_register_server, &register_server_proc, sizeof(g_real_register_server));
    std::memcpy(&g_real_unregister_server, &unregister_server_proc, sizeof(g_real_unregister_server));

    const bool loaded = g_real_create != nullptr;
    append_proxy_logf("real dinput8 load %s", loaded ? "succeeded" : "failed");
    return loaded;
}

std::uint8_t scale_byte(LONG value, LONG source_max = DI_FFNOMINALMAX) {
    LONG clamped = value;
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > source_max) {
        clamped = source_max;
    }
    return static_cast<std::uint8_t>((clamped * 255) / source_max);
}

std::uint8_t scale_nibble(LONG value, LONG source_max = DI_FFNOMINALMAX) {
    LONG clamped = value;
    if (clamped < 0) {
        clamped = 0;
    }
    if (clamped > source_max) {
        clamped = source_max;
    }
    return static_cast<std::uint8_t>((clamped * 15) / source_max);
}

ULONGLONG now_us() {
    return static_cast<ULONGLONG>(GetTickCount64()) * 1000ULL;
}

DWORD apply_unsigned_gain(DWORD value, DWORD gain) {
    const DWORD clamped_gain = clamp_dword(gain, 0, DI_FFNOMINALMAX);
    const ULONGLONG scaled = (static_cast<ULONGLONG>(value) * static_cast<ULONGLONG>(clamped_gain)) /
                             static_cast<ULONGLONG>(DI_FFNOMINALMAX);
    return static_cast<DWORD>(scaled);
}

LONG apply_signed_gain(LONG value, DWORD gain) {
    const DWORD clamped_gain = clamp_dword(gain, 0, DI_FFNOMINALMAX);
    const LONGLONG scaled = (static_cast<LONGLONG>(value) * static_cast<LONGLONG>(clamped_gain)) /
                            static_cast<LONGLONG>(DI_FFNOMINALMAX);
    return clamp_long(static_cast<LONG>(scaled), -DI_FFNOMINALMAX, DI_FFNOMINALMAX);
}

LONG apply_combined_gain(LONG value, DWORD effect_gain, DWORD device_gain) {
    const LONG after_effect = apply_signed_gain(value, effect_gain);
    return apply_signed_gain(after_effect, device_gain);
}

DWORD apply_combined_gain_unsigned(DWORD value, DWORD effect_gain, DWORD device_gain) {
    return apply_unsigned_gain(apply_unsigned_gain(value, effect_gain), device_gain);
}

double normalize_phase01(double phase) {
    double result = std::fmod(phase, 1.0);
    if (result < 0.0) {
        result += 1.0;
    }
    return result;
}

double periodic_wave_sample(REFGUID guid, double phase01) {
    const double normalized = normalize_phase01(phase01);
    if (is_guid_equal(guid, GUID_Sine)) {
        return std::sin(normalized * kTwoPi);
    }
    if (is_guid_equal(guid, GUID_Square)) {
        return (normalized < 0.5) ? 1.0 : -1.0;
    }
    if (is_guid_equal(guid, GUID_Triangle)) {
        return 1.0 - 4.0 * std::abs(normalized - 0.5);
    }
    if (is_guid_equal(guid, GUID_SawtoothUp)) {
        return (2.0 * normalized) - 1.0;
    }
    if (is_guid_equal(guid, GUID_SawtoothDown)) {
        return 1.0 - (2.0 * normalized);
    }
    return std::sin(normalized * kTwoPi);
}

struct EnumObjectContext {
    LPDIENUMDEVICEOBJECTSCALLBACKW callback;
    LPVOID ref;
    DWORD requested_flags;
    bool actuator_only;
    bool actuator_emitted;
};

struct EnumDeviceContext {
    LPDIENUMDEVICESCALLBACKW callback;
    LPVOID ref;
};

struct SupportedEffectDefinition {
    const GUID* guid;
    DWORD type_flags;
    DWORD static_params;
    DWORD dynamic_params;
    const wchar_t* name;
};

BOOL CALLBACK enum_objects_wrapper(LPCDIDEVICEOBJECTINSTANCEW instance, LPVOID ref) {
    auto* context = static_cast<EnumObjectContext*>(ref);
    if (!context || !context->callback || !instance) {
        return DIENUM_STOP;
    }

    DIDEVICEOBJECTINSTANCEW patched = *instance;
    const bool is_axis = (instance->dwType & DIDFT_AXIS) != 0;

    if (is_axis && !context->actuator_emitted) {
        patched.dwFlags |= DIDOI_FFACTUATOR;
        patched.dwFFMaxForce = DI_FFNOMINALMAX;
        patched.dwFFForceResolution = 1024;
        context->actuator_emitted = true;
    }

    if (context->actuator_only && (patched.dwFlags & DIDOI_FFACTUATOR) == 0) {
        return DIENUM_CONTINUE;
    }

    return context->callback(&patched, context->ref);
}

BOOL CALLBACK enum_devices_wrapper(LPCDIDEVICEINSTANCEW instance, LPVOID ref) {
    auto* context = static_cast<EnumDeviceContext*>(ref);
    if (!context || !context->callback || !instance) {
        return DIENUM_STOP;
    }

    DIDEVICEINSTANCEW patched = *instance;
    if (patched.dwSize >= sizeof(DIDEVICEINSTANCEW) && is_game_controller_type(patched.dwDevType)) {
        patched.guidFFDriver = CLSID_DirectInputDevice8;
        patched.dwDevType = kSyntheticDrivingType;
        append_proxy_log("EnumDevices injected guidFFDriver");
    }

    return context->callback(&patched, context->ref);
}

const SupportedEffectDefinition* supported_effects() {
    static const SupportedEffectDefinition kEffects[] = {
        {&GUID_ConstantForce, DIEFT_CONSTANTFORCE | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION, L"Constant Force"},
        {&GUID_RampForce, DIEFT_RAMPFORCE | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION, L"Ramp Force"},
        {&GUID_Square, DIEFT_PERIODIC | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION | DIEP_ENVELOPE,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION | DIEP_ENVELOPE, L"Square"},
        {&GUID_Sine, DIEFT_PERIODIC | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION | DIEP_ENVELOPE,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION | DIEP_ENVELOPE, L"Sine"},
        {&GUID_Triangle, DIEFT_PERIODIC | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION | DIEP_ENVELOPE,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION | DIEP_ENVELOPE, L"Triangle"},
        {&GUID_SawtoothUp, DIEFT_PERIODIC | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION | DIEP_ENVELOPE,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION | DIEP_ENVELOPE, L"Sawtooth Up"},
        {&GUID_SawtoothDown, DIEFT_PERIODIC | DIEFT_FFATTACK | DIEFT_FFFADE,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES | DIEP_DIRECTION | DIEP_ENVELOPE,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN | DIEP_DIRECTION | DIEP_ENVELOPE, L"Sawtooth Down"},
        {&GUID_Spring, DIEFT_CONDITION | DIEFT_POSNEGCOEFFICIENTS | DIEFT_POSNEGSATURATION | DIEFT_DEADBAND,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Spring"},
        {&GUID_Damper, DIEFT_CONDITION | DIEFT_POSNEGCOEFFICIENTS | DIEFT_POSNEGSATURATION,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Damper"},
        {&GUID_Inertia, DIEFT_CONDITION | DIEFT_POSNEGCOEFFICIENTS | DIEFT_POSNEGSATURATION,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Inertia"},
        {&GUID_Friction, DIEFT_CONDITION | DIEFT_POSNEGCOEFFICIENTS | DIEFT_POSNEGSATURATION,
         DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES,
         DIEP_START | DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Friction"},
    };
    return kEffects;
}

constexpr int supported_effect_count() {
    return 11;
}

bool effect_matches_type(const SupportedEffectDefinition& effect, DWORD requested_type) {
    if (requested_type == 0 || requested_type == DIEFT_ALL) {
        return true;
    }

    const DWORD requested_base_type = DIEFT_GETTYPE(requested_type);
    if (requested_base_type != 0 && DIEFT_GETTYPE(effect.type_flags) != requested_base_type) {
        return false;
    }

    const DWORD requested_flags = requested_type & ~0xFFu;
    return requested_flags == 0 || (effect.type_flags & requested_flags) == requested_flags;
}

HRESULT populate_effect_info(LPDIEFFECTINFOW info, const SupportedEffectDefinition& effect) {
    if (!info || info->dwSize < sizeof(DIEFFECTINFOW)) {
        return DIERR_INVALIDPARAM;
    }

    std::memset(info, 0, sizeof(DIEFFECTINFOW));
    info->dwSize = sizeof(DIEFFECTINFOW);
    info->guid = *effect.guid;
    info->dwEffType = effect.type_flags;
    info->dwStaticParams = effect.static_params;
    info->dwDynamicParams = effect.dynamic_params;
    lstrcpynW(info->tszName, effect.name, MAX_PATH);
    return DI_OK;
}

class DeviceProxy;

class EffectProxy final : public IDirectInputEffect {
public:
    EffectProxy(IDirectInputEffect* inner, REFGUID guid, DeviceProxy* owner);
    ~EffectProxy() = default;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override;

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version, REFGUID guid) override;
    HRESULT STDMETHODCALLTYPE GetEffectGuid(LPGUID guid) override;
    HRESULT STDMETHODCALLTYPE GetParameters(LPDIEFFECT effect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE SetParameters(LPCDIEFFECT effect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Start(DWORD iterations, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE GetEffectStatus(LPDWORD flags) override;
    HRESULT STDMETHODCALLTYPE Download() override;
    HRESULT STDMETHODCALLTYPE Unload() override;
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE escape) override;

    bool started() const noexcept { return started_; }
    bool has_time_varying_force() const;
    void refresh_runtime(ULONGLONG now);
    void force_stop_runtime();
    void apply(g923bridge::WheelStatePayload& payload, DWORD device_gain, ULONGLONG now) const;

private:
    void update_from_effect(LPCDIEFFECT effect, DWORD flags);
    bool is_temporally_active(ULONGLONG now) const;
    bool has_expired(ULONGLONG now) const;
    LONG compute_force(ULONGLONG now, DWORD device_gain) const;
    float direction_multiplier() const;
    float envelope_multiplier(ULONGLONG active_elapsed, ULONGLONG total_duration) const;

    volatile LONG ref_count_;
    IDirectInputEffect* inner_;
    DeviceProxy* owner_;
    GUID guid_;
    bool started_;
    DWORD iterations_;
    DWORD effect_gain_;
    DWORD duration_;
    DWORD start_delay_;
    DWORD direction_flags_;
    LONG direction_[2];
    bool envelope_enabled_;
    DIENVELOPE envelope_;
    ULONGLONG start_time_us_;
    DWORD condition_count_;
    DICONDITION conditions_[2];
    DICONSTANTFORCE constant_force_;
    DIPERIODIC periodic_force_;
    DIRAMPFORCE ramp_force_;
};

class DeviceProxy final : public IDirectInputDevice8W {
public:
    explicit DeviceProxy(IDirectInputDevice8W* inner);
    ~DeviceProxy() = default;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override;

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override;
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW callback, LPVOID ref, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID prop, LPDIPROPHEADER header) override;
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID prop, LPCDIPROPHEADER header) override;
    HRESULT STDMETHODCALLTYPE Acquire() override;
    HRESULT STDMETHODCALLTYPE Unacquire() override;
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override;
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD inout, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT format) override;
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE handle) override;
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW instance, DWORD object, DWORD how) override;
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW instance) override;
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND window, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version, REFGUID guid) override;
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID guid, LPCDIEFFECT effect, LPDIRECTINPUTEFFECT* out, LPUNKNOWN outer) override;
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW callback, LPVOID ref, DWORD type) override;
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW info, REFGUID guid) override;
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD out) override;
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD command) override;
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback, LPVOID ref, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE escape) override;
    HRESULT STDMETHODCALLTYPE Poll() override;
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD size, LPCDIDEVICEOBJECTDATA data, LPDWORD inout, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR file, LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID ref, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR file, DWORD entries, LPDIFILEEFFECT effects, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW format, LPCWSTR user, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW format, LPCWSTR user, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW header) override;

    void remove_effect(EffectProxy* effect);
    void rebuild_and_send();
    bool has_active_time_varying_effect() const;

private:
    volatile LONG ref_count_;
    IDirectInputDevice8W* inner_;
    EffectProxy* effects_[kMaxEffects];
    int effect_count_;
    DWORD ff_gain_;
    DWORD autocenter_mode_;
    DWORD ff_state_;
    bool advertises_force_feedback_;
    bool last_sent_has_state_;
    bool have_last_payload_;
    ULONGLONG last_periodic_rebuild_us_;
    g923bridge::WheelStatePayload last_payload_;
};

class DirectInputProxy final : public IDirectInput8W {
public:
    explicit DirectInputProxy(IDirectInput8W* inner) : ref_count_(1), inner_(inner) {}
    ~DirectInputProxy() = default;

    ULONG STDMETHODCALLTYPE AddRef() override {
        inner_->AddRef();
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        inner_->Release();
        const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* out) override {
        if (!out) {
            return E_POINTER;
        }

        if (is_guid_equal(riid, IID_IUnknown) ||
            is_guid_equal(riid, IID_IDirectInput8W) ||
            is_guid_equal(riid, IID_IDirectInput7W) ||
            is_guid_equal(riid, IID_IDirectInput2W)) {
            append_proxy_logf("DirectInputProxy::QueryInterface -> %s", directinput_iid_name(riid));
            *out = static_cast<IDirectInput8W*>(this);
            AddRef();
            return DI_OK;
        }

        return inner_->QueryInterface(riid, out);
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8W* out, LPUNKNOWN outer) override {
        if (!out) {
            return E_POINTER;
        }

        append_proxy_log("DirectInputProxy::CreateDevice called");
        IDirectInputDevice8W* device = nullptr;
        const HRESULT result = inner_->CreateDevice(guid, &device, outer);
        if (FAILED(result) || !device) {
            append_proxy_logf("CreateDevice failed: 0x%08lx", static_cast<unsigned long>(result));
            return result;
        }

        *out = new DeviceProxy(device);
        append_proxy_log("CreateDevice returning wrapped device");
        return result;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKW callback, LPVOID ref, DWORD flags) override {
        if (!callback) {
            return DIERR_INVALIDPARAM;
        }

        append_proxy_logf("DirectInputProxy::EnumDevices type=0x%08lx flags=0x%08lx",
                          static_cast<unsigned long>(type),
                          static_cast<unsigned long>(flags));

        EnumDeviceContext context{};
        context.callback = callback;
        context.ref = ref;
        return inner_->EnumDevices(type, enum_devices_wrapper, &context, flags);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guid) override { return inner_->GetDeviceStatus(guid); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND window, DWORD flags) override { return inner_->RunControlPanel(window, flags); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version) override { return inner_->Initialize(instance, version); }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID guid, LPCWSTR name, LPGUID out) override { return inner_->FindDevice(guid, name, out); }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
        LPCWSTR user, LPDIACTIONFORMATW format, LPDIENUMDEVICESBYSEMANTICSCBW callback, LPVOID ref, DWORD flags) override {
        return inner_->EnumDevicesBySemantics(user, format, callback, ref, flags);
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK callback, LPDICONFIGUREDEVICESPARAMSW params, DWORD flags, LPVOID ref) override {
        return inner_->ConfigureDevices(callback, params, flags, ref);
    }

private:
    volatile LONG ref_count_;
    IDirectInput8W* inner_;
};

EffectProxy::EffectProxy(IDirectInputEffect* inner, REFGUID guid, DeviceProxy* owner)
    : ref_count_(1), inner_(inner), owner_(owner), guid_(guid), started_(false), iterations_(1),
      effect_gain_(DI_FFNOMINALMAX), duration_(INFINITE), start_delay_(0), direction_flags_(DIEFF_POLAR),
      direction_{0, 0}, envelope_enabled_(false), envelope_{}, start_time_us_(0), condition_count_(0),
      conditions_{}, constant_force_{}, periodic_force_{}, ramp_force_{} {
    periodic_force_.dwMagnitude = DI_FFNOMINALMAX;
    periodic_force_.dwPeriod = 100000;
}

ULONG STDMETHODCALLTYPE EffectProxy::AddRef() {
    if (inner_) {
        inner_->AddRef();
    }
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

ULONG STDMETHODCALLTYPE EffectProxy::Release() {
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (inner_) {
        inner_->Release();
    }
    if (remaining == 0) {
        owner_->remove_effect(this);
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE EffectProxy::QueryInterface(REFIID riid, LPVOID* out) {
    if (!out) {
        return E_POINTER;
    }

    if (is_guid_equal(riid, IID_IUnknown) || is_guid_equal(riid, IID_IDirectInputEffect)) {
        *out = static_cast<IDirectInputEffect*>(this);
        AddRef();
        return DI_OK;
    }

    if (!inner_) {
        *out = nullptr;
        return E_NOINTERFACE;
    }

    return inner_->QueryInterface(riid, out);
}

HRESULT STDMETHODCALLTYPE EffectProxy::Initialize(HINSTANCE instance, DWORD version, REFGUID guid) {
    return inner_ ? inner_->Initialize(instance, version, guid) : DI_OK;
}

HRESULT STDMETHODCALLTYPE EffectProxy::GetEffectGuid(LPGUID guid) {
    if (!guid) {
        return E_POINTER;
    }
    *guid = guid_;
    return DI_OK;
}

HRESULT STDMETHODCALLTYPE EffectProxy::GetParameters(LPDIEFFECT effect, DWORD flags) {
    return inner_ ? inner_->GetParameters(effect, flags) : DI_OK;
}

HRESULT STDMETHODCALLTYPE EffectProxy::SetParameters(LPCDIEFFECT effect, DWORD flags) {
    const HRESULT result = inner_ ? inner_->SetParameters(effect, flags) : DI_OK;
    if (SUCCEEDED(result) && effect) {
        append_proxy_logf(
            "EffectProxy::SetParameters effect=%s flags=0x%08lx axes=%lu type_bytes=%lu",
            effect_guid_name(guid_),
            static_cast<unsigned long>(flags),
            static_cast<unsigned long>(effect->cAxes),
            static_cast<unsigned long>(effect->cbTypeSpecificParams));
        update_from_effect(effect, flags);
        if ((flags & DIEP_START) != 0) {
            started_ = true;
            if ((flags & DIEP_NORESTART) == 0) {
                start_time_us_ = now_us();
            }
            if (iterations_ == 0) {
                iterations_ = 1;
            }
        }
        if (is_guid_equal(guid_, GUID_ConstantForce) && constant_force_.lMagnitude != 0) {
            started_ = true;
            if (start_time_us_ == 0) {
                start_time_us_ = now_us();
            }
        }
        owner_->rebuild_and_send();
    }
    return result;
}

HRESULT STDMETHODCALLTYPE EffectProxy::Start(DWORD iterations, DWORD flags) {
    const HRESULT result = inner_ ? inner_->Start(iterations, flags) : DI_OK;
    if (SUCCEEDED(result)) {
        append_proxy_logf("EffectProxy::Start effect=%s iterations=%lu flags=0x%08lx",
                          effect_guid_name(guid_),
                          static_cast<unsigned long>(iterations),
                          static_cast<unsigned long>(flags));
        iterations_ = (iterations == 0) ? 1 : iterations;
        started_ = true;
        start_time_us_ = now_us();
        owner_->rebuild_and_send();
    }
    return result;
}

HRESULT STDMETHODCALLTYPE EffectProxy::Stop() {
    const HRESULT result = inner_ ? inner_->Stop() : DI_OK;
    if (SUCCEEDED(result)) {
        append_proxy_logf("EffectProxy::Stop effect=%s", effect_guid_name(guid_));
        started_ = false;
        iterations_ = 1;
        owner_->rebuild_and_send();
    }
    return result;
}

HRESULT STDMETHODCALLTYPE EffectProxy::GetEffectStatus(LPDWORD flags) {
    if (!flags) {
        return E_POINTER;
    }
    if (inner_) {
        return inner_->GetEffectStatus(flags);
    }
    *flags = is_temporally_active(now_us()) ? DIEGES_PLAYING : 0;
    return DI_OK;
}

HRESULT STDMETHODCALLTYPE EffectProxy::Download() { return inner_ ? inner_->Download() : DI_OK; }
HRESULT STDMETHODCALLTYPE EffectProxy::Unload() { return inner_ ? inner_->Unload() : DI_OK; }
HRESULT STDMETHODCALLTYPE EffectProxy::Escape(LPDIEFFESCAPE escape) { return inner_ ? inner_->Escape(escape) : DI_OK; }

bool EffectProxy::has_time_varying_force() const {
    if (!started_) {
        return false;
    }

    return is_guid_equal(guid_, GUID_RampForce) ||
           is_guid_equal(guid_, GUID_Sine) ||
           is_guid_equal(guid_, GUID_Square) ||
           is_guid_equal(guid_, GUID_Triangle) ||
           is_guid_equal(guid_, GUID_SawtoothUp) ||
           is_guid_equal(guid_, GUID_SawtoothDown);
}

void EffectProxy::refresh_runtime(ULONGLONG now) {
    if (started_ && has_expired(now)) {
        started_ = false;
        iterations_ = 1;
    }
}

void EffectProxy::force_stop_runtime() {
    started_ = false;
    iterations_ = 1;
}

void EffectProxy::update_from_effect(LPCDIEFFECT effect, DWORD flags) {
    if (!effect) {
        return;
    }

    const bool all_params = (flags & DIEP_ALLPARAMS) == DIEP_ALLPARAMS;
    if (all_params || (flags & DIEP_GAIN) != 0) {
        effect_gain_ = clamp_dword(effect->dwGain, 0, DI_FFNOMINALMAX);
    }
    if (all_params || (flags & DIEP_DURATION) != 0) {
        duration_ = effect->dwDuration;
    }
    if (all_params || (flags & DIEP_STARTDELAY) != 0) {
        start_delay_ = effect->dwStartDelay;
    }
    if ((all_params || (flags & DIEP_DIRECTION) != 0) && effect->cAxes > 0 && effect->rglDirection) {
        direction_flags_ = effect->dwFlags;
        if ((direction_flags_ & (DIEFF_CARTESIAN | DIEFF_POLAR | DIEFF_SPHERICAL)) == 0) {
            direction_flags_ |= DIEFF_POLAR;
        }
        direction_[0] = effect->rglDirection[0];
        direction_[1] = (effect->cAxes > 1) ? effect->rglDirection[1] : effect->rglDirection[0];
    }
    if (all_params || (flags & DIEP_ENVELOPE) != 0) {
        envelope_enabled_ = effect->lpEnvelope != nullptr;
        if (effect->lpEnvelope) {
            envelope_ = *effect->lpEnvelope;
        }
    }

    if ((all_params || (flags & DIEP_TYPESPECIFICPARAMS) != 0) &&
        effect->lpvTypeSpecificParams && effect->cbTypeSpecificParams > 0) {
        if (is_guid_equal(guid_, GUID_Spring) || is_guid_equal(guid_, GUID_Damper) ||
            is_guid_equal(guid_, GUID_Friction) || is_guid_equal(guid_, GUID_Inertia)) {
            const auto* conditions = static_cast<const DICONDITION*>(effect->lpvTypeSpecificParams);
            condition_count_ = min_dword(2, effect->cbTypeSpecificParams / sizeof(DICONDITION));
            if (condition_count_ == 0) {
                condition_count_ = 1;
            }
            for (DWORD i = 0; i < condition_count_; ++i) {
                conditions_[i] = conditions[i];
            }
            if (condition_count_ == 1) {
                conditions_[1] = conditions_[0];
            }
        } else if (is_guid_equal(guid_, GUID_ConstantForce)) {
            constant_force_ = *static_cast<const DICONSTANTFORCE*>(effect->lpvTypeSpecificParams);
        } else if (is_guid_equal(guid_, GUID_RampForce)) {
            ramp_force_ = *static_cast<const DIRAMPFORCE*>(effect->lpvTypeSpecificParams);
        } else {
            periodic_force_ = *static_cast<const DIPERIODIC*>(effect->lpvTypeSpecificParams);
            if (periodic_force_.dwPeriod == 0) {
                periodic_force_.dwPeriod = 100000;
            }
        }
    }

    if (is_guid_equal(guid_, GUID_ConstantForce)) {
        append_proxy_logf("EffectProxy::ConstantForce magnitude=%ld",
                          static_cast<long>(constant_force_.lMagnitude));
    } else if (is_guid_equal(guid_, GUID_RampForce)) {
        append_proxy_logf("EffectProxy::RampForce start=%ld end=%ld",
                          static_cast<long>(ramp_force_.lStart),
                          static_cast<long>(ramp_force_.lEnd));
    } else if (is_guid_equal(guid_, GUID_Sine) || is_guid_equal(guid_, GUID_Square) ||
               is_guid_equal(guid_, GUID_Triangle) || is_guid_equal(guid_, GUID_SawtoothUp) ||
               is_guid_equal(guid_, GUID_SawtoothDown)) {
        append_proxy_logf("EffectProxy::Periodic magnitude=%lu offset=%ld period=%lu",
                          static_cast<unsigned long>(periodic_force_.dwMagnitude),
                          static_cast<long>(periodic_force_.lOffset),
                          static_cast<unsigned long>(periodic_force_.dwPeriod));
    }
}

float EffectProxy::direction_multiplier() const {
    if ((direction_flags_ & DIEFF_CARTESIAN) != 0) {
        if (direction_[0] == 0) {
            return 1.0f;
        }
        float cartesian = static_cast<float>(direction_[0]) / static_cast<float>(DI_FFNOMINALMAX);
        if (cartesian > 1.0f) {
            cartesian = 1.0f;
        } else if (cartesian < -1.0f) {
            cartesian = -1.0f;
        }
        return cartesian;
    }

    if ((direction_flags_ & (DIEFF_POLAR | DIEFF_SPHERICAL)) == 0) {
        return 1.0f;
    }

    const double angle = (static_cast<double>(direction_[0]) * kTwoPi) / 36000.0;
    return static_cast<float>(std::cos(angle));
}

float EffectProxy::envelope_multiplier(ULONGLONG active_elapsed, ULONGLONG total_duration) const {
    if (!envelope_enabled_) {
        return 1.0f;
    }

    const float attack_level = static_cast<float>(clamp_dword(envelope_.dwAttackLevel, 0, DI_FFNOMINALMAX)) /
                               static_cast<float>(DI_FFNOMINALMAX);
    const float fade_level = static_cast<float>(clamp_dword(envelope_.dwFadeLevel, 0, DI_FFNOMINALMAX)) /
                             static_cast<float>(DI_FFNOMINALMAX);

    if (envelope_.dwAttackTime > 0 && active_elapsed < envelope_.dwAttackTime) {
        const float attack_t = static_cast<float>(active_elapsed) / static_cast<float>(envelope_.dwAttackTime);
        return attack_level + (1.0f - attack_level) * attack_t;
    }

    if (duration_ != INFINITE && envelope_.dwFadeTime > 0 && total_duration > 0 && active_elapsed < total_duration) {
        const ULONGLONG fade_start = (total_duration > envelope_.dwFadeTime) ? (total_duration - envelope_.dwFadeTime) : 0ULL;
        if (active_elapsed >= fade_start) {
            const ULONGLONG fade_elapsed = active_elapsed - fade_start;
            const float fade_t = static_cast<float>(fade_elapsed) / static_cast<float>(envelope_.dwFadeTime);
            return 1.0f + (fade_level - 1.0f) * fade_t;
        }
    }

    return 1.0f;
}

bool EffectProxy::is_temporally_active(ULONGLONG now) const {
    if (!started_) {
        return false;
    }

    if (now <= start_time_us_) {
        return start_delay_ == 0;
    }

    const ULONGLONG elapsed = now - start_time_us_;
    if (elapsed < start_delay_) {
        return false;
    }

    if (duration_ == INFINITE || iterations_ == INFINITE || duration_ == 0) {
        return true;
    }

    const ULONGLONG total_duration = static_cast<ULONGLONG>(duration_) * static_cast<ULONGLONG>(iterations_);
    const ULONGLONG active_elapsed = elapsed - start_delay_;
    return active_elapsed < total_duration;
}

bool EffectProxy::has_expired(ULONGLONG now) const {
    if (!started_ || duration_ == INFINITE || iterations_ == INFINITE || duration_ == 0) {
        return false;
    }
    if (now <= start_time_us_) {
        return false;
    }

    const ULONGLONG elapsed = now - start_time_us_;
    if (elapsed < start_delay_) {
        return false;
    }

    const ULONGLONG total_duration = static_cast<ULONGLONG>(duration_) * static_cast<ULONGLONG>(iterations_);
    return (elapsed - start_delay_) >= total_duration;
}

LONG EffectProxy::compute_force(ULONGLONG now, DWORD device_gain) const {
    if (!is_temporally_active(now)) {
        return 0;
    }

    const ULONGLONG elapsed = (now > start_time_us_) ? (now - start_time_us_) : 0ULL;
    const ULONGLONG active_elapsed = (elapsed > start_delay_) ? (elapsed - start_delay_) : 0ULL;
    const ULONGLONG total_duration =
        (duration_ == INFINITE || iterations_ == INFINITE || duration_ == 0)
            ? 0ULL
            : static_cast<ULONGLONG>(duration_) * static_cast<ULONGLONG>(iterations_);

    LONG raw_force = 0;

    if (is_guid_equal(guid_, GUID_ConstantForce)) {
        raw_force = constant_force_.lMagnitude;
    } else if (is_guid_equal(guid_, GUID_RampForce)) {
        if (duration_ == 0 || duration_ == INFINITE) {
            raw_force = ramp_force_.lEnd;
        } else {
            const DWORD cycle_duration = (duration_ == 0) ? 1 : duration_;
            const DWORD cycle_elapsed = static_cast<DWORD>(active_elapsed % cycle_duration);
            const LONG delta = ramp_force_.lEnd - ramp_force_.lStart;
            raw_force = ramp_force_.lStart +
                        static_cast<LONG>((static_cast<LONGLONG>(delta) * static_cast<LONGLONG>(cycle_elapsed)) /
                                          static_cast<LONGLONG>(cycle_duration));
        }
    } else if (is_guid_equal(guid_, GUID_Sine) || is_guid_equal(guid_, GUID_Square) ||
               is_guid_equal(guid_, GUID_Triangle) || is_guid_equal(guid_, GUID_SawtoothUp) ||
               is_guid_equal(guid_, GUID_SawtoothDown)) {
        const DWORD period = (periodic_force_.dwPeriod == 0) ? 100000 : periodic_force_.dwPeriod;
        const double phase_offset = static_cast<double>(periodic_force_.dwPhase) / 36000.0;
        const double phase = static_cast<double>(active_elapsed % period) / static_cast<double>(period) + phase_offset;
        const double wave = periodic_wave_sample(guid_, phase);
        raw_force = static_cast<LONG>(periodic_force_.lOffset +
                                      static_cast<LONG>(static_cast<double>(periodic_force_.dwMagnitude) * wave));
    } else {
        return 0;
    }

    const float shaped = static_cast<float>(raw_force) *
                         envelope_multiplier(active_elapsed, total_duration) *
                         direction_multiplier();
    const LONG directed_force = clamp_long(static_cast<LONG>(shaped), -DI_FFNOMINALMAX, DI_FFNOMINALMAX);
    return apply_combined_gain(directed_force, effect_gain_, device_gain);
}

void EffectProxy::apply(g923bridge::WheelStatePayload& payload, DWORD device_gain, ULONGLONG now) const {
    if (!is_temporally_active(now)) {
        return;
    }

    if (is_guid_equal(guid_, GUID_Spring)) {
        payload.custom_spring_enabled = 1;
        const DWORD combined_positive_sat = apply_combined_gain_unsigned(
            conditions_[0].dwPositiveSaturation, effect_gain_, device_gain);
        const DWORD combined_negative_sat = apply_combined_gain_unsigned(
            conditions_[1].dwNegativeSaturation, effect_gain_, device_gain);
        const LONG combined_positive_coeff = apply_combined_gain(
            abs_long(conditions_[0].lPositiveCoefficient), effect_gain_, device_gain);
        const LONG combined_negative_coeff = apply_combined_gain(
            abs_long(conditions_[1].lNegativeCoefficient), effect_gain_, device_gain);

        payload.spring_k1 = max_u8(payload.spring_k1, scale_nibble(combined_positive_coeff));
        payload.spring_k2 = max_u8(payload.spring_k2, scale_nibble(combined_negative_coeff));
        payload.spring_sat1 = max_u8(payload.spring_sat1, scale_nibble(static_cast<LONG>(combined_positive_sat)));
        payload.spring_sat2 = max_u8(payload.spring_sat2, scale_nibble(static_cast<LONG>(combined_negative_sat)));
        payload.spring_deadband_left = max_u8(payload.spring_deadband_left, scale_nibble(conditions_[0].lDeadBand));
        payload.spring_deadband_right = max_u8(payload.spring_deadband_right, scale_nibble(conditions_[1].lDeadBand));
        payload.spring_clip = max_u8(
            payload.spring_clip,
            scale_byte(static_cast<LONG>(max_dword(combined_positive_sat, combined_negative_sat))));
    } else if (is_guid_equal(guid_, GUID_Damper) || is_guid_equal(guid_, GUID_Friction) || is_guid_equal(guid_, GUID_Inertia)) {
        payload.damper_enabled = 1;
        payload.damper_force_positive = max_u8(
            payload.damper_force_positive,
            scale_byte(apply_combined_gain(abs_long(conditions_[0].lPositiveCoefficient), effect_gain_, device_gain)));
        payload.damper_force_negative = max_u8(
            payload.damper_force_negative,
            scale_byte(apply_combined_gain(abs_long(conditions_[1].lNegativeCoefficient), effect_gain_, device_gain)));
        payload.damper_saturation_positive =
            max_u8(payload.damper_saturation_positive, scale_byte(static_cast<LONG>(apply_combined_gain_unsigned(
                conditions_[0].dwPositiveSaturation, effect_gain_, device_gain))));
        payload.damper_saturation_negative =
            max_u8(payload.damper_saturation_negative, scale_byte(static_cast<LONG>(apply_combined_gain_unsigned(
                conditions_[1].dwNegativeSaturation, effect_gain_, device_gain))));
    } else {
        const LONG force = compute_force(now, device_gain);
        if (force != 0) {
            payload.constant_force_enabled = 1;
            payload.constant_force_magnitude = static_cast<std::int16_t>(
                clamp_long(static_cast<LONG>(payload.constant_force_magnitude) + force,
                           -DI_FFNOMINALMAX, DI_FFNOMINALMAX));
        }
    }
}

DeviceProxy::DeviceProxy(IDirectInputDevice8W* inner)
    : ref_count_(1), inner_(inner), effects_{}, effect_count_(0), ff_gain_(DI_FFNOMINALMAX),
      autocenter_mode_(DIPROPAUTOCENTER_ON), ff_state_(DIGFFS_EMPTY | DIGFFS_STOPPED | DIGFFS_ACTUATORSON | DIGFFS_POWERON),
      advertises_force_feedback_(true), last_sent_has_state_(false), have_last_payload_(false),
      last_periodic_rebuild_us_(0), last_payload_{} {
}

ULONG STDMETHODCALLTYPE DeviceProxy::AddRef() {
    inner_->AddRef();
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
}

ULONG STDMETHODCALLTYPE DeviceProxy::Release() {
    inner_->Release();
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE DeviceProxy::QueryInterface(REFIID riid, LPVOID* out) {
    if (!out) {
        return E_POINTER;
    }

    if (is_guid_equal(riid, IID_IUnknown) ||
        is_guid_equal(riid, IID_IDirectInputDevice8W) ||
        is_guid_equal(riid, IID_IDirectInputDevice7W) ||
        is_guid_equal(riid, IID_IDirectInputDevice2W) ||
        is_guid_equal(riid, IID_IDirectInputDeviceW)) {
        append_proxy_log("DeviceProxy::QueryInterface returning wrapped W device");
        *out = static_cast<IDirectInputDevice8W*>(this);
        AddRef();
        return DI_OK;
    }

    return inner_->QueryInterface(riid, out);
}

HRESULT STDMETHODCALLTYPE DeviceProxy::GetCapabilities(LPDIDEVCAPS caps) {
    if (!caps) {
        return E_POINTER;
    }

    const HRESULT result = inner_->GetCapabilities(caps);
    if (FAILED(result)) {
        return result;
    }

    advertises_force_feedback_ = is_game_controller_type(caps->dwDevType) || caps->dwAxes > 0;
    if (advertises_force_feedback_) {
        caps->dwFlags |= DIDC_FORCEFEEDBACK | DIDC_FFATTACK | DIDC_FFFADE | DIDC_SATURATION |
                         DIDC_POSNEGCOEFFICIENTS | DIDC_POSNEGSATURATION | DIDC_DEADBAND;
        if (caps->dwSize >= sizeof(DIDEVCAPS)) {
            caps->dwFFSamplePeriod = 0;
            caps->dwFFMinTimeResolution = 0;
            caps->dwFFDriverVersion = 1;
        }
    }

    append_proxy_logf("GetCapabilities flags=0x%08lx axes=%lu buttons=%lu povs=%lu",
                      static_cast<unsigned long>(caps->dwFlags),
                      static_cast<unsigned long>(caps->dwAxes),
                      static_cast<unsigned long>(caps->dwButtons),
                      static_cast<unsigned long>(caps->dwPOVs));

    return result;
}

HRESULT STDMETHODCALLTYPE DeviceProxy::EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW callback, LPVOID ref, DWORD flags) {
    if (!callback) {
        return DIERR_INVALIDPARAM;
    }

    EnumObjectContext context{};
    context.callback = callback;
    context.ref = ref;
    context.requested_flags = flags;
    context.actuator_only = (flags & DIDFT_FFACTUATOR) != 0;
    context.actuator_emitted = false;

    DWORD inner_flags = flags;
    if (context.actuator_only) {
        inner_flags &= ~DIDFT_FFACTUATOR;
        inner_flags |= DIDFT_AXIS;
    }

    const HRESULT result = inner_->EnumObjects(enum_objects_wrapper, &context, inner_flags);
    if (FAILED(result)) {
        return result;
    }

    return context.actuator_only && !context.actuator_emitted ? DI_OK : result;
}

HRESULT STDMETHODCALLTYPE DeviceProxy::GetProperty(REFGUID prop, LPDIPROPHEADER header) {
    if (!header) {
        return E_POINTER;
    }

    if (is_property_key(prop, 7)) {
        if (header->dwSize < sizeof(DIPROPDWORD)) {
            return DIERR_INVALIDPARAM;
        }
        auto* value = reinterpret_cast<LPDIPROPDWORD>(header);
        value->dwData = ff_gain_;
        return DI_OK;
    }

    if (is_property_key(prop, 9)) {
        if (header->dwSize < sizeof(DIPROPDWORD)) {
            return DIERR_INVALIDPARAM;
        }
        auto* value = reinterpret_cast<LPDIPROPDWORD>(header);
        value->dwData = autocenter_mode_;
        return DI_OK;
    }

    return inner_->GetProperty(prop, header);
}

HRESULT STDMETHODCALLTYPE DeviceProxy::SetProperty(REFGUID prop, LPCDIPROPHEADER header) {
    if (!header) {
        return E_POINTER;
    }

    if (is_property_key(prop, 7)) {
        if (header->dwSize < sizeof(DIPROPDWORD)) {
            return DIERR_INVALIDPARAM;
        }
        ff_gain_ = clamp_dword(reinterpret_cast<const DIPROPDWORD*>(header)->dwData, 0, DI_FFNOMINALMAX);
        rebuild_and_send();
        return DI_OK;
    }

    if (is_property_key(prop, 9)) {
        if (header->dwSize < sizeof(DIPROPDWORD)) {
            return DIERR_INVALIDPARAM;
        }
        autocenter_mode_ = reinterpret_cast<const DIPROPDWORD*>(header)->dwData;
        rebuild_and_send();
        return DI_OK;
    }

    return inner_->SetProperty(prop, header);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::Acquire() { return inner_->Acquire(); }
HRESULT STDMETHODCALLTYPE DeviceProxy::Unacquire() {
    g_bridge_client.send_stop_all();
    ff_state_ |= DIGFFS_STOPPED | DIGFFS_EMPTY;
    have_last_payload_ = false;
    last_sent_has_state_ = false;
    last_payload_ = g923bridge::WheelStatePayload{};
    return inner_->Unacquire();
}
HRESULT STDMETHODCALLTYPE DeviceProxy::GetDeviceState(DWORD size, LPVOID data) { return inner_->GetDeviceState(size, data); }
HRESULT STDMETHODCALLTYPE DeviceProxy::GetDeviceData(DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD inout, DWORD flags) {
    return inner_->GetDeviceData(size, data, inout, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::SetDataFormat(LPCDIDATAFORMAT format) { return inner_->SetDataFormat(format); }
HRESULT STDMETHODCALLTYPE DeviceProxy::SetEventNotification(HANDLE handle) { return inner_->SetEventNotification(handle); }
HRESULT STDMETHODCALLTYPE DeviceProxy::SetCooperativeLevel(HWND window, DWORD flags) { return inner_->SetCooperativeLevel(window, flags); }
HRESULT STDMETHODCALLTYPE DeviceProxy::GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW instance, DWORD object, DWORD how) {
    const HRESULT result = inner_->GetObjectInfo(instance, object, how);
    if (SUCCEEDED(result) && instance && (instance->dwType & DIDFT_AXIS) != 0) {
        instance->dwFlags |= DIDOI_FFACTUATOR;
        instance->dwFFMaxForce = DI_FFNOMINALMAX;
        instance->dwFFForceResolution = 1024;
    }
    return result;
}
HRESULT STDMETHODCALLTYPE DeviceProxy::GetDeviceInfo(LPDIDEVICEINSTANCEW instance) {
    const HRESULT result = inner_->GetDeviceInfo(instance);
    if (SUCCEEDED(result) && instance && instance->dwSize >= sizeof(DIDEVICEINSTANCEW) && advertises_force_feedback_) {
        instance->guidFFDriver = CLSID_DirectInputDevice8;
        instance->dwDevType = kSyntheticDrivingType;
        append_proxy_log("GetDeviceInfo injected guidFFDriver");
    }
    return result;
}
HRESULT STDMETHODCALLTYPE DeviceProxy::RunControlPanel(HWND window, DWORD flags) { return inner_->RunControlPanel(window, flags); }
HRESULT STDMETHODCALLTYPE DeviceProxy::Initialize(HINSTANCE instance, DWORD version, REFGUID guid) { return inner_->Initialize(instance, version, guid); }
HRESULT STDMETHODCALLTYPE DeviceProxy::EnumEffects(LPDIENUMEFFECTSCALLBACKW callback, LPVOID ref, DWORD type) {
    if (!callback) {
        return DIERR_INVALIDPARAM;
    }

    append_proxy_logf("EnumEffects type=0x%08lx", static_cast<unsigned long>(type));

    for (int i = 0; i < supported_effect_count(); ++i) {
        const auto& effect = supported_effects()[i];
        if (!effect_matches_type(effect, type)) {
            continue;
        }

        DIEFFECTINFOW info{};
        info.dwSize = sizeof(DIEFFECTINFOW);
        const HRESULT fill_result = populate_effect_info(&info, effect);
        if (FAILED(fill_result)) {
            append_proxy_logf("EnumEffects failed to populate effect info: 0x%08lx",
                              static_cast<unsigned long>(fill_result));
            return fill_result;
        }

        append_proxy_logf("EnumEffects reporting %ls", effect.name);

        if (callback(&info, ref) == DIENUM_STOP) {
            append_proxy_log("EnumEffects callback requested stop");
            break;
        }
    }

    return DI_OK;
}

HRESULT STDMETHODCALLTYPE DeviceProxy::GetEffectInfo(LPDIEFFECTINFOW info, REFGUID guid) {
    for (int i = 0; i < supported_effect_count(); ++i) {
        const auto& effect = supported_effects()[i];
        if (is_guid_equal(guid, *effect.guid)) {
            append_proxy_log("GetEffectInfo returning synthetic effect info");
            return populate_effect_info(info, effect);
        }
    }

    return DIERR_DEVICENOTREG;
}

HRESULT STDMETHODCALLTYPE DeviceProxy::GetForceFeedbackState(LPDWORD out) {
    if (!out) {
        return E_POINTER;
    }
    *out = ff_state_;
    return DI_OK;
}
HRESULT STDMETHODCALLTYPE DeviceProxy::EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback, LPVOID ref, DWORD flags) {
    return inner_->EnumCreatedEffectObjects(callback, ref, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::Escape(LPDIEFFESCAPE escape) { return inner_->Escape(escape); }
HRESULT STDMETHODCALLTYPE DeviceProxy::Poll() {
    const HRESULT result = inner_->Poll();
    const ULONGLONG now = now_us();
    if (has_active_time_varying_effect() &&
        (last_periodic_rebuild_us_ == 0 || (now - last_periodic_rebuild_us_) >= kPeriodicUpdateIntervalUs)) {
        rebuild_and_send();
        last_periodic_rebuild_us_ = now;
    }
    return result;
}
HRESULT STDMETHODCALLTYPE DeviceProxy::SendDeviceData(DWORD size, LPCDIDEVICEOBJECTDATA data, LPDWORD inout, DWORD flags) {
    return inner_->SendDeviceData(size, data, inout, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::EnumEffectsInFile(LPCWSTR file, LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID ref, DWORD flags) {
    return inner_->EnumEffectsInFile(file, callback, ref, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::WriteEffectToFile(LPCWSTR file, DWORD entries, LPDIFILEEFFECT effects, DWORD flags) {
    return inner_->WriteEffectToFile(file, entries, effects, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::BuildActionMap(LPDIACTIONFORMATW format, LPCWSTR user, DWORD flags) {
    return inner_->BuildActionMap(format, user, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::SetActionMap(LPDIACTIONFORMATW format, LPCWSTR user, DWORD flags) {
    return inner_->SetActionMap(format, user, flags);
}
HRESULT STDMETHODCALLTYPE DeviceProxy::GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW header) { return inner_->GetImageInfo(header); }

HRESULT STDMETHODCALLTYPE DeviceProxy::CreateEffect(
    REFGUID guid, LPCDIEFFECT effect, LPDIRECTINPUTEFFECT* out, LPUNKNOWN outer) {
    (void)outer;
    if (!out) {
        return E_POINTER;
    }

    IDirectInputEffect* inner_effect = nullptr;
    append_proxy_logf("CreateEffect called effect=%s", effect_guid_name(guid));
    HRESULT result = inner_->CreateEffect(guid, effect, &inner_effect, outer);
    if (FAILED(result) || !inner_effect) {
        inner_effect = nullptr;
        result = DI_OK;
        append_proxy_log("CreateEffect using software fallback");
    }

    auto* proxy = new EffectProxy(inner_effect, guid, this);
    if (effect) {
        proxy->SetParameters(effect, DIEP_ALLPARAMS);
    }
    if (effect_count_ < kMaxEffects) {
        effects_[effect_count_++] = proxy;
    } else {
        append_proxy_log("effect table full, not tracking additional effect");
    }
    *out = proxy;
    return result;
}

HRESULT STDMETHODCALLTYPE DeviceProxy::SendForceFeedbackCommand(DWORD command) {
    append_proxy_logf("SendForceFeedbackCommand command=0x%08lx",
                      static_cast<unsigned long>(command));
    HRESULT result = inner_->SendForceFeedbackCommand(command);
    if (FAILED(result)) {
        result = DI_OK;
    }

    switch (command) {
        case DISFFC_RESET:
        case DISFFC_STOPALL:
            for (int i = 0; i < effect_count_; ++i) {
                if (effects_[i]) {
                    effects_[i]->force_stop_runtime();
                }
            }
            ff_state_ |= DIGFFS_STOPPED | DIGFFS_EMPTY;
            ff_state_ &= ~DIGFFS_PAUSED;
            g_bridge_client.send_stop_all();
            have_last_payload_ = false;
            last_sent_has_state_ = false;
            last_payload_ = g923bridge::WheelStatePayload{};
            break;
        case DISFFC_PAUSE:
            ff_state_ |= DIGFFS_PAUSED;
            break;
        case DISFFC_CONTINUE:
            ff_state_ &= ~DIGFFS_PAUSED;
            rebuild_and_send();
            break;
        case DISFFC_SETACTUATORSON:
            ff_state_ |= DIGFFS_ACTUATORSON;
            ff_state_ &= ~DIGFFS_ACTUATORSOFF;
            rebuild_and_send();
            break;
        case DISFFC_SETACTUATORSOFF:
            for (int i = 0; i < effect_count_; ++i) {
                if (effects_[i]) {
                    effects_[i]->force_stop_runtime();
                }
            }
            ff_state_ |= DIGFFS_ACTUATORSOFF;
            ff_state_ &= ~DIGFFS_ACTUATORSON;
            g_bridge_client.send_stop_all();
            have_last_payload_ = false;
            last_sent_has_state_ = false;
            last_payload_ = g923bridge::WheelStatePayload{};
            break;
        default:
            break;
    }

    return result;
}

void DeviceProxy::remove_effect(EffectProxy* effect) {
    for (int i = 0; i < effect_count_; ++i) {
        if (effects_[i] == effect) {
            for (int j = i; j < effect_count_ - 1; ++j) {
                effects_[j] = effects_[j + 1];
            }
            effects_[effect_count_ - 1] = nullptr;
            --effect_count_;
            break;
        }
    }
    rebuild_and_send();
}

bool DeviceProxy::has_active_time_varying_effect() const {
    for (int i = 0; i < effect_count_; ++i) {
        if (effects_[i] && effects_[i]->has_time_varying_force()) {
            return true;
        }
    }
    return false;
}

void DeviceProxy::rebuild_and_send() {
    const ULONGLONG now = now_us();
    g923bridge::WheelStatePayload payload{};

    for (int i = 0; i < effect_count_; ++i) {
        if (effects_[i]) {
            effects_[i]->refresh_runtime(now);
            effects_[i]->apply(payload, ff_gain_, now);
        }
    }

    if (autocenter_mode_ == DIPROPAUTOCENTER_ON && !payload.custom_spring_enabled) {
        constexpr DWORD kAutocenterFallbackNominal = 3200;
        payload.autocenter_enabled = 1;
        payload.autocenter_force = max_u8(
            payload.autocenter_force,
            scale_byte(static_cast<LONG>(apply_unsigned_gain(ff_gain_, kAutocenterFallbackNominal))));
        payload.autocenter_slope = max_u8(payload.autocenter_slope, 4);
    }

    if ((ff_state_ & DIGFFS_PAUSED) != 0 || (ff_state_ & DIGFFS_ACTUATORSOFF) != 0) {
        payload = g923bridge::WheelStatePayload{};
    }

    const bool has_state =
        payload.autocenter_enabled || payload.custom_spring_enabled ||
        payload.damper_enabled || payload.constant_force_enabled;

    if (has_state) {
        ff_state_ &= ~DIGFFS_EMPTY;
        ff_state_ &= ~DIGFFS_STOPPED;
        const bool payload_changed =
            !have_last_payload_ || std::memcmp(&payload, &last_payload_, sizeof(payload)) != 0;
        if (payload_changed) {
            append_proxy_logf(
                "rebuild_and_send spring=%u damper=%u constant=%u constant_mag=%d",
                static_cast<unsigned>(payload.custom_spring_enabled),
                static_cast<unsigned>(payload.damper_enabled),
                static_cast<unsigned>(payload.constant_force_enabled),
                static_cast<int>(payload.constant_force_magnitude));
            g_bridge_client.send_state(payload);
            last_payload_ = payload;
            have_last_payload_ = true;
        }
        last_sent_has_state_ = true;
    } else {
        ff_state_ |= DIGFFS_EMPTY | DIGFFS_STOPPED;
        have_last_payload_ = false;
        if (last_sent_has_state_) {
            append_proxy_log("rebuild_and_send stop_all");
            g_bridge_client.send_stop_all();
            last_sent_has_state_ = false;
            last_payload_ = g923bridge::WheelStatePayload{};
        }
    }
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_this_module = instance;
        g_bridge_client.initialize();
        DisableThreadLibraryCalls(instance);
        append_proxy_log("proxy attached");
        ensure_real_dinput_loaded();
    } else if (reason == DLL_PROCESS_DETACH) {
        append_proxy_log("proxy detaching");
        g_bridge_client.send_stop_all();
        g_bridge_client.shutdown();
        InterlockedExchange(&g_bridge_announced, 0);
        g_this_module = nullptr;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    append_proxy_logf("DirectInput8Create called for %s", directinput_iid_name(riid));
    announce_bridge_connection();

    if (!out || !ensure_real_dinput_loaded()) {
        append_proxy_log("DirectInput8Create failed before real create");
        return E_FAIL;
    }

    LPVOID raw = nullptr;
    const HRESULT result = g_real_create(instance, version, riid, &raw, outer);
    if (FAILED(result) || !raw) {
        append_proxy_logf("real DirectInput8Create failed: 0x%08lx", static_cast<unsigned long>(result));
        return result;
    }

    if (is_guid_equal(riid, IID_IDirectInput8W)) {
        append_proxy_log("wrapping IDirectInput8W");
        *out = static_cast<IDirectInput8W*>(new DirectInputProxy(reinterpret_cast<IDirectInput8W*>(raw)));
        return result;
    }

    append_proxy_log("passing DirectInput interface through without wrapping");
    *out = raw;
    return result;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
    return g_real_can_unload ? g_real_can_unload() : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    return g_real_get_class_object ? g_real_get_class_object(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
    return g_real_register_server ? g_real_register_server() : S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
    return g_real_unregister_server ? g_real_unregister_server() : S_OK;
}
