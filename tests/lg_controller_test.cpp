#include "esphome/components/lg_controller/lg-controller.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace esphome;
using namespace esphome::lg_controller;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x " failed\n"; std::exit(1); } } while (false)

// Synthetic frames derived from protocol.md Type 0/1/3 field tables at e31f29f.
// Literal checksums are independent of the implementation. NOT household captures.
const protocol::Frame cap8{0xC9,0x12,0xE0,0,0,0,0,0,8,0,0,0,0x96};
const protocol::Frame cap4{0xC9,0x12,0xE0,0,0,0,0,0,0,0,0,0,0xEE};
const protocol::Frame count8{0xCB,0,0,0,0,0,0,0,0,0,0x80,0,0x1E};
const protocol::Frame count4{0xCB,0,0,0,0,0,0,0,0,0,0x40,0,0x5E};
const protocol::Frame all_off{0xC8,0,0,0,0,0,5,20,0,0,0,0,0xB4};

struct Fixture {
 InternalGPIOPin pin;
 LgSelect selects[5]; LgNumber numbers[6]; sensor::Sensor sensors[5];
 binary_sensor::BinarySensor binaries[4]; LgSwitch switches[11];
 LgController controller;
 explicit Fixture(uint8_t count=0, bool slave=false, sensor::Sensor* room=nullptr) : controller(
  &pin,room,&selects[0],&selects[1],&selects[2],&selects[3],&selects[4],
  &numbers[0],&numbers[1],&numbers[2],&numbers[3],nullptr,&numbers[4],
  &sensors[0],&sensors[1],&sensors[2],&sensors[3],
  &binaries[0],&binaries[1],&binaries[2],&binaries[3],
  &switches[0],&switches[1],&switches[2],
  &switches[3],&switches[4],&switches[5],&switches[6],
  &switches[7],&switches[8],&switches[9],&switches[10],false,slave,0,count,0,false) {
   test_now=0;
   controller.set_configured_zone_count_sensor(&sensors[4]);
   controller.setup();
 }
 void receive(const protocol::Frame& f) {
  CHECK(protocol::is_valid_frame(f));
  controller.rx.insert(controller.rx.end(),f.begin(),f.end());
  static_cast<Component&>(controller).loop();
 }
 void tick(uint32_t elapsed) { test_now+=elapsed; controller.intervals.at("update")(); }
 void ready() {
  controller.timeouts.at("initial_send")(); tick(10000);
  CHECK(controller.tx.size()==1);
  protocol::Frame echo{}; std::copy(controller.tx.back().begin(),controller.tx.back().end(),echo.begin());
  receive(echo); controller.tx.clear();
 }
 // Let the bus go idle, expect exactly one transmission, echo it back and return it.
 std::vector<uint8_t> send_next() {
  tick(500); CHECK(controller.tx.size()==1);
  auto sent=controller.tx.back(); echo_all(); return sent;
 }
 void echo_all() {
  for (auto& sent : controller.tx) {
   protocol::Frame echo{}; std::copy(sent.begin(),sent.end(),echo.begin()); receive(echo);
  }
  controller.tx.clear();
 }
 // Advance time in one-second steps, echoing heartbeats. Returns every frame sent.
 std::vector<std::vector<uint8_t>> advance(uint32_t seconds) {
  std::vector<std::vector<uint8_t>> sent;
  for (uint32_t i=0;i<seconds;++i) {
   tick(1000);
   sent.insert(sent.end(),controller.tx.begin(),controller.tx.end());
   echo_all();
  }
  return sent;
 }
};

// Synthetic frame from its first twelve bytes; the checksum is computed.
protocol::Frame make(std::array<uint8_t,12> bytes) {
 protocol::Frame f{}; std::copy(bytes.begin(),bytes.end(),f.begin());
 protocol::update_checksum(f); return f;
}
void set(LgNumber& n, float v) { static_cast<number::Number&>(n).control(v); }
void choose(LgSelect& s, const char* option) { static_cast<select::Select&>(s).control(option); }
using protocol::Field;

