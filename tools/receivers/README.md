# Local Receiver Scripts (No Cloud)

These receivers run on a device in the same LAN as the ESP32 and store images locally on that device.

- ESP32 sends `POST /upload`
- `Content-Type: image/jpeg`
- Body is raw JPEG bytes

No internet/cloud upload is involved.

## 1) PC Receiver

Run on your PC:

```bash
python3 tools/receivers/pc_receiver.py
```

- Listens on `0.0.0.0:8080`
- Saves files to `captures_pc/`

Set ESP32 URL:

```text
http://<PC_LAN_IP>:8080/upload
```

## 2) Raspberry Pi Receiver

Run on your Raspberry Pi:

```bash
python3 tools/receivers/raspi_receiver.py
```

- Listens on `0.0.0.0:8080`
- Saves files to `captures_raspi/`

Set ESP32 URL:

```text
http://<RASPI_LAN_IP>:8080/upload
```

## 3) BeagleBone Receiver

Run on your BeagleBone:

```bash
python3 tools/receivers/beaglebone_receiver.py
```

- Listens on `0.0.0.0:8080`
- Saves files to `captures_beaglebone/`

Set ESP32 URL:

```text
http://<BEAGLEBONE_LAN_IP>:8080/upload
```

## ESP32 Configuration

Set these in menuconfig under WiFi Service:

- `WIFI_SERVICE_SSID`
- `WIFI_SERVICE_PASSWORD`
- `WIFI_SERVICE_UPLOAD_URL` (receiver URL shown above)

Then build and flash.
