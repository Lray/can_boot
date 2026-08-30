# 板端 OTA 全链路 Debug 手册

本文档总结板端向 hawkBit 服务器发送请求(OTA 升级全链路)时遇到的所有问题,
每个问题给出**可复现步骤**、**根因**、**解决方案**与**验证方法**。

## 链路拓扑

```text
板端(192.168.10.104, WiFi)
  ├─ wpa_supplicant + udhcpc      (WiFi 连接)
  ├─ ecu-ota-orchestrator          (编排进程: wifi -> swupdate -> ZMQ -> worker)
  ├─ /sbin/swupdate (suricatta)    (DDI 轮询 + 下载 SWU + Remote Handler 推送)
  └─ ZMQ REP ipc:///run/ecu-ota/remote-handler/ecu-v1

Windows 主机 (192.168.10.101, WSL mirrored 网络)
  └─ WSL Ubuntu-24.04
       ├─ dockerd
       └─ hawkbit (0.0.0.0:18080) + hawkbit-ui (0.0.0.0:8088)
```

## 环境速查

| 项 | 值 |
|---|---|
| 板序列号 | `00675779d0c446e21d4` |
| 板 WiFi | ssid=`10086`, IP=`192.168.10.104`, 网关=`192.168.10.1` |
| hawkBit 主机 | `192.168.10.101:18080`(WSL mirrored,与 Windows 共享 IP) |
| WSL 发行版 | Ubuntu-24.04, sudo 密码 `041206` |
| adb | `E:\T527\.local-tools\platform-tools-20260730\platform-tools\adb.exe` |
| 板上配置 | `/run/media/mmcblk0p6/ecu-ota/{bin,config,trust,var/jobs}` |
| 板上二进制 | `ecu-ota-orchestrator`, `gateway-ota-worker-v1` |
| 一键脚本 | `gateway/scripts/hawkbit_e2e.sh`(WSL 内执行) |
| 认证 | hawkBit admin=`admin`/`admin`(`~/.config/ecu-ota/hawkbit-admin.env`) |

---

## 问题 1: 板端无法解析/连接 hawkBit 服务器(防火墙与网络)

### 现象

```text
板端: busybox wget -S http://192.168.10.101:18080/
wget: can't connect to remote host (192.168.10.101): No route to host
或
wget: download timed out
或
wget: error getting response: Connection reset by peer
```

### 可复现

1. 板 WiFi 已连(有 192.168.10.x IP)。
2. 板上执行 `wget -T 8 http://192.168.10.101:18080/`。
3. 若 Windows 防火墙拦截或网络模式不对,出现上述错误。

### 根因(按排查顺序)

| # | 原因 | 说明 |
|---|---|---|
| 1 | **WSL 网络模式** | `C:\Users\86151\.wslconfig` 中 `networkingMode=mirrored` 使 WSL 与 Windows 共享 IP。WSL 内 `ip addr` 显示的 `eth3/192.168.10.101` 是共享地址,**板必须访问该 IP,而不是 WSL 的 NAT 地址(172.x 或 10.x)**。 |
| 2 | **Windows 防火墙** | WSL mirrored 模式下,外部设备访问 Windows 共享 IP 的端口时,Windows 防火墙按入站规则处理。默认拦截 18080。 |
| 3 | **WSL firewall 设置** | `.wslconfig` 中 `firewall=true` 时,即使 Windows 放行,WSL 侧也可能拦;设 `firewall=false` 可绕过。 |
| 4 | **WSL 空闲重启** | 若 `vmIdleTimeout` 未设为 `-1`,WSL 会话闲置数分钟会被关闭,docker 容器随之停止,板端表现为连接拒绝。 |
| 5 | **netsh portproxy 残留** | 曾配置 `netsh interface portproxy` 占用 18080 转发到失效的 WSL 地址,会导致 docker 端口绑定失败(`address already in use`)。 |

### 解决