void test_capability_reduction_invalidates_learned_count() {
 Fixture f; f.ready(); f.receive(cap8); f.receive(count8);
 CHECK(f.sensors[4].state==8);
 f.receive(cap4);
 CHECK(f.sensors[4].state==0);
 f.switches[10].turn_on(); CHECK(!f.switches[10].state);
 f.tick(500); CHECK(f.controller.tx.empty());
 f.receive(count4); CHECK(f.sensors[4].state==4);
}

void test_zone_command_survives_telemetry_and_queued_retry() {
 Fixture f(8); f.ready(); f.receive(all_off);
 f.switches[3].turn_on();
 f.receive(all_off); // Telemetry between local command and first send.
 CHECK(f.switches[3].state);
 f.tick(500); CHECK(f.controller.tx.size()==1);
 const auto sent=f.controller.tx.back();
 CHECK((sent[5]&0x40)!=0); // Documented zone 1 mask.
 f.tick(3001); // Missing echo queues a retry, without sending immediately.
 CHECK(f.controller.tx.size()==1);
 f.receive(all_off); CHECK(f.switches[3].state);
 f.tick(500); CHECK(f.controller.tx.size()==2);
 CHECK(f.controller.tx.back()==sent);
 protocol::Frame echo{}; std::copy(sent.begin(),sent.end(),echo.begin());
 f.receive(echo);
 f.receive(all_off); CHECK(!f.switches[3].state);
}

void test_discovery_order_and_explicit_override() {
 {
  Fixture f; f.ready(); f.receive(count8); CHECK(f.sensors[4].state==8);
  f.receive(cap4); CHECK(f.sensors[4].state==0);
  f.receive(count8); CHECK(f.sensors[4].state==0); // Rejected against known max.
  f.receive(count4); CHECK(f.sensors[4].state==4);
  f.receive(cap8); CHECK(f.sensors[4].state==4); // Maximum is not exact count.
 }
 {
  Fixture f(8); f.ready(); f.receive(cap4); f.receive(count4);
  CHECK(f.sensors[4].state==8); // Explicit installer override remains authoritative.
 }
}

void test_new_zone_command_waits_behind_exact_retry() {
 Fixture f(8); f.ready(); f.receive(all_off);
 f.switches[3].turn_on(); f.tick(500);
 const auto first=f.controller.tx.back();
 f.tick(3001);
 f.switches[4].turn_on(); f.receive(all_off);
 CHECK(f.switches[3].state && f.switches[4].state);
 f.tick(500); CHECK(f.controller.tx.back()==first);
 protocol::Frame echo{}; std::copy(first.begin(),first.end(),echo.begin());
 f.receive(echo); f.receive(all_off); f.tick(500);
 CHECK(f.controller.tx.size()==3);
 CHECK((f.controller.tx.back()[5]&0x60)==0x60);
 CHECK((first[5]&0x60)==0x40);
}

void test_four_zone_write_preserves_byte10_aliases() {
 Fixture f(4); f.ready(); f.receive(cap4);
 // Type-0 byte 10 aliases are unrelated to zones on a four-zone system.
 const protocol::Frame aliases{0xC8,0,0,0,0,0,5,20,0,0,0x1E,0,0xAA};
 f.receive(aliases); f.switches[3].turn_on(); f.tick(500);
 CHECK(f.controller.tx.size()==1);
 CHECK(f.controller.tx.back()[10]==0x1E);
 CHECK((f.controller.tx.back()[5]&0x78)==0x40);
 protocol::Frame echo{};
 std::copy(f.controller.tx.back().begin(),f.controller.tx.back().end(),echo.begin());
 CHECK(protocol::is_valid_frame(echo));
 f.receive(echo); f.tick(500); CHECK(f.controller.tx.size()==1);
}

