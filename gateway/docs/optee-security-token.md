# OP-TEE SecurityAccess token

The device P-256 private key remains in the T527 keybox. `mcu-updater` opens
the dedicated TA through `libteec`; there is no signer daemon, Unix socket, or
REE IPC protocol.

```text
MCU SecurityAccess seed (4 bytes)
        -> mcu-updater
        -> TEEC_InvokeCommand(ISSUE_TOKEN)
        -> OP-TEE TA builds fixed claims and COSE_Sign1
        -> complete token returned to mcu-updater
        -> MCU verifies token and seed binding
```

The TA contract is `ta/include/ta_api.h`:

| Command | Input | Output |
| --- | --- | --- |
| `TOKEN_TA_CMD_GET_PUBLIC_KEY` | none | provisioned 64-byte `x || y` |
| `TOKEN_TA_CMD_ISSUE_TOKEN` | exactly one non-zero 4-byte SecurityAccess seed | complete protocol-bound COSE_Sign1 token |

The TA does not expose an arbitrary digest-signing command. It constructs the
fixed seed and freshness claims, hashes the COSE Sig_structure, and signs it
inside secure world. The REE cannot use this key to sign another message
format.

`src/security/security_token_tee.c` verifies the TA public key against the
compiled trust anchor when `mcu-updater` starts. Startup fails on mismatch.

The TA UUID remains `724b12aa-6e74-4779-bf3a-1580a076fed3`; install the built
`.ta` under `/lib/optee_armtz/` and run `tee-supplicant.service` before
`mcu-updater.service`.
