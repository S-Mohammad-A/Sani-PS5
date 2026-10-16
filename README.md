# ESP32-S3 Xbox → PS5 Remote Play

ESP32-S3 firmware: Xbox One Series X/S controller over BLE, Connected to PlayStation 5 With Remote Play (controller-only).
Just for First time Set-up esp32-s3 board - Next time just Push the Button or TAP "Connect to PS5" Button on WEB-Portal.

## Requirements

- ESP32-S3 board
- ESP-IDF **v5.4.x**
- Xbox One / Series controller (Bluetooth)
- PS5 and setup device on the same Wi-Fi (after STA config) - Or Connect PS5 To ESP32-AP (ESP32-PS5-Setup)

## Build & flash

1. Install ESP-IDF v5.4 and enable the environment.
2. Open a terminal in the project root.
3. `idf.py set-target esp32s3`
4. `idf.py build`
5. `idf.py -p PORT flash monitor`  
   Windows example: `idf.py -p COM5 flash monitor`

## First-time setup

1. Power the board. If no Wi-Fi is saved, SoftAP starts:
   - SSID: `ESP32-PS5-Setup`
   - Password: `12345678` (changeable in portal)
2. Connect to the AP → open `http://192.168.4.1`
3. Portal order:
	if you have yuor own network:
		1. **Wi-Fi** — home SSID/password → Save → reboot
	if you have NOT :
		1. Connect PS5 to ESP32-PS5-Setup

	2. **PS5 Console IP** — Ping & Save IP
		See your Consol IP from Setting → Network → Connection Status → View Connection Status → IPv4 address
		
	3. **PS5 registration** — PIN + PSN Account ID (base64) → Save & Register
		"Remote Play PIN" Can Get From Setting → System → Remote Play → Pair Device
			If dont Have Yuor "PSN Account ID (base64)" :
				Jailbrooken PS5,Eassily can Get  "PIN" + "PSN Account ID" From OnionHEN,pizzaHEN,elfArsenal or same payload from Remote Play Menu
				NON-Jailbreak --> Use this → https://pyrosoft.pro/psnid/
				
   4. **Remote Play** — Connect
		if "IP" & "PIN" & "PSN Account ID" is correct,Connection established

   5. **Controller** — Show name, battery, rumble(OFF or ON)
   6. **Stick deadzone** — drift, triggers, button remap
   7. **Hardware & defaults** — RGB GPIO, button GPIO, AP password, reset defaults
   
4. Pair the Xbox controller (hold pair buttin on the pad ESP32 Automatically get Connected).
5. Connect to PS5 from Web Portal or short-press the hardware button (default GPIO14).
6. if forget AP password short-press Default-RESET button to reset to 12345678 (default GPIO13).
	Hint: all button work with Ground .No need any resistor.

## Hardware buttons

| GPIO | Function |
|------|----------|
| **14** (default, configurable) | Short press while portal is open → Connect to PS5 |
| **13** (fixed) | Press → factory reset (drift, mapping, AP password, default GPIOs) and reboot |

## LED (default GPIO48)

| Color | Meaning |
|--------|---------|
| Yellow | AP mode |
| Red | Wi-Fi OK, not registered |
| Green | Wi-Fi OK, registered |
| Blue | Remote Play connected |
| Purple blink | Session ended / portal ready |
Hint: after Remote successfull LED get Color From Game/Consol Like PS4/PS5 Controller.



## Notes

- Controller-only (no video/audio).
- GPIO and SoftAP password changes: save in portal; **reboot** for GPIO; AP password applies on next SoftAP start.
- **Reset defaults** restores drift, button mapping, AP password, and default GPIO numbers.

## Credits

Built with:

- **Chiaki** — PlayStation Remote Play client
- **Bluepad32** — Bluetooth gamepads on ESP32

Thanks to the authors and contributors of these projects.
