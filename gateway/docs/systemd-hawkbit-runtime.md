# T527 systemd HawkBit runtime

## Ownership boundary

```text
systemd-networkd + wpa_supplicant -> IP route
                                        |
hawkbit.conf -> SWUpdate Suricatta -> HawkBit DDI
                                        |
                                  Remote Handler
                                        v
                         ecu-ota-orchestrator
          bind -> receive -> SHA-256 verify -> atomic publish -> worker
                                                           |
                    token-signer-daemon <- OP-TEE TA ------+
```

`ecu-ota-orchestrator` no longer starts Wi-Fi, SWUpdate, or the token signer.
It has exactly one job: bind the SWUpdate Remote Handler endpoint; accept the
bounded `image.bin` stream; verify its pre-approved size and SHA-256; publish
`gateway-input-v1` atomically; and run the existing worker. It does not
implement DDI, HTTP, HawkBit authentication, image signatures, or MCU
transport.

| Unit | Ownership |
| --- | --- |
| `ecu-ota-wifi.service` | Brings `wlan0` up. |
| `ecu-ota-wpa-supplicant.service` | Foreground WPA process, permanently restarted by systemd. |
| `systemd-networkd` + `80-ecu-ota-wlan0.network` | DHCP and route maintenance. |
| `ecu-ota-network-online.service` | Blocks DDI start until `wlan0` has a route. |
| `ecu-ota-token-signer.service` | Long-lived OP-TEE signer; it validates the TA key and then drops identity internally. |
| `ecu-ota-orchestrator.service` | One bound release session; it must not restart with a completed UUID. |
| `ecu-ota-swupdate.service` | Long-lived official SWUpdate 2019.11 Suricatta client, `Restart=always`. |

## Fixed HawkBit device identity

Install [`config/hawkbit/hawkbit.conf.example`](../config/hawkbit/hawkbit.conf.example)
as `/etc/ecu-ota/hawkbit.conf`, owned by `root:root`, mode `0600`:

```ini
HAWKBIT_SERVER_URL=http://<fixed-windows-lan-address>:18080
HAWKBIT_TENANT=DEFAULT
HAWKBIT_TARGET_ID=<stable-controller-id>
HAWKBIT_TARGET_TOKEN=<32-lowercase-hex>
```

`HAWKBIT_SERVER_URL` is a board-reachable fixed LAN address, normally a
Windows address protected by a DHCP reservation or a static assignment. It is
not `127.0.0.1` and not a WSL NAT address. Docker must publish port 18080 and
the Windows firewall must allow it. The deployment script uses the board's
`nc` TCP probe and refuses to start Suricatta if this exact address is not
reachable.

Management API credentials remain only in the owner-only WSL credential file
used by `scripts/hawkbit_action_wsl.sh`; they are never copied to the board.
`scripts/swupdate-suricatta` merely validates the root-owned configuration and
executes the official `/sbin/swupdate -u` Suricatta mode. It is not a second
DDI or HTTP client.

## Release activation

Each release has one new UUID and one pre-approved raw-image binding in
`/etc/ecu-ota/orchestrator.conf`:

```ini
ECU_OTA_JOB_ID=<new-uuid-v4>
ECU_OTA_EXPECTED_SIZE=<final-image.bin-byte-count>
ECU_OTA_EXPECTED_SHA256=<final-image.bin-sha256>
```

The deployment script derives the size and digest from the supplied final
`image.bin`, starts a fresh orchestrator, waits for the IPC endpoint, checks
board-to-HawkBit TCP, and only then starts resident SWUpdate. Do not reuse an
old UUID after any terminal result.

1. Build the release using `scripts/build_ecu_hawkbit_swu_wsl.sh`. It calls
   `can/scripts/make_ecu_mcuboot_bundle.py`, which delegates image signing to
   MCUboot `imgtool`; it then follows the official SWUpdate RSA-PSS + CPIO-CRC
   creation method and runs `swupdate -c`.

2. Run `scripts/deploy_ecu_ota_systemd_adb.ps1` with `image.bin`, the SWU
   public trust PEM, root-only `hawkbit.conf`, and root-only WPA configuration.

3. Keep the existing `scripts/hawkbit_action_wsl.sh` as the sole Management
   API uploader. The added `provision` command creates the stable target once
   from an owner-only copy of `hawkbit.conf`; `assign` remains the package
   upload and action-assignment command.

   ```bash
   ./scripts/hawkbit_action_wsl.sh provision \
       --hawkbit-config /home/lirui/.config/ecu-ota/hawkbit.conf \
       --target-address <board-wlan-ip> --state ~/.local/state/ecu-ota-hawkbit/t527.env
   ./scripts/hawkbit_action_wsl.sh assign \
       --artifact /absolute/path/to/release.swu \
       --state ~/.local/state/ecu-ota-hawkbit/t527.env
   ```

4. Use the same script for terminal action status. Success requires HawkBit
   terminal feedback and the worker's MCU confirmation result.

## Deployment guardrails

`deploy_ecu_ota_systemd_adb.ps1` requires PowerShell 7 and refuses to write
anything unless PID 1 is `systemd`; systemd-networkd, `nc`, and `/sbin/swupdate`
exist; inputs hash correctly; the DDI configuration is safe; and the board can
open TCP to the configured server. The current BusyBox/ADB HIL rootfs is not a
systemd target, so the script fails before staging on that image. Add systemd,
systemd-networkd and the listed runtime files to the production Buildroot
image first.

Building `-DENABLE_SWUPDATE_BRIDGE=ON` requires the target SDK's `libzmq`
headers and library. Missing `zmq.h` or `libzmq` is a build blocker, not a
reason to replace the official Remote Handler protocol.