void test_invalidated_count_does_not_reuse_old_zone_ownership() {
 Fixture f; f.ready(); f.receive(cap8); f.receive(count8); f.receive(all_off);
 f.switches[10].turn_on(); f.tick(500);
 CHECK((f.controller.tx.back()[10]&0x02)!=0); // Zone 8 previously owned and on.
 protocol::Frame echo{};
 std::copy(f.controller.tx.back().begin(),f.controller.tx.back().end(),echo.begin());
 f.receive(echo); f.controller.tx.clear();
 f.receive(cap4); CHECK(f.sensors[4].state==0);
 f.receive(cap8); f.receive(count8); // Rediscovery without a fresh status frame.
 f.switches[3].turn_on(); f.tick(500);
 CHECK(f.controller.tx.size()==1);
 CHECK((f.controller.tx.back()[5]&0x40)!=0);
 // The last unit shadow still has zone 8 off. Old local ownership must not
 // resurrect its former on state just because the count is learned again.
 CHECK((f.controller.tx.back()[10]&0x02)==0);
}

void test_status_fields_overlay_only_their_bits() {
 Fixture f; f.ready();
 LgSwitch humidifier; LgSelect ceiling;
 f.controller.set_field_switch(Field::Humidifier,&humidifier);
 f.controller.set_field_select(Field::CeilingHeight,&ceiling);
 // On, cooling. Filter sign and swirl set, defrost, outdoor active, ceiling height 1, 20 C.
 const auto unit=make({0xC8,0x02,0x21,0x04,0,0x04,0x45,20,0,0,0,0});
 f.receive(unit);
 CHECK(!humidifier.state); CHECK(ceiling.state=="1");
 humidifier.turn_on();
 auto sent=f.send_next();
 CHECK(sent[0]==0xA8); CHECK((sent[1]&0x01)!=0);
 CHECK(sent[2]==0x29); CHECK(sent[3]==0x04); CHECK((sent[5]&0x06)==0x04);
 CHECK((sent[6]&0xC0)==0x40);
 choose(ceiling,"3");
 sent=f.send_next();
 CHECK((sent[6]&0xC0)==0xC0); CHECK(sent[2]==0x29);
 f.receive(unit); // The unit is authoritative again once the write was echoed.
 CHECK(!humidifier.state); CHECK(ceiling.state=="1");
 f.tick(500); CHECK(f.controller.tx.empty()); // Publishing received state is not a command.
}

void test_slave_sends_status_field_with_slave_header() {
 Fixture f(0,true);
 LgSwitch energy; f.controller.set_field_switch(Field::EnergySaving,&energy);
 f.controller.timeouts.at("initial_send")();
 f.receive(all_off); f.send_next();
 f.receive(all_off); energy.turn_on();
 const auto sent=f.send_next();
 CHECK(sent[0]==0x28); CHECK((sent[5]&0x02)!=0);
}

void test_type_a_fields_wait_for_unit_settings() {
 Fixture f; f.ready();
 LgNumber auto_change,min_setpoint; LgSwitch robot;
 f.controller.set_field_number(Field::AutoChangeTemperature,&auto_change);
 f.controller.set_field_number(Field::MinSetpoint,&min_setpoint);
 f.controller.set_field_switch(Field::RobotCleanAuto,&robot);
 set(auto_change,5);
 f.tick(500); CHECK(f.controller.tx.empty()); // Never built from an empty frame.
 const auto unit=make({0xCA,0x07,1,2,3,4,5,0x21,0x43,0x82,0xF1,0x08});
 f.receive(unit);
 CHECK(auto_change.state==5); CHECK(min_setpoint.state==16); CHECK(!robot.state);
 auto sent=f.send_next();
 const std::vector<uint8_t> expected{0xAA,0x07,1,2,3,4,5,0x21,0x43,0x85,0xF1,0x08};
 CHECK(std::equal(expected.begin(),expected.end(),sent.begin()));
 set(min_setpoint,18); robot.turn_on();
 sent=f.send_next();
 CHECK(sent[10]==0xF3); CHECK(sent[11]==0x0C); CHECK(sent[9]==0x85);
}

