# hawkBit Gateway MCU HIL Delivery

This procedure verifies the complete Gateway-to-MCU OTA chain on the board. It
uses the SDK's SWUpdate 2019.11 Suricatta, hawkBit DDI, and Remote Handler
implementation. It does not use a second DDI, HTTP, download, or Remote
Handler client.

The automated path is:

```text
hawkBit Management API -> hawkBit DDI -> Suricatta -> signed SWU
-> Remote Handler -> ecu-ota-orchestrator -> package-input-v1 publication
-> gateway-ota-worker-v1 -> UDS download -> reset -> MCU confirm or rollback
-> HawkBit finished terminal state
```

`gateway-ota-worker-v1` is the real worker. The deployed runtime includes the
root-owned `token-signer-daemon`, the `ecu-token-signer` account, and the
`ecu-token-client` socket group. The HIL runner uses `awlink0` and accepts
success only after the real worker has returned zero.

## Preconditions

1. Build the target artifacts:

   ```sh
   cd /mnt/e/T527/can_boot/gateway
   ./scripts/build_target_wsl.sh build-target-hawkbit-e2e
   ```

   Build and retain the exact raw `image.bin` that is the SWU's only remote
   artifact. The SWU must be rebuilt and signed around that image. The checked-in
   historical `v101` artifact contains the removed inner package format and is
   not a valid input for this direct-image flow.

2. Start hawkBit on host port `18080`. The current container uses H2 in-memory
   storage. Keep WSL alive for the whole run; a hawkBit restart loses targets,
   artifacts, distribution sets, and actions.

3. On WSL, create the owner-only Management API credential file:

   ```sh
   install -d -m 700 ~/.config/ecu-ota
   umask 077
   cat > ~/.config/ecu-ota/hawkbit-admin.env
   HAWKBIT_ADMIN_USER=admin
   HAWKBIT_ADMIN_PASSWORD=replace-with-the-local-admin-password
   ```

   Set its mode to `0600`. The file is outside the repository. Do not put the
   target token, Wi-Fi passphrase, or SWU private key in repository files.

4. In the PowerShell 7 session that runs the procedure, set the Wi-Fi
   passphrase only in process memory:

   ```powershell
   $env:ECU_OTA_WIFI_PASSWORD = '...'
   ```

## Run

```powershell
Set-Location E:\T527\can_boot\gateway
.\scripts\run_hawkbit_gateway_hil_adb.ps1 `
  -SwuPath E:\path\to\release.swu `
  -ImagePath E:\path\to\release\image.bin
```

The runner reconnects Wi-Fi when necessary, discovers a board-reachable host
address for hawkBit on port `18080`, creates one target/action pair, deploys
only the real AArch64 worker and bridge to `/data/local/tmp`, and collects
evidence on the board.

Specify `-HawkBitHost 192.168.10.x` only when automatic discovery cannot find
the host address. The host address must be the current Wi-Fi address, not a
previous address recorded in an old test.

`-SwuPath` and `-ImagePath` are required. The former is the signed direct-image
SWU; the latter supplies the size and SHA-256 that bind its remote stream before
the worker can start. Use the released raw image from the trusted build/signing
pipeline, not a file unpacked from an unverified SWU. The runner independently
checks the published board-side `image.bin` against this SHA-256 after terminal
success.

## Terminal Semantics

The Remote Handler protocol in SWUpdate 2019.11 accepts only `ACK` or
`ACK:<milliseconds>`. It has no NACK response. The bridge sends an ACK only
after a real worker returns zero, which requires MCU confirm or rollback
verification. The runner observes the resulting terminal `finished` state
through the HawkBit Management API. It does not implement or synthesize DDI
feedback.

The runner retains these evidence files:

```text
/data/local/tmp/ecu-ota-hawkbit-runtime/<job-id>/
/data/local/tmp/ecu-ota-hawkbit-runtime/<job-id>.orchestrator.log
```

After a successful bridge publication the job contains only the atomically
published, root-owned `package-input-v1` directory with the single `image.bin`
member. The bridge verifies the release-bound SHA-256 before publication. The
temporary `image.bin.partial` lives inside `package-input-v1.partial` and is
renamed away by the atomic publication, so no second persistent image copy can
outlive the job state transition. The runner removes only the stopped Remote
Handler socket and lock from `/run`.

## Recovery

The WSL action state file is created below
`/home/lirui/.local/state/ecu-ota-hawkbit/` with mode `0600`. It contains the
target token and must remain owner-only. Check its
public state without printing the token:

```sh
cd /mnt/e/T527/can_boot/gateway
./scripts/hawkbit_action_wsl.sh status --state /home/lirui/.local/state/ecu-ota-hawkbit/<state>.env
```

After the action is terminal and no longer needed, delete that state file. Do
not reuse it to create another action. A new run creates a new target and
distribution assignment, preventing ambiguity after a hawkBit H2 restart.

## Resume Evidence

This ECU profile accepts one full-slot image of at most 131072 B. A 256 MiB SWU
is not a valid worker input and must not be used to claim this end-to-end path.
Transport-resume experiments for larger artifacts belong to an independent
SWUpdate transport test, not the MCU OTA worker HIL.
