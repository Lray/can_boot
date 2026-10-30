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
          bind -> receive into transaction memfd
                         -> official ASCII -> LSS_gateway
                         <- Node-ID <- Fastscan/select/query/deselect
                         -> UDS update -> handler ACK
                                               |
                         libteec -> OP-TEE token TA
```

`mcu-updater` binds the SWUpdate Remote Handler endpoint, accepts one bounded,
already verified `image.bin` stream into an anonymous file, queries the
resident LSS gateway to configure unassigned nodes and query the target by identity,
and runs the selected MCU update state
machine in the same process. It does not start Wi-Fi or SWUpdate, or
implement DDI, HTTP, HawkBit authentication, image signatures, or MCU
boot policy.

`LSS_gateway` uses fixed interface `awlink0` and owns the only LSS Master and official Gateway ASCII instances.
Its initialization and event loop follow official `CO_main_basic.c` and
`CO_epoll_interface.c`: the entry point connects
the Unix socket, CAN reception and periodic official processing. The original
`CO_gateway_ascii` and `CO_fifo` handle command parsing and the LSS state machine.
The gateway accepts one client at `/run/LSS_gateway/command.sock`; it performs
Fastscan only after a command, and receives no image bytes or identity mapping.

| Unit | Ownership |
| --- | --- |
| `mcu-update-wifi.service` | Brings `wlan0` up. |
| `mcu-update-wpa-supplicant.service` | Foreground WPA process, permanently restarted by systemd. |
| `systemd-networkd` + `80-mcu-update-wlan0.network` | DHCP and route maintenance. |
| `mcu-update-network-online.service` | Blocks DDI start until `wlan0` has a route. |
| `LSS_gateway.service` | Resident LSS Master and official Gateway ASCII owner on `awlink0`; systemd creates `/run/LSS_gateway` with mode `0750`. |
| `mcu-updater.service` | Resident image receiver and OTA executor; ordered after the LSS gateway and `tee-supplicant`; each Remote Handler transaction exists only in an anonymous in-memory file. |
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

## Fixed updater runtime

The updater uses the product paths, `awlink0`, and
`ipc:///run/mcu-update/remote-handler/image-install` directly. There is no
updater environment file or command-line option layer.

Release size, digest, and transaction identifiers are not runtime settings.
SWUpdate first verifies the signed description and complete artifact, its
Remote Handler announces the actual image size, and the resident updater
buffers exactly that many bytes in a `memfd`. The receiver does not recompute
an artifact digest. Closing the descriptor, process death, or power loss
discards the bytes automatically; no package path, UUID, retention policy, or
startup cleanup exists. The data mount is still required only because the
production executable is deployed there.

This product deliberately does not enable SWUpdate `hardware-compatibility`.
The signed remote artifact carries the target factory identity. After reception,
the updater extracts that identity and sends `lss_allnodes 100 2 1`,
`lss_switch_sel`, `lss_get_node`, and `lss_switch_glob 0` in sequence over one
exclusive connection to the gateway command socket. The official Gateway ASCII
state machine performs the corresponding LSS calls. The updater
requires an active Node-ID in `1..127` before calling the OTA function; it does
not initialize CANopen or LSS. OTA checks the factory identity through UDS
before and after reset. MCUboot retains the final image and boot checks.

