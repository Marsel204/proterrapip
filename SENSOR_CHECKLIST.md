# ESP32 DevKit component checks

Sketch: `firmware/component_test/component_test.ino`. Target: classic ESP32 DevKit
(ESP32-WROOM, Arduino board **ESP32 Dev Module**), not ESP32-S3/C3.
Serial Monitor: **115200 baud**. Newline setting can be any option.

## Before powering up

- Connect all logic grounds together. ESP32 GPIO signals must stay within 0–3.3 V.
- Soil analog outputs go to GPIO34 and GPIO35. Power compatible soil modules at
  3.3 V; otherwise verify their output voltage and provide suitable conditioning.
  GPIO34/35 have no internal pull resistors, so a disconnected input can float.
- Flow signal goes to GPIO27. Check the actual sensor datasheet: a 5 V push-pull
  output needs a divider or level shifter. A verified open-collector output can
  use a pull-up to 3.3 V only if its module has no pull-up to 5 V. A 4.7–10 kΩ
  external pull-up is usually more reliable than the ESP32 internal pull-up.
- LCD SDA/SCL go to GPIO21/22. Many 5 V I2C backpacks pull both lines up to 5 V;
  use a suitable bidirectional I2C level shifter with pull-ups on each side.
  Do not assume a 5 V LCD will work correctly when powered from 3.3 V.
- Rotary CLK/DT/SW go to GPIO18/19/23; common goes to GND. Power a compatible
  rotary module at 3.3 V so its pull-ups cannot put 5 V on the inputs.
- Pump uses an active-HIGH MOSFET driver at GPIO25, a separate suitable supply,
  common ground, and a flyback diode across a DC motor (cathode to positive).
  Use a driver compatible with 3.3 V logic and a gate pull-down to keep it off
  during reset. The software timeout runs only while firmware is executing.
- LEDs on GPIO13/14/32 each need a series resistor. GPIO33 drives an active
  buzzer through the S8050 with a base resistor; a passive buzzer needs a tone
  waveform instead of this sketch's steady HIGH signal.

## Test in this order

Start with the pump supply disconnected. Upload the sketch, open Serial Monitor,
and press EN/reset to see the startup report. No sensor can be declared working
from compilation alone; record the observations below.

| Component | Action | Expected result / what to record |
| --- | --- | --- |
| Soil 1, GPIO34 | Type `s`. Move the sensing portion between dry and moist soil; keep its electronics dry. | Calibrated (2-pt linear, NVS `soil_cal`): DRY dirt = `2545.37` (0%), WET = `1011.15` (100%); `pct = constrain(-0.065180 * raw + 165.9064, 0, 100)`. |
| Soil 2, GPIO35 | Repeat using sensor 2. | Calibrated (2-pt linear, NVS `soil_cal`): DRY dirt = `2444.57` (0%), WET = `1049.60` (100%); `pct = constrain(-0.071686 * raw + 175.2418, 0, 100)`. |
| Flow, GPIO27 | With pump off, pass water through the sensor in the marked direction using an external/gravity source. | Pulses increase during flow and return to zero when flow stops. Total liters increase. Zero pulses without water is normal. |
| Rotary | Turn slowly both ways, then press once. | Plant selection moves in opposite directions; one press prints one confirmation. Some encoders have two detents per full quadrature cycle. |
| LCD, GPIO21/22 | Type `i`, then watch for 12 seconds. | Scanner reports an address; all four LCD pages appear. Default is `0x27`; change `LCD_ADDRESS` to the actual backpack address (often `0x3F`). An ACK alone does not prove the display works. |
| LEDs, GPIO13/14/32 | Type `l`. | All LEDs light for 0.5 seconds. Then green stays on, yellow indicates measured flow, red indicates pump ON. |
| Buzzer, GPIO33 | Type `b`. | Active buzzer sounds for 0.5 seconds, then stops. |
| Pump, GPIO25 | After checking wiring, prime the pump and connect its supply. Type `1`, then `0`. Repeat `1` and allow timeout. | Starts only on command, stops on `0`, and stops after about 5 seconds with a protection message. Never run a pump dry. |
| Wi-Fi | Fill `WIFI_SSID` and `WIFI_PASSWORD` locally, rebuild/upload with a 2.4 GHz network. | Serial prints connected IP; LCD reports connected. Empty SSID intentionally skips Wi-Fi. Keep credentials out of commits. |

## Interpret readings

- Soil values are **raw ADC counts (0–4095), not moisture percentages**. Constant
  near-zero/full-scale readings suggest a wiring, supply, saturation, or sensor
  problem. Midrange or noisy readings alone do not establish sensor health.
  If a reading stays fixed, swap the soil modules between inputs to see whether
  the fault follows the sensor or the channel. Power down before rewiring.
- Calibrate each soil sensor separately in the intended soil. Once dry and wet
  endpoints are measured, a basic relative scale is
  `100 * (raw - dry) / (wet - dry)`, clamped to 0–100, with unequal endpoints.
  This is a relative estimate, not laboratory volumetric water content.
- `PULSES_PER_LITER = 450` is a starting assumption for a YF-S201. Collect a known
  measured volume and count pulses; calibrated pulses/liter = pulses / liters.
  The sketch derives both rate and volume from that single constant.
- A lit but blank LCD may need its contrast potentiometer adjusted. If scanning
  finds no address, check power, common ground, SDA/SCL, and level shifting.
- Slow encoder turns should be checked first; polling can miss fast turns while
  serial output or I2C work is in progress.

## Build

Required: Arduino ESP32 core and **LiquidCrystal I2C by Frank de Brabander 1.1.2**.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/component_test
```

This diagnostic sketch does not automatically irrigate based on soil readings.
Only serial command `1` starts the pump. It preserves your original pin map,
removes the blocking LED delay, debounces the rotary button, uses an ESP32
critical section for pulse snapshots, and skips LCD writes if its address does
not acknowledge. The timeout is a software limit, not a hardware cutoff.