void test_type_b_fields_write_without_query_bit() {
 Fixture f; f.ready();
 LgSelect overcooling; LgNumber pressure;
 f.controller.set_field_select(Field::Overcooling,&overcooling);
 f.controller.set_field_number(Field::StaticPressure,&pressure);
 f.receive(make({0xCB,0x01,0x48,0x70,0x71,0x72,0x04,0x00,0x12,0x03,0x45,0x06}));
 CHECK(overcooling.state=="1"); CHECK(pressure.state==5);
 set(pressure,9); choose(overcooling,"3");
 const auto sent=f.send_next();
 const std::vector<uint8_t> expected{0xAB,0x01,0xC8,0,0,0,0x04,0x00,0x12,0x03,0x49,0x06};
 CHECK(std::equal(expected.begin(),expected.end(),sent.begin()));
}

void test_type_c_write_sets_changed_flag() {
 Fixture f; f.ready();
 LgSwitch mosquito; LgNumber high,deadband;
 f.controller.set_field_switch(Field::MosquitoAway,&mosquito);
 f.controller.set_dual_setpoint_high_number(&high);
 f.controller.set_deadband_number(&deadband);
 mosquito.turn_on();
 f.tick(500); CHECK(f.controller.tx.empty());
 const auto unit=make({0xCC,0xAB,0x01,0x64,0x91,0x29,24,22,0x50,0x2B,0x25,0x01});
 f.receive(unit);
 auto sent=f.send_next();
 const std::vector<uint8_t> expected{0xAC,0xAB,0x01,0x64,0x91,0x29,24,22|0x80,0x50,0x2B,0x25,0x03};
 CHECK(std::equal(expected.begin(),expected.end(),sent.begin()));
 f.receive(unit);
 CHECK(!mosquito.state); CHECK(high.state==24.5f); CHECK(deadband.state==2.5f);
 set(high,26); set(deadband,3);
 sent=f.send_next();
 CHECK(sent[6]==26); CHECK(sent[8]==0x00); CHECK(sent[10]==0x30); CHECK(sent[11]==0x01);
}

void test_installer_settings_match_documented_examples() {
 Fixture f; f.ready();
 LgSwitch auto_start,fan_continuous; LgNumber dry_contact,silent;
 f.controller.set_field_switch(Field::AutoStart,&auto_start);
 f.controller.set_field_number(Field::SimpleDryContact,&dry_contact);
 f.controller.set_field_number(Field::SilentMode,&silent);
 f.controller.set_field_switch(Field::FanContinuous,&fan_continuous);
 auto_start.turn_on(); set(silent,2);
 f.tick(500); CHECK(f.controller.tx.empty());
 f.receive(make({0xCD,0,0,0,0,0,0,0,0,0,0,0}));
 // protocol.md: "Set 39 (indoor unit auto start) to 1: ad 01".
 auto sent=f.send_next();
 CHECK((sent==std::vector<uint8_t>{0xAD,0x01,0,0,0,0,0,0,0,0,0,0,0xFB}));
 // "Set 41 (simple dry contact setting) to 3: ad 00 00 00 00 00 03"; auto start is kept.
 set(dry_contact,3);
 sent=f.send_next();
 CHECK(sent[1]==0x01); CHECK(sent[6]==0x03);
 // "Set 48 (silent mode) to 2: ae 10 00 02". Other subtypes are not a settings shadow.
 f.receive(make({0xCE,0x00,0xFF,0xFF,0xFF,0,0,0,0,0,0,0}));
 f.tick(500); CHECK(f.controller.tx.empty());
 f.receive(make({0xCE,0x10,0,0,0,0,0,0,0,0,0,0}));
 sent=f.send_next();
 CHECK((sent==std::vector<uint8_t>{0xAE,0x10,0,0x02,0,0,0,0,0,0,0,0,0x95}));
 // "Set 46 (fan continuous) to 1: ae 10 00 00 02"; silent mode is kept.
 fan_continuous.turn_on();
 sent=f.send_next();
 CHECK(sent[3]==0x02); CHECK(sent[4]==0x02);
}

