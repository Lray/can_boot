# SWUpdate 2019.11 IPC headers

`include/network_ipc.h` and `include/swupdate_status.h` are verbatim API
headers from the official SWUpdate `2019.11` tag (commit
`5de3bc30a203ee218f9ebbe256b42e26cf06c74f`). Their upstream SHA-256 values
are `58010f4d4bb718c9e78f86e69359fb52080fbd9fbcc611e8c283b057383ab5eb`
and `de748b2577a7a9e2a49a71d7e3f03b8c93a7cdc176ff23d1514d9b49368e96ee`.
They are licensed LGPL-2.1-or-later by their upstream authors.

The matching SDK source archive SHA-256 is
`df5ab8abc6077fb3f42188ffef02c04001bf9bec1d1183621624c8f65ae41986`.

Source: <https://github.com/sbabic/swupdate/tree/2019.11/include>

They are retained only as provenance for the SDK API version. Gateway
applications do not compile or link a second hawkBit client. The board agent
executes the SDK-provided SWUpdate 2019.11 binary, whose Suricatta and curl
channel own DDI, authentication, download, retry/resume, and feedback.
