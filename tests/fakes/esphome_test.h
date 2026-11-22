#pragma once
// Minimal external ESPHome boundary doubles. No LG parsing/state logic lives here.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace esphome {
template<class T> using optional = std::optional<T>;
inline uint32_t test_now = 0;
inline uint32_t millis() { return test_now; }
inline float fahrenheit_to_celsius(float v) { return (v-32)*5/9; }
inline float celsius_to_fahrenheit(float v) { return v*9/5+32; }
template<class... T> void test_log(T&&...) {}
inline std::string format_hex_pretty(const uint8_t* p, size_t n) {
 std::string out; char b[4];
 for (size_t i=0;i<n;++i) { std::snprintf(b,sizeof b,i?".%02X":"%02X",p[i]); out+=b; }
 return out;
}
constexpr size_t format_hex_pretty_size(size_t n) { return n*3; }
template<size_t N> char* format_hex_pretty_to(char (&out)[N], const uint8_t* p, size_t n, char sep=':') {
 std::string s=format_hex_pretty(p,n); std::replace(s.begin(),s.end(),'.',sep);
 std::snprintf(out,N,"%s",s.c_str()); return out;
}
namespace setup_priority { constexpr float BUS = 100; }
class Component {
public:
 virtual ~Component() = default;
 virtual void setup() {} virtual void loop() {} virtual float get_setup_priority() const { return 0; }
 virtual void dump_config() {}
 std::map<std::string, std::function<void()>> timeouts, intervals;
 void set_timeout(const std::string& n, uint32_t, std::function<void()> f) { timeouts[n] = f; }
 void set_interval(const std::string& n, uint32_t, std::function<void()> f) { intervals[n] = f; }
};
class InternalGPIOPin { public: bool high = true; bool digital_read() const { return high; } };
struct ESPPreferenceObject { template<class T> bool load(T*) { return false; } template<class T> bool save(T*) { return true; } };
struct Preferences { template<class T> ESPPreferenceObject make_preference(uint32_t) { return {}; } void sync() {} };
inline Preferences test_preferences;
inline Preferences* global_preferences = &test_preferences;
struct Application { unsigned reboots = 0; void safe_reboot() { ++reboots; } };
inline Application App;
namespace sensor { class Sensor { public: float state = NAN; void publish_state(float v) { state=v; } float get_state() const { return state; } }; }
namespace binary_sensor { class BinarySensor { public: bool state=false; void publish_state(bool v) { state=v; } }; }
namespace text_sensor { class TextSensor { public: std::string state; void publish_state(const std::string& v) { state=v; } }; }
namespace switch_ {
enum SwitchRestoreMode { SWITCH_RESTORE_DEFAULT_OFF };
class Switch { public: virtual ~Switch()=default; bool state=false; virtual void write_state(bool)=0;
 void publish_state(bool v) { state=v; } void turn_on() { write_state(true); } void turn_off() { write_state(false); }
 void set_restore_mode(SwitchRestoreMode) {} optional<bool> get_initial_state_with_restore_mode() { return false; }
}; }
namespace button { class Button { public: virtual ~Button()=default; virtual void press_action()=0; void press() { press_action(); } }; }
namespace number { class Number { public: virtual ~Number()=default; float state=0; virtual void control(float)=0; void publish_state(float v) { state=v; } }; }
namespace select { class Select { public: virtual ~Select()=default; virtual void control(const std::string&)=0;
 std::vector<std::string> options{"0","1","2","3","4","5","6"}; std::string state="0";
 std::vector<std::function<void(size_t)>> callbacks;
 std::string current_option() const { return state; }
 void add_on_state_callback(std::function<void(size_t)> f) { callbacks.push_back(f); }
 void publish_state(const std::string& v) { state=v; auto i=std::find(options.begin(),options.end(),v)-options.begin(); for(auto& f:callbacks) f(i); }
 optional<std::string> at(size_t i) const { return i<options.size()?optional<std::string>(options[i]):std::nullopt; }
}; }
namespace climate {
enum ClimateMode { CLIMATE_MODE_OFF,CLIMATE_MODE_COOL,CLIMATE_MODE_HEAT,CLIMATE_MODE_DRY,CLIMATE_MODE_FAN_ONLY,CLIMATE_MODE_HEAT_COOL };
enum ClimateFanMode { CLIMATE_FAN_LOW,CLIMATE_FAN_MEDIUM,CLIMATE_FAN_HIGH,CLIMATE_FAN_AUTO,CLIMATE_FAN_QUIET };
enum ClimateSwingMode { CLIMATE_SWING_OFF,CLIMATE_SWING_BOTH,CLIMATE_SWING_VERTICAL,CLIMATE_SWING_HORIZONTAL };
using ClimateModeMask=std::set<ClimateMode>; using ClimateFanModeMask=std::set<ClimateFanMode>; using ClimateSwingModeMask=std::set<ClimateSwingMode>;
constexpr unsigned CLIMATE_SUPPORTS_CURRENT_TEMPERATURE=1;
struct ClimateTraits {
 void set_supported_modes(ClimateModeMask) {} void set_supported_fan_modes(ClimateFanModeMask) {} void set_supported_swing_modes(ClimateSwingModeMask) {}
 void add_feature_flags(unsigned) {} void set_visual_min_temperature(float) {} void set_visual_max_temperature(float) {}
 void set_visual_target_temperature_step(float) {} void set_visual_current_temperature_step(float) {}
};
struct ClimateCall {
 optional<ClimateMode> mode; optional<ClimateFanMode> fan_mode; std::string custom_fan_mode;
 optional<ClimateMode> get_mode() const { return mode; } optional<float> get_target_temperature() const { return {}; }
 optional<ClimateFanMode> get_fan_mode() const { return fan_mode; } optional<ClimateSwingMode> get_swing_mode() const { return {}; }
 bool has_custom_fan_mode() const { return !custom_fan_mode.empty(); }
 const std::string& get_custom_fan_mode() const { return custom_fan_mode; }
};
class Climate { public:
 virtual ~Climate()=default; virtual void control(const ClimateCall&)=0; virtual ClimateTraits traits()=0;
 ClimateMode mode=CLIMATE_MODE_OFF; optional<ClimateFanMode> fan_mode=CLIMATE_FAN_MEDIUM; ClimateSwingMode swing_mode=CLIMATE_SWING_OFF;
 float target_temperature=20, current_temperature=20;
 std::vector<const char*> custom_fan_modes; const char* custom_fan_mode=nullptr;
 void set_supported_custom_fan_modes(const std::vector<const char*>& m) { custom_fan_modes=m; }
 bool has_custom_fan_mode() const { return custom_fan_mode!=nullptr; }
 std::string get_custom_fan_mode() const { return custom_fan_mode?custom_fan_mode:""; }
 const char* find_custom_fan_mode_(const char* m) { for (auto* c:custom_fan_modes) if(!std::strcmp(c,m)) return c; return nullptr; }
 bool set_custom_fan_mode_(const char* m) { auto* c=find_custom_fan_mode_(m); if(!c) return false; custom_fan_mode=c; fan_mode.reset(); return true; }
 bool set_fan_mode_(ClimateFanMode m) { custom_fan_mode=nullptr; fan_mode=m; return true; }
 void publish_state() {} uint32_t get_object_id_hash() const { return 1; }
 struct Restore { void apply(Climate*) {} }; optional<Restore> restore_state_() { return {}; }
}; }
namespace uart { class UARTDevice { public:
 std::deque<uint8_t> rx; std::vector<std::vector<uint8_t>> tx;
 size_t available() const { return rx.size(); }
 bool read_byte(uint8_t* v) { if(rx.empty()) return false; *v=rx.front(); rx.pop_front(); return true; }
 void write_array(const uint8_t* p,size_t n) { tx.emplace_back(p,p+n); }
}; }
}
#define ESP_LOGD(...) ::esphome::test_log(__VA_ARGS__)
#define ESP_LOGW(...) ::esphome::test_log(__VA_ARGS__)
#define ESP_LOGE(...) ::esphome::test_log(__VA_ARGS__)
#define ESP_LOGI(...) ::esphome::test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) ::esphome::test_log(__VA_ARGS__)