1. **`.wslconfig` 最终配置**(已生效,勿再改动):

   ```ini
   [wsl2]
   vmIdleTimeout=-1
   ```

   说明:保持默认 NAT 或 mirrored 均可,但**不要**再叠加 `firewall=true`/portproxy。
   本环境最终采用默认配置 + `vmIdleTimeout=-1`,WSL 稳定运行数小时。

2. **Windows 防火墙放行 18080**(管理员 PowerShell,若配置了 mirrored):

   ```powershell
   netsh advfirewall firewall add rule name=hawkbit-18080 dir=in action=allow protocol=TCP localport=18080
   ```

3. **删除残留 portproxy**(如果存在):

   ```powershell
   netsh interface portproxy delete v4tov4 listenport=18080 listenaddress=0.0.0.0
   ```

4. **验证连通性**(三板斧,按序执行):

   ```bash
   # WSL 内自测
   curl -fsS -o /dev/null -w '%{http_code}\n' http://127.0.0.1:18080/
   curl -fsS -o /dev/null -w '%{http_code}\n' http://192.168.10.101:18080/

   # 板端自测(必须拿到 HTTP 状态码,不是超时/拒绝)
   adb -s 00675779d0c446e21d4 shell "busybox wget -T 8 -q -S -O /dev/null http://192.168.10.101:18080/ 2>&1 | head -3"
   # 期望: HTTP/1.1 302  ->  Location: ...swagger-ui...  ->  HTTP/1.1 200
   ```

### 判定标准

板端 wget 返回 `HTTP/1.1 302/200` 即链路通。任何 `timed out` / `No route to host` /
`Connection reset` / `Connection refused` 都表示问题未解决。

---

## 问题 2: 板端 WiFi 连接失败/未连接

### 现象

```text
wpa_cli -i wlan0 status: Failed to connect to non-global ctrl_ifname: wlan0
wpa_state=SCANNING / DISCONNECTED
ip -4 addr show wlan0: (无 inet)
```

### 可复现

1. 板重启后 wpa_supplicant 未自动启动(本板 init 脚本未拉起它)。
2. 手动 `ip link set wlan0 up` + 启动 wpa_supplicant + udhcpc 才能联网。

### 根因

- 板 rootfs 的 `wpa_supplicant`/`wpa_cli` 是**老版本**:
  - **不支持 `-C <dir>` 选项**(新语法),只支持 `-p <dir>`。
  - `wpa_cli -i wlan0 -C /run/wpa_supplicant` 会打印 usage 报错。
- orchestrator 的 `wifi_ctrl` 需要**容忍已运行的 wpa_supplicant**(探测到 ctrl interface 存在则跳过启动),否则"ctrl_iface exists"报错导致 wifi 失败。
- 需要扫描+连接**已保存网络**(`list_networks` 找到 ssid 后 `select_network <id>`)。

### 解决

1. **手动连接模板**(adb shell,顺序执行):

   ```sh
   killall wpa_supplicant 2>/dev/null
   mkdir -p /run/wpa_supplicant; chmod 0750 /run/wpa_supplicant
   ip link set wlan0 up
   wpa_supplicant -B -D nl80211 -i wlan0 \
     -c /run/media/mmcblk0p6/ecu-ota/config/wpa_supplicant.conf -C /run/wpa_supplicant
   sleep 3
   wpa_cli -i wlan0 -p /run/wpa_supplicant scan; sleep 3
   wpa_cli -i wlan0 -p /run/wpa_supplicant list_networks   # 找到 ssid=10086 的 id
   wpa_cli -i wlan0 -p /run/wpa_supplicant select_network 0
   udhcpc -i wlan0 -n -q
   wpa_cli -i wlan0 -p /run/wpa_supplicant status   # 期望 wpa_state=COMPLETED + ip_address
   ```