The first command uses the official `100 ms` Fastscan step timeout, starts at
Node-ID `2`, and requests persistent storage. Configured nodes do not participate
in unconfigured-node Fastscan; identity selection and inquiry still retrieve their
active Node-ID. Client reception follows the official `cocomm` blocking-read
example with no independent addressing deadline. The reference client source is not vendored.
The complete command sequence is in the [gateway README](../README.md#进程与命令入口).

The fixed start at `2` supports the current single-MCU product. Multi-MCU production
requires a recoverable allocation strategy: restarting allocation at `2` after a
partially stored commissioning run can assign an already occupied Node-ID.

1. Build the MCU image only with the repository's official MCUboot
   `tools/image.py`. Create and sign the SWU only with `swugenerator`; use
   `swugenerator --help` as the command contract. No project wrapper may
   reproduce either implementation.

2. Run `scripts/deploy_mcu_update_systemd_adb.ps1` with the SWU public trust
   PEM, root-only `hawkbit.conf`, and root-only WPA configuration. Deployment
   is independent of any particular release image. It installs
   `LSS_gateway` and `mcu-updater`, and starts the two resident
   services with the gateway ordered before the updater.

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

4. Run the production HIL entry point after deployment. It assigns the SWU and
   requires HawkBit's terminal result after the updater confirms the running MCU image:

   ```powershell
   .\scripts\run_mcu_update_hil_adb.ps1 `
       -SwuPath <release.swu> `
       -HawkBitStateFile /home/lirui/.local/state/mcu-update-hawkbit/t527.env
   ```

## Deployment guardrails

`deploy_mcu_update_systemd_adb.ps1` requires PowerShell 7 and refuses to write
anything unless PID 1 is `systemd`; systemd-networkd, `nc`, and `/sbin/swupdate`
exist; inputs hash correctly; the DDI configuration is safe; and the board can
open TCP to the configured server. The current BusyBox/ADB HIL rootfs is not a
systemd target, so the script fails before staging on that image. Add systemd,
systemd-networkd and the listed runtime files to the production Buildroot
image first.

Building `-DENABLE_SWUPDATE_BRIDGE=ON` requires the target SDK's `libzmq` and
OP-TEE client library. The updater does not link SWUpdate progress or control
IPC clients.

## Synchronous result reporting

The sequence is:

1. Receive the complete artifact into the current transaction's anonymous file.
2. Request Fastscan/configuration and query the gateway by identity in one
   exclusive session, obtain a valid Node-ID and deselect, then run the selected
   MCU update, reconnect, and confirm the running image.
3. Return the Remote Handler final ACK only when every MCU step succeeds.
   Receive, protocol and MCU failures return NACK, so Suricatta reports
   `closed/failure` directly.
4. After a successful handler return, SWUpdate persists `ustate=1`, reports
   `proceeding/none`, and runs the post-update command that restarts SWUpdate.
5. The launcher reads `ustate=1` with `fw_printenv` and starts Suricatta with
   `-c 2`. Suricatta obtains the current action ID itself, reports
   `closed/success`, and resets `ustate` to `0`.

The updater has no SWUpdate progress/control client, HawkBit action state, or
HTTP/DDI implementation.

The [hawkBit DDI state table](https://github.com/eclipse-hawkbit/hawkbit/wiki/DDI-API)
maps proceeding to RUNNING and closed/success or closed/failure to terminal
states. Never send closed/none: hawkBit treats it as success.
The [official Suricatta documentation](https://sbabic.github.io/swupdate/suricatta.html)
requires a SWUpdate restart after installation so Suricatta can report the
persisted result. For this product, a successful Remote Handler return already
includes application-level MCU validation, so `STATE_INSTALLED` is promoted to
the successful startup confirmation `-c 2`.

The deployed SWUpdate and application share only the Remote Handler path:

```text
CONFIG_SOCKET_REMOTE_HANDLER_DIRECTORY="/run/mcu-update/remote-handler/"
remote artifact data = "image-install"
```

The unauthorized downstream patch that restricted the remote endpoint to
`ecu-v1` was removed from the SDK package and SWUpdate was rebuilt on
2026-09-26. The official handler now takes the endpoint name from the SWU
`images[].data` field. The product defconfig selects the same
`/run/mcu-update/` socket paths as the application.

## Not implemented

- Gateway restart recovery: the current anonymous image is discarded and the
  OTA executor is not restarted after power loss or process death.
- MCU power-return retry: the updater does not wait for a powered-off MCU and
  restart the complete image transfer when it returns.
An interruption before SWUpdate persists `ustate=1` does not resume the old
ZeroMQ transaction; HawkBit must deliver a complete new attempt. An interruption
after `ustate=1` is persistent is handled by the normal `-c 2` startup report.

## Validation (2026-09-28)

The official SWUpdate 2019.11 package was rebuilt with only the vendor
`0001` through `0007` patches. The resulting AArch64 binary contains only the
three `/run/mcu-update/` IPC paths. The official hidden `GUNZIP` option remains
at its upstream `default y`; current MCU artifacts do not use compression.

The production `mcu-updater` AArch64 target built successfully with no
`json-c` or SWUpdate IPC dependency, shell syntax checks passed, and all 13
host regression tests passed. Production HawkBit/MCU HIL acceptance for the
synchronous ACK/NACK and `-c 2` flow remains to be run on the device.