void test_extended_fan_speeds() {
 {
  Fixture f; f.ready();
  // Wall unit advertising every fan speed including low-medium, medium-high and power.
  f.receive(make({0xC9,0x04,0xE8,0x3F,0,0,0x18,0,0,0,0,0}));
  CHECK(f.controller.custom_fan_modes.size()==3);
  f.receive(make({0xC8,0x02|(5<<5),0,0,0,0,5,20,0,0,0,0}));
  CHECK(f.controller.has_custom_fan_mode());
  CHECK(std::strcmp(f.controller.custom_fan_mode,"Low-Medium")==0);
  climate::ClimateCall call; call.custom_fan_mode="Power"; f.controller.control(call);
  auto sent=f.send_next();
  CHECK((sent[1]>>5)==7);
  climate::ClimateCall standard; standard.fan_mode=climate::CLIMATE_FAN_LOW;
  f.controller.control(standard);
  sent=f.send_next();
  CHECK((sent[1]>>5)==0); CHECK(!f.controller.has_custom_fan_mode());
 }
 {
  // Without the capability the nearest standard speed is shown and the raw code survives.
  Fixture f; f.ready();
  f.receive(make({0xC8,0x02|(7<<5),0,0,0,0,5,20,0,0,0,0}));
  CHECK(!f.controller.has_custom_fan_mode());
  CHECK(f.controller.fan_mode==climate::CLIMATE_FAN_HIGH);
  const auto sent=f.advance(21);
  CHECK(sent.size()==1); CHECK((sent[0][1]>>5)==7);
 }
}

void test_momentary_button_is_cleared_in_next_frame() {
 Fixture f; f.ready();
 LgButton filter_reset; f.controller.set_filter_reset_button(&filter_reset);
 f.receive(all_off);
 filter_reset.press();
 auto sent=f.send_next();
 CHECK((sent[2]&0x02)!=0);
 f.receive(make({0xC8,0,0x02,0,0,0,5,20,0,0,0,0})); // Even if the unit mirrors the bit.
 const auto later=f.advance(21);
 CHECK(later.size()==1); CHECK((later[0][2]&0x02)==0);
}

void test_owned_reservations_switch_the_unit() {
 Fixture f; f.ready();
 LgNumber on,off;
 f.controller.set_on_timer_number(&on); f.controller.set_off_timer_number(&off);
 f.receive(make({0xC8,0x02|(4<<2),0,0,0,0,5,20,0,0,0,0})); // On, heating.
 CHECK(f.controller.mode==climate::CLIMATE_MODE_HEAT);
 set(off,30);
 auto sent=f.send_next();
 CHECK((sent[3]&0x10)!=0); CHECK(sent[8]==(2<<3)); CHECK(sent[9]==30);
 // The unit mirrors the reservation back, as it does for the sleep timer.
 f.receive(make({0xC8,0x02|(4<<2),0,0x10,0,0,5,20,(2<<3),30,0,0}));
 auto frames=f.advance(10*60);
 CHECK(!frames.empty());
 // Announced once; heartbeats keep the flag without repeating stale minutes.
 for (auto& frame : frames) { CHECK((frame[3]&0x10)!=0); CHECK(frame[8]==0); CHECK(frame[9]==0); }
 CHECK(off.state==20 || off.state==21);
 CHECK(f.controller.mode==climate::CLIMATE_MODE_HEAT);
 frames=f.advance(21*60);
 CHECK(f.controller.mode==climate::CLIMATE_MODE_OFF); CHECK(off.state==0);
 bool announced_end=false;
 for (auto& frame : frames) {
  if ((frame[1]&0x02)==0 && frame[8]==(2<<3) && frame[9]==0) announced_end=true;
 }
 CHECK(announced_end);
 CHECK((frames.back()[3]&0x10)==0); CHECK((frames.back()[1]&0x02)==0);

 // The unit reports itself off and keeps the heating mode bits. Turn it back on in 10 minutes.
 f.receive(make({0xC8,(4<<2),0,0,0,0,5,20,0,0,0,0}));
 set(on,10);
 sent=f.send_next();
 CHECK(sent[8]==(1<<3)); CHECK(sent[9]==10);
 frames=f.advance(11*60);
 CHECK(f.controller.mode==climate::CLIMATE_MODE_HEAT);
 CHECK((frames.back()[1]&0x1E)==(0x02|(4<<2)));
}