2. **代码侧已修复**(`gateway/adapters/swupdate/wifi_ctrl.c`):
   - 使用 `-p`(兼容老版 wpa_cli)。
   - `wifi_ctrl_start_supplicant` 先探测 ctrl interface,已运行则跳过启动。
   - `wifi_ctrl_connect_saved` 内部执行 scan → list_networks → select_network → 轮询 COMPLETED+IP。

3. **验证**:

   ```sh
   wpa_cli -i wlan0 -p /run/wpa_supplicant status | grep -E 'wpa_state|ip_address'
   # 期望: wpa_state=COMPLETED  +  ip_address=192.168.10.104
   ```

---

## 问题 3: hawkBit 容器崩溃/反复重启

### 现象

```text
docker ps --filter name=hawkbit
hawkbit Up 3 seconds          # 反复重启
curl http://127.0.0.1:18080/rest/v1/targets  -> 000 / Connection reset
docker inspect hawkbit --format '{{.State.ExitCode}} {{.State.OOMKilled}}'
0 false                       # Exit=0 优雅关闭, 非 OOM
docker logs hawkbit 末尾:
[ionShutdownHook] ... Shutdown initiated...
systemd-logind: The system will power off now!
```

### 可复现

- 在 **WSL 会话被宿主机关闭/回收**时(闲置数分钟、`vmIdleTimeout` 未禁、或宿主电源策略),
  所有容器被 SIGTERM 优雅停止,hawkBit 的 **H2 内存数据库随之丢失**。
- 容器有 `--restart unless-stopped`,dockerd 恢复后会重启容器,但 target/action/artifact 数据全丢。

### 根因

1. **hawkBit 官方镜像默认 H2 内存库**(`jdbc:h2:mem:hawkbit`),容器重启即数据全丢。
2. **WSL 虚拟机生命周期不稳定**:`systemd-logind` 出现 `power off`,docker.service 被 systemd 停止,
   表现为"每 5-7 分钟崩一次"。根因在宿主机 WSL 服务/空闲回收策略,不在应用代码。
3. 曾错误叠加 `.wslconfig` 的 `firewall=false`/`vmIdleTimeout=-1` 与 netsh portproxy,
   反而加剧了重启频率(需要 WSL 完全重启配置才生效)。

### 解决

1. **`.wslconfig` 保持最小化**:

   ```ini
   [wsl2]
   vmIdleTimeout=-1
   ```

   修改后需 `wsl --shutdown` 完全重启 WSL 一次才生效。

2. **容器自启**(已配置,勿动):

   ```bash
   docker run -d --name hawkbit --restart unless-stopped -p 18080:8080 \
     -v hawkbit-artifactrepo:/app/artifactrepo \
     -e HAWKBIT_SERVER_DDI_SECURITY_AUTHENTICATION_TARGETTOKEN_ENABLED=true \
     hawkbit/hawkbit-update-server:1.1.0
   docker run -d --name hawkbit-ui --restart unless-stopped -p 8088:8088 \
     hawkbit/hawkbit-ui:1.1.0
   ```

3. **启动/检查脚本**(用户提供的标准操作):

   ```bash
   docker inspect hawkbit hawkbit-ui >/dev/null
   if [ "$(docker inspect -f '{{.State.Running}}' hawkbit)" != "true" ]; then docker start hawkbit; fi
   until curl -fsS --max-time 5 -o /dev/null http://127.0.0.1:18080/; do sleep 2; done
   if [ "$(docker inspect -f '{{.State.Running}}' hawkbit-ui)" != "true" ]; then docker start hawkbit-ui; fi
   until curl -fsS --max-time 5 -o /dev/null http://127.0.0.1:8088/; do sleep 2; done
   docker ps --filter name=hawkbit --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
   ```

