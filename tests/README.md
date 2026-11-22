# Offline regression tests

Run `sh tests/run-native-tests.sh` with an existing C++17 compiler. No network,
ESPHome installation, GPIO device, serial port or HVAC access is used.

`lg_protocol_test.cpp` exercises the pure protocol layer. Four fixed frames are
copied from `protocol.md` at `e31f29f`; they are published examples, not captures
from Damian's installation. Their checksums are deliberately not regenerated.

`lg_controller_test.cpp` includes the production controller unchanged and drives
its public setup/loop entry points, registered scheduler callback and zone switch
callbacks. Frames are synthetic examples derived from the documented Type 0/1/3
tables, with literal checksum bytes. Tests cover capability/count ordering,
explicit-count precedence, stale telemetry during writes and queued retries,
newer local commands behind exact retries, and four-zone byte-10 preservation.

`fakes/` supplies only the external ESPHome boundary: queued UART bytes, recorded
transmissions, manually advanced clock/timers, entity state, preferences and
reboot notification. LG logic is never copied into the doubles. A fake reboot
records the request but does not restart the process; the principal regression
uses a capability *update* after initial discovery, which does not request a
reboot. Discovery-order tests inspect the immediate pre-reboot count only.

These tests do not validate ESPHome ABI/API compatibility, preference persistence,
actual scheduler execution, boot restoration, electrical timing or physical bus
behavior. Existing native/ESP32 firmware build jobs remain necessary. The
component test suppresses an existing signed/unsigned comparison warning in the
unrelated temperature-conversion implementation; other warnings are errors.
