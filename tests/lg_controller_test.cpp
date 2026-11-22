#include "esphome/components/lg_controller/lg-controller.h"
#include <cstdlib>
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
 explicit Fixture(uint8_t count=0) : controller(
  &pin,nullptr,&selects[0],&selects[1],&selects[2],&selects[3],&selects[4],
  &numbers[0],&numbers[1],&numbers[2],&numbers[3],nullptr,&numbers[4],
  &sensors[0],&sensors[1],&sensors[2],&sensors[3],
  &binaries[0],&binaries[1],&binaries[2],&binaries[3],
  &switches[0],&switches[1],&switches[2],
  &switches[3],&switches[4],&switches[5],&switches[6],
  &switches[7],&switches[8],&switches[9],&switches[10],false,false,0,count,0,false) {
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
};

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

int main() {
 test_zone_command_survives_telemetry_and_queued_retry();
 test_capability_reduction_invalidates_learned_count();
 test_discovery_order_and_explicit_override();
 test_new_zone_command_waits_behind_exact_retry();
 test_four_zone_write_preserves_byte10_aliases();
 test_invalidated_count_does_not_reuse_old_zone_ownership();
 std::cout << "lg_controller_test: all checks passed\n";
}