4. **数据持久化(可选,根治崩溃丢数据)**:
   用文件型 H2 并挂到卷,使容器重启后 target/action 不丢:

   ```bash
   docker run -d --name hawkbit --restart unless-stopped -p 18080:8080 \
     -v hawkbit-artifactrepo:/app/artifactrepo \
     -e HAWKBIT_SERVER_DDI_SECURITY_AUTHENTICATION_TARGETTOKEN_ENABLED=true \
     -e SPRING_DATASOURCE_URL='jdbc:h2:file:/app/artifactrepo/hawkbit' \
     -e SPRING_DATASOURCE_DRIVER_CLASS_NAME=org.h2.Driver \
     hawkbit/hawkbit-update-server:1.1.0
   ```

   ⚠️ 注意:hawkBit 1.1.0 用 **EclipseLink** 不是 Hibernate,不要设置
   `SPRING_JPA_DATABASE_PLATFORM=org.hibernate.dialect.H2Dialect`(会导致
   `ClassNotFoundException: org.hibernate.dialect.H2Dialect`)。

5. **验证**:

   ```bash
   docker inspect hawkbit --format 'UpSince={{.State.StartedAt}}'
   # 两次执行间隔 2 分钟,时间戳不变 = 容器稳定
   curl -s -o /dev/null -w '%{http_code}\n' -u admin:admin http://127.0.0.1:18080/rest/v1/targets
   # 期望 200
   ```

---

## 问题 4: swupdate 轮询报错(Channel 7/28/56/401)

### 现象

```text
[ERROR] : SWUPDATE failed [0] ERROR : Channel get operation failed (7): 'Error'
[ERROR] : SWUPDATE failed [0] ERROR : Channel put operation failed (56): 'Error'
[ERROR] : SWUPDATE failed [0] ERROR : Channel operation returned HTTP error code 401.
```

### 根因对照表

| 错误 | 含义 | 处理 |
|---|---|---|
| `7` | 连接被拒(ECONNREFUSED) | hawkBit 容器崩溃/未启动/端口未绑定。等容器就绪或重启。 |
| `28`/`56` | 连接中断(超时/EOF) | 网络抖动或容器重启窗口;板↔主机链路需先过问题 1 验证。 |
| `401` | 目标 token 无效 | **hawkBit 重启后 H2 数据丢失**,板上 `swupdate.cfg` 里的 target 已不存在。**重新注册 target 并更新 cfg**。 |

### 解决

每次 hawkBit 重启后必须**重新走注册流程**(`scripts/hawkbit_e2e.sh` 自动完成):

```bash
cd /mnt/e/T527/can_boot/gateway
./scripts/hawkbit_e2e.sh /mnt/e/T527/can_boot/gateway/build-target-hawkbit-e2e/hawkbit-e2e/gateway-hawkbit-e2e-v101.swu
```

脚本自动:清 state → register → assign → 写板上 swupdate.cfg → 重启 orchestrator → 等待传输。

---

## 问题 5: bundle 传输中断(只收到 sw-description/sig,cpio 未传完)

### 现象

```text
[TRACE] : EVENT [0] : {"percent": 100, "msg":"Received 362B of 362B"}     # sw-description
[TRACE] : EVENT [0] : {"percent": 100, "msg":"Received 847B of 847B"}     # sw-description.sig
# 之后无任何输出, gateway-input-v1.cpio (132608B) 未传输
```

### 根因

- swupdate 先下载并校验 `sw-description` 与签名,然后才下载主 artifact
  (`gateway-input-v1.cpio`)。**若下载主 artifact 期间 hawkBit 容器崩溃/网络中断**,
  swupdate 静默等待或重试,日志停止增长。
- 在本次环境中,hawkBit 容器周期性崩溃(问题 3)是最常见诱因。

### 解决

1. 确保问题 1/2/3 全部通过(网络通 + WiFi 连 + hawkBit 稳定)。
2. 用 `scripts/hawkbit_e2e.sh` 重跑(重新注册,数据一致性)。
3. 观察日志出现 `orchestrator: bundle accepted bytes=132608` 即传输完成。

### 判定标准(全链路成功)