void test_foreign_reservation_is_displayed_not_executed() {
 Fixture f; f.ready();
 LgNumber off; LgButton clear;
 f.controller.set_off_timer_number(&off); f.controller.set_clear_reservations_button(&clear);
 const auto unit=make({0xC8,0x02,0,0x10,0,0,5,20,(2<<3),45,0,0});
 f.receive(unit);
 CHECK(off.state==45);
 f.advance(46*60);
 CHECK(off.state==0); CHECK(f.controller.mode==climate::CLIMATE_MODE_COOL);
 set(off,30); f.send_next();
 clear.press();
 const auto sent=f.send_next();
 CHECK(sent[8]==(4<<3)); CHECK((sent[3]&0x10)==0); CHECK(off.state==0);
 f.advance(31*60);
 CHECK(f.controller.mode==climate::CLIMATE_MODE_COOL); // Cleared timer never fires.
}

void test_two_thermistor_setting() {
 sensor::Sensor room; room.state=22;
 Fixture f(0,false,&room); f.ready();
 LgSwitch two_th; f.controller.set_thermistor_2th_switch(&two_th);
 f.receive(all_off);
 two_th.turn_on();
 auto sent=f.send_next();
 CHECK((sent[6]&0x30)==0x20); CHECK((sent[7]&0x3F)==24);
 two_th.turn_off();
 sent=f.send_next();
 CHECK((sent[6]&0x30)==0x10);
}

void test_power_and_capability_sensors() {
 Fixture f; f.ready();
 sensor::Sensor power; text_sensor::TextSensor capabilities;
 binary_sensor::BinarySensor zone_type_new;
 f.controller.set_power_sensor(&power);
 f.controller.set_capabilities_text_sensor(&capabilities);
 f.controller.set_zone_type_new_binary_sensor(&zone_type_new);
 // protocol.md Type 7 example, displayed by the PREMTB100 as 123.5 kW.
 f.receive({0xCF,0x00,0x12,0x34,0x56,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3E});
 CHECK(std::fabs(power.state-123.456f)<0.001f);
 f.receive(cap8);
 CHECK(capabilities.state=="C9.12.E0.00.00.00.00.00.08.00.00.00.96");
 f.receive(make({0xC8,0,0,0,0,0x80,5,20,0,0,0,0}));
 CHECK(zone_type_new.state);
 static_cast<Component&>(f.controller).dump_config();
}

