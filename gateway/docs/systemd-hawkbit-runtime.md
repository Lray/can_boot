# T527 systemd HawkBit runtime

## Ownership boundary

```text
systemd-networkd + wpa_supplicant -> IP route
                                        |
hawkbit.conf -> SWUpdate Suricatta -> HawkBit DDI
                                        |
                                  Remote Handler
                                        v
                         mcu-updater
          bind -> receive -> derive SHA-256 -> atomic publish
                                      -> UDS update -> confirm
                                               |
                    token-signer-daemon <- OP-TEE TA
```

`mcu-updater` no longer starts Wi-Fi, SWUpdate, or the token signer.
It has exactly one job: bind one SWUpdate Remote Handler endpoint per registered
LSS identity; accept one
bounded, already verified `image.bin` stream; derive its SHA-256 for local
audit and resume identity; publish `package-input-v1` atomically; and run the
serialized MCU update state machine in the same process. It does not
implement DDI, HTTP, HawkBit authentication, image signatures, or MCU
boot policy.

| Unit | Ownership |
| --- | --- |
| `mcu-update-wifi.service` | Brings `wlan0` up. |
| `mcu-update-wpa-supplicant.service` | Foreground WPA process, permanently restarted by systemd. |
| `systemd-networkd` + `80-mcu-update-wlan0.network` | DHCP and route maintenance. |
| `mcu-update-network-online.service` | Blocks DDI start until `wlan0` has a route. |
| `mcu-update-token-signer.service` | Long-lived OP-TEE signer; it validates the TA key and then drops identity internally. |
| `mcu-updater.service` | Loads the persistent identity registry and binds one endpoint per MCU; every transaction gets an internal UUID and ephemeral job directory. |
| `mcu-update-swupdate.service` | Long-lived official SWUpdate 2019.11 Suricatta client, `Restart=always`. |

## Fixed HawkBit device identity

Install [`config/hawkbit/hawkbit.conf.example`](../config/hawkbit/hawkbit.conf.example)
as `/etc/mcu-update/hawkbit.conf`, owned by `root:root`, mode `0600`:

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

## Release-independent updater configuration

`/etc/mcu-update/mcu-updater.conf` contains device-local settings only:

```ini
MCU_UPDATE_WORK_ROOT=/run/mcu-update/jobs
MCU_UPDATE_CAN_IFNAME=awlink0
MCU_UPDATE_REMOTE_ENDPOINT_BASE=ipc:///run/mcu-update/remote-handler/mcu-v1
```

This is an endpoint base. The service appends the canonical LSS identity. The
signed `sw-description` uses the corresponding basename in its `data` field.
The official SWUpdate 2019.11 build must set
`CONFIG_SOCKET_REMOTE_HANDLER_DIRECTORY="/run/mcu-update/remote-handler/"`.
The service keeps `TMPDIR` on the same directory for extraction and for newer
official versions that resolve the Remote Handler directory from it. No
Remote Handler source patch is used.

Release size, digest, and transaction identifiers are deliberately absent.
SWUpdate first verifies the signed description and complete artifact, its
Remote Handler announces the actual image size, and the resident updater
creates a fresh internal UUID. The receiver recomputes SHA-256 from exactly
the received bytes; the update engine recomputes it again as the MCU resume
payload identity. Temporary job data lives below the systemd runtime directory
and is removed after each transaction.

This product deliberately does not use SWUpdate `hardware-compatibility` for
individual MCUs. The signed LSS identity selects the MCU; the Gateway verifies
the same identity through UDS before and after programming. MCUboot remains the
image authenticity and boot-policy authority.

1. Create `image.bin` only with `E:\T527\can_boot\tools\image.py`. Then build the
   release using `scripts/build_mcu_hawkbit_swu_wsl.sh`, passing the target LSS
   identity. The script uses official `swugenerator` and runs `swupdate -c`.

2. Run `scripts/deploy_mcu_update_systemd_adb.ps1` with the SWU public trust
   PEM, root-only `hawkbit.conf`, and root-only WPA configuration. Deployment
   is independent of any particular release image.

3. Keep the existing `scripts/hawkbit_action_wsl.sh` as the sole Management
   API uploader. The added `provision` command creates the stable target once
   from an owner-only copy of `hawkbit.conf`; `assign` remains the package
   upload and action-assignment command.

   ```bash
   ./scripts/hawkbit_action_wsl.sh provision \
       --hawkbit-config /home/lirui/.config/mcu-update/hawkbit.conf \
       --target-address <board-wlan-ip> --state ~/.local/state/mcu-update-hawkbit/t527.env
   ./scripts/hawkbit_action_wsl.sh assign \
       --artifact /absolute/path/to/release.swu \
       --state ~/.local/state/mcu-update-hawkbit/t527.env
   ```

4. Run the production HIL entry point after deployment. It assigns the SWU,
   watches the real systemd services and journal, and requires both HawkBit's
   terminal result and the updater's post-reboot MCU confirmation:

   ```powershell
   .\scripts\run_mcu_update_hil_adb.ps1 `
       -SwuPath <release.swu> `
       -HawkBitStateFile /home/lirui/.local/state/mcu-update-hawkbit/t527.env
   ```

## Deployment guardrails

`deploy_mcu_update_systemd_adb.ps1` requires PowerShell 7 and refuses to write
anything unless PID 1 is `systemd`; systemd-networkd, `nc`, and `/sbin/swupdate`
exist; inputs hash correctly; the DDI configuration is safe; and the board can
open TCP to the configured server. It also rejects an SWUpdate binary that
does not contain the required official Remote Handler directory setting. The current BusyBox/ADB HIL rootfs is not a
systemd target, so the script fails before staging on that image. Add systemd,
systemd-networkd and the listed runtime files to the production Buildroot
image first.

For an in-place migration, the deployer discovers the superseded update-unit
prefix from its SWUpdate Remote Handler dependency, disables and removes that
unit cohort, and moves its configuration, helper directory, and persistent
runtime root to sibling `.retired` paths before enabling the new services.

Building `-DENABLE_SWUPDATE_BRIDGE=ON` requires the target SDK's `libzmq`
headers and library. Missing `zmq.h` or `libzmq` is a build blocker, not a
reason to replace the official Remote Handler protocol.