```text
orchestrator: READY job=... expected_bytes=132608
orchestrator: bundle accepted bytes=132608     <- SWU 完整收到
(随后 worker 启动;no-MCU 边界下预期 worker terminal failure code=20)
板上 job 目录:
/run/media/mmcblk0p6/ecu-ota/var/jobs/<job-id>/inner-bundle-v1.cpio   (132608 字节)
/run/media/mmcblk0p6/ecu-ota/var/jobs/<job-id>/gateway-input-v1/      (解包目录)
```

---

## 一键复现/验证流程(完整链路)

```bash
# 1. WSL 侧: 确保 hawkBit 就绪(问题 3 的检查脚本)
docker ps --filter name=hawkbit --format 'table {{.Names}}\t{{.Status}}'
curl -s -o /dev/null -w '%{http_code}\n' -u admin:admin http://127.0.0.1:18080/rest/v1/targets   # 200

# 2. 板侧: WiFi 连接(问题 2)
adb -s 00675779d0c446e21d4 shell "wpa_cli -i wlan0 -p /run/wpa_supplicant status | grep -E 'wpa_state|ip_address'"
#   期望 COMPLETED + 192.168.10.104

# 3. 板侧: 到 hawkBit 连通性(问题 1)
adb -s 00675779d0c446e21d4 shell "busybox wget -T 8 -q -S -O /dev/null http://192.168.10.101:18080/ 2>&1 | head -3"
#   期望 HTTP/1.1 302/200

# 4. 全链路(问题 4/5)
cd /mnt/e/T527/can_boot/gateway
./scripts/hawkbit_e2e.sh /mnt/e/T527/can_boot/gateway/build-target-hawkbit-e2e/hawkbit-e2e/gateway-hawkbit-e2e-v101.swu
#   期望: BUNDLE ACCEPTED / orchestrator: bundle accepted bytes=132608

# 5. 证据收集
adb -s 00675779d0c446e21d4 shell "ls -la /run/media/mmcblk0p6/ecu-ota/var/jobs/<job-id>/; sha256sum /run/media/mmcblk0p6/ecu-ota/var/jobs/<job-id>/inner-bundle-v1.cpio"
```

## 关键结论

1. 板端→服务器请求失败的根因排序:**WSL 网络/防火墙 > WiFi 未连 > hawkBit 崩溃 > 配置过期**。
2. 每次 hawkBit 重启后 **target/action 必丢**(H2 内存库),必须重新注册——这是"401"和
   "只收到描述文件"的根本原因。
3. `.wslconfig` 只需 `vmIdleTimeout=-1`,不要叠加其他选项,否则引入更难排查的副作用。
4. 板上 `wpa_cli` 是**老版本**,必须用 `-p` 参数(代码已适配)。
5. 全链路传输成功的唯一硬证据:`orchestrator: bundle accepted bytes=132608` + job 目录文件齐全。

---

## 权威调研: hawkBit 崩溃、SWUpdate 通用性与云厂商 OTA 方案

> 调研时间 2026-08-15,资料均来自官方文档/仓库。

### 1. hawkBit 的 H2 崩溃与数据库选择(官方答案)

