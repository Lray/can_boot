# Gateway UDS client

The Gateway is the central controller for the CANopen nodes. It discovers and
addresses an MCU node through LSS, then acts as that node's UDS client over the
Node-ID-derived ISO-TP channel.

The UDS protocol engine is the unmodified driftregion/iso14229 release 0.11.0,
commit 8a7eb23d6ab9d52f66ce36b07ba456613709c78e. Source provenance is
recorded in [third_party/iso14229/UPSTREAM.md](../../third_party/iso14229/UPSTREAM.md).

src/uds/uds_client.c is the platform and product adapter. It connects the
upstream UDSClient_t state machine to the existing TransportOps boundary and
exposes the services used by the node-update flow:

- 0x10 DiagnosticSessionControl
- 0x11 ECUReset
- 0x22 ReadDataByIdentifier
- 0x27 SecurityAccess
- 0x31 RoutineControl
- 0x34/0x36/0x37 download transfer
- 0x3E TesterPresent

P2/P2* negotiation, response validation, standard NRC values and
0x78 ResponsePending are handled by upstream. The adapter adds the
operation-wide updater deadline, validates project DID/routine payloads and
keeps the update profile outside the vendored files.

Host validation is in tests/test_uds_client.c. It verifies negotiated timing,
exact NRC propagation, multiple 0x78 responses, DID validation,
SecurityAccess, download/transfer/exit and transport failure handling.

Board validation remains:

    cd /opt/can-ota-gateway
    ./scripts/verify_uds.sh awlink0 .

The test must address the selected MCU node, pass the supported DID reads and
leave the CAN link ERROR-ACTIVE with stable error counters.