// Frames captured on 22 November 2026 from an LG B70AWY ducted unit (indoor B70AWYN9L6) with an
// LG wall controller as master, this controller as slave, and four configured zones of which
// zones 1 and 4 were open. Literal bytes including checksums; the unit was in fan-only mode.
void test_captured_ducted_unit_frames() {
 const protocol::Frame master{0xA8,0x0A,0x80,0,0,0xC8,0x05,0x1A,0,0,0,0,0x4C};
 const protocol::Frame unit{0xC8,0x0A,0x00,0,0,0xC8,0x05,0x1A,0,0,0,0,0xEC};
 const protocol::Frame type4{0xCC,0xCA,0x41,0,0,0,0x14,0x10,0,0x2E,0x20,0,0x1C};
 const protocol::Frame type7{0xCF,0,0,0,0,0,0,0,0,0,0,0,0x9A};
 Fixture f(4,true);
 sensor::Sensor filter_hours,room,power; binary_sensor::BinarySensor zone_type_new;
 f.controller.set_filter_time_remaining_sensor(&filter_hours);
 f.controller.set_type4_room_temperature_sensor(&room);
 f.controller.set_power_sensor(&power);
 f.controller.set_zone_type_new_binary_sensor(&zone_type_new);
 f.controller.timeouts.at("initial_send")();
 f.receive(master); f.send_next(); // A slave announces its restored state once.
 f.receive(unit); f.receive(type4); f.receive(type7);
 CHECK(f.controller.mode==climate::CLIMATE_MODE_FAN_ONLY);
 CHECK(f.controller.fan_mode==climate::CLIMATE_FAN_LOW);
 CHECK(f.controller.target_temperature==20);
 CHECK(f.switches[3].state && !f.switches[4].state && !f.switches[5].state && f.switches[6].state);
 CHECK(zone_type_new.state);
 CHECK(filter_hours.state==458); CHECK(room.state==23); CHECK(power.state==0);
 // The wall controller sets the vertical swing bit in every frame and the unit never does.
 // Before the capabilities are known that reads as a swing mode.
 f.receive(master); CHECK(f.controller.swing_mode==climate::CLIMATE_SWING_VERTICAL);
 // The unit's capabilities message: ducted, no swing, no extra fan speeds.
 f.receive({0xC9,0x12,0xEB,0x5C,0x26,0x3C,0x00,0x84,0xEF,0x57,0x0F,0x83,0xB5});
 CHECK(f.controller.custom_fan_modes.empty());
 f.receive(master); CHECK(f.controller.swing_mode==climate::CLIMATE_SWING_OFF);
 f.tick(500); CHECK(f.controller.tx.empty());
 // Home Assistant asked for fan high right after a master heartbeat. The installed firmware
 // sent this exact frame and both the wall controller and the unit followed it. The
 // unsupported swing bit goes out as the wall controller last sent it.
 f.receive(master);
 climate::ClimateCall call; call.fan_mode=climate::CLIMATE_FAN_HIGH; f.controller.control(call);
 auto sent=f.send_next();
 CHECK((sent==std::vector<uint8_t>{0x28,0x4B,0x80,0,0,0xC8,0x05,0x14,0,0,0,0,0x81}));
 // Opening zone 2 changes only its own bit.
 f.receive(master);
 f.switches[4].turn_on();
 sent=f.send_next();
 CHECK(sent[5]==0xE8); CHECK(sent[10]==0); CHECK(sent[2]==0x80);
}

int main() {
 test_captured_ducted_unit_frames();
 test_status_fields_overlay_only_their_bits();
 test_slave_sends_status_field_with_slave_header();
 test_type_a_fields_wait_for_unit_settings();
 test_type_b_fields_write_without_query_bit();
 test_type_c_write_sets_changed_flag();
 test_installer_settings_match_documented_examples();
 test_extended_fan_speeds();
 test_momentary_button_is_cleared_in_next_frame();
 test_owned_reservations_switch_the_unit();
 test_foreign_reservation_is_displayed_not_executed();
 test_two_thermistor_setting();
 test_power_and_capability_sensors();
 test_zone_command_survives_telemetry_and_queued_retry();
 test_capability_reduction_invalidates_learned_count();
 test_discovery_order_and_explicit_override();
 test_new_zone_command_waits_behind_exact_retry();
 test_four_zone_write_preserves_byte10_aliases();
 test_invalidated_count_does_not_reuse_old_zone_ownership();
 std::cout << "lg_controller_test: all checks passed\n";
}