Eclipse hawkBit 官方仓库 README(https://github.com/eclipse-hawkbit/hawkbit)的
SQL 数据库支持表明确:

| 数据库 | 官方状态 |
|---|---|
| H2 | **Test, Dev**(仅测试/开发) |
| MySQL/MariaDB | **Production grade**(生产级) |
| PostgreSQL | **Production grade**(生产级) |

官方原文:"We are providing a Spring Boot based reference Update Server **including
embedded H2 DB for test and evaluation purposes**"(H2 仅用于测试评估)。
**H2 内存库容器重启即丢数据是官方明示的已知行为**,不是 bug。

### 2. 本环境验证过的 hawkBit 持久化方案(解决崩溃丢数据)

文件型 H2 + 兼容模式(`MODE=LEGACY`),已验证容器重启后 target/action 保留:

```bash
docker run -d --name hawkbit --restart unless-stopped -p 18080:8080 \
  -v hawkbit-artifactrepo:/app/artifactrepo \
  -e HAWKBIT_SERVER_DDI_SECURITY_AUTHENTICATION_TARGETTOKEN_ENABLED=true \
  -e SPRING_DATASOURCE_URL='jdbc:h2:file:/app/artifactrepo/hawkbit;MODE=LEGACY;AUTO_SERVER=TRUE' \
  -e SPRING_DATASOURCE_DRIVER_CLASS_NAME=org.h2.Driver \
  hawkbit/hawkbit-update-server:1.1.0
```

⚠️ 关键:**必须加 `MODE=LEGACY`**。hawkBit 1.1.0 使用 EclipseLink,其 SQL 依赖 H2 1.x
的 `IDENTITY` 函数;H2 2.x 移除了该函数,不加 `MODE=LEGACY` 会报
`Function "IDENTITY" not found`(HTTP 500)。
⚠️ 不要设置 `SPRING_JPA_DATABASE_PLATFORM`(EclipseLink 非 Hibernate)。

生产环境建议:用官方 `latest-mysql` 镜像 + MySQL(注意其驱动是 MariaDB,
URL 必须用 `jdbc:mariadb://`),或 PostgreSQL。

### 3. 企业是否使用 SWUpdate + hawkBit(是,且有商业生态)

hawkBit 官方 README 列出的 **hawkBit 兼容商业产品**:
- **Bosch IoT Rollouts**(博世 SaaS,https://bosch-iot-suite.com/service/rollouts/)
- **Kynetics Update Factory**(https://www.kynetics.com/iot-platform-update-factory)

hawkBit 官方客户端列表(https://github.com/eclipse-hawkbit/hawkbit):
**SWUpdate(第一位)**、RAUC(rauc-hawkbit-updater)、Zephyr-RTOS、ChirpStack Gateway OS 等。

SWUpdate 官方(https://swupdate.org/features):**100% 开源、无企业版、无运行时许可**,
集成 Yocto/Buildroot/Debian,大量产品在用。**suricatta 是官方服务器对接层,
hawkBit 是默认后端**。

### 4. 阿里云等云厂商: 是否用自定义 SDK(是)

- **阿里云物联网平台 OTA**:走自研 MQTT Topic 协议 + 设备端 LinkSDK,
  **不是 hawkBit DDI API**(见阿里云帮助中心"物联网平台-设备OTA升级"文档)。
- **AWS IoT**:用 **IoT Jobs**(https://docs.aws.amazon.com/iot/latest/developerguide/iot-jobs.html),
  自研协议 + AWS SDK。
- 华为云 IoTDA、腾讯云 IoT 同理(各自 SDK + 私有协议)。

### 5. 链路通用性结论(swupdate → remote handler → ZMQ → ota_worker)

| 层 | 通用性 | 依据 |
|---|---|---|
| **SWUpdate 设备端** | ✅ 通用 | 官方 suricatta 文档(https://sbabic.github.io/swupdate/suricatta.html):支持 **hawkBit + 通用 HTTP server + wfx(Lua)+ Lua 自定义模块**;"modular design allows to add further backends"。接阿里云可写 Lua suricatta 模块或通用 HTTP 模式(服务器只需 302+Location)。 |
| **Remote handler → ZMQ → worker** | ✅ 通用 | SWUpdate 官方机制,与服务器协议无关。 |
| **hawkBit 服务器** | ⚠️ 可替换 | 云厂商平台不兼容 DDI API;但**换服务器不影响 worker/编排逻辑**,只需替换对接层。 |

**结论**:架构分层正确。`hawkBit` 是可替换的服务器组件;生产用云厂商平台时,
设备端 SWUpdate 不变(通用 HTTP/Lua 模式),服务器对接层替换即可。
