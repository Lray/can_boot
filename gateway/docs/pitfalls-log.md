# 板端 OTA 全链路踩坑记录

本文件记录 2026-08-14 ~ 08-15 板端 OTA 全链路调试中遇到的所有坑
(代码、编译、网络、WSL、脚本、部署),每条含:现象 → 根因 → 解决 → 预防。

## 目录

1. [编译与代码坑](#1-编译与代码坑)
2. [PowerShell/WSL 调用坑](#2-powershellwsl-调用坑)
3. [板端 WiFi 坑](#3-板端-wifi-坑)
4. [网络与防火墙坑](#4-网络与防火墙坑)
5. [hawkBit 容器坑](#5-hawkbit-容器坑)
6. [SWUpdate 传输坑](#6-swupdate-传输坑)
7. [脚本与部署坑](#7-脚本与部署坑)
8. [WSL 环境坑](#8-wsl-环境坑)
9. [调试方法论](#9-调试方法论)

---

## 1. 编译与代码坑

### 坑 1.1 `_GNU_SOURCE` 缺失导致 O_NOFOLLOW/O_CLOEXEC 未定义

**现象**:交叉编译报 `'O_NOFOLLOW' undeclared`、`'O_CLOEXEC' undeclared`、
`implicit declaration of function 'lstat'`、`'S_ISSOCK'` 未定义。

**根因**:`O_NOFOLLOW`/`O_CLOEXEC`/`S_ISSOCK` 等由 `<fcntl.h>`/`<sys/stat.h>`
在 `_GNU_SOURCE` 下才暴露;目标 CMake target 未定义该宏。

**解决**:`target_compile_definitions(<lib> PRIVATE _GNU_SOURCE)`(旧 bridge 的
`gateway-bridge-framing` 有,新库 `gateway-remote-handler` 漏了)。

**预防**:新加 `.c` 用到 Linux 扩展(openat/fexecve/renameat2/usleep/lstat 等),
必须在 CMake 里配 `_GNU_SOURCE`。

### 坑 1.2 `usleep` 隐式声明

**现象**:`implicit declaration of function 'usleep'` 警告(仅警告,链接可能失败)。

**根因**:`usleep` 在 `_GNU_SOURCE` 或 `_DEFAULT_SOURCE` 下才声明。

**解决**:`gateway-wifi-ctrl` 加 `target_compile_definitions(... PRIVATE _GNU_SOURCE)`。

### 坑 1.3 缺少头文件 include

**现象**:`orchestrator.c` 报 `implicit declaration of function 'fs_rename_noreplace'`。

**根因**:用了 `fs_util.h` 的函数但没 include。

**解决**:`#include "fs_util.h"`。

### 坑 1.4 git 冲突与 autostash 残留

**现象**:`git pull --rebase --autostash` 后工作区出现 `Unmerged paths`
(`both modified`、`deleted by them`),`git stash pop` 报 "needs merge"。

**根因**:autostash 与远端新提交修改同一批文件(脚本/配置),pop 时冲突。

**解决**:
1. 手动编辑冲突文件,删 `<<<<<<< / ======= / >>>>>>>` 标记;
2. `git add` 冲突文件;
3. `git stash drop`(内容已被重构覆盖时)。

**预防**:pull 前先 commit 或确保 stash 内容与远端不重叠。

### 坑 1.5 代码里重复 snprintf 逻辑错误

**现象**:重构 `launch_swupdate` 时写出的代码对同一 buffer 调用两次 snprintf,
且引用了结构中不存在的字段,编译不过。

**根因**:一次性重写函数时未对照 `OrchestratorOptions_t` 实际字段。

**解决**:先读头文件确认字段,再写实现;写完立即 `cmake --build` 验证。

**预防**:新增参数 → 先改 `.h` → 再改 `.c` → 立即编译。

---

## 2. PowerShell/WSL 调用坑

### 坑 2.1 adb shell 命令被 PowerShell 拆解

**现象**:`adb shell "cmd | grep xxx"` 报 `'grep' 不是内部或外部命令`、
`文件或目录语法不正确`、`系统找不到指定的路径`。

**根因**:PowerShell 把 `"..."` 内的管道/引号交给本地解析,adbc 收到的是拆散的参数;
`2>/dev/null` 被 PowerShell 当作重定向。

**解决**:
- 复杂 shell 命令写成 `.ps1` 文件,用
  `powershell -ExecutionPolicy Bypass -File xxx.ps1` 执行;
- 或 WSL 内写 `.sh` 文件,`bash /mnt/c/.../xxx.sh` 执行;
- 避免在 PowerShell 直接内联含 `|`、`2>&1`、`$()` 的 adb/wsl 命令。

**预防**:约定——**所有涉及 adb shell/wsl 的多段命令一律走脚本文件**。

### 坑 2.2 wsl.exe 输出缓冲/无输出

**现象**:`wsl.exe -d Ubuntu-24.04 -- bash -lc "..."` 长时间运行返回空(no output),
脚本实际在跑或已失败。

**根因**:PowerShell 对 wsl 子进程的输出捕获 + 引号转义不可靠。

**解决**:WSL 内用 `nohup ... > log 2>&1 &`,再分次 `cat log` 查看。

**预防**:长时间命令一律后台 + 日志文件。

### 坑 2.3 WSL 内调用 Windows 的 adb.exe,路径视角不同

**现象**:WSL 里 `adb push /mnt/c/...` 报 `cannot stat`;或 push 时用 `/tmp/x` 找不到。

**根因**:adb.exe 是 Windows 程序,只能读 Windows 路径(`C:\...`),
不能读 WSL 的 `/mnt/c/...` 或 `/tmp/...`。

**解决**:
- WSL 写文件到 `/mnt/c/Users/86151/AppData/Local/Temp/opencode/xxx`(WSL 可见);
- 传给 adb 时用 Windows 路径 `C:\\Users\\86151\\AppData\\Local\\Temp\\opencode\\xxx`。

**预防**:adb push 的目标源路径统一走 `C:\` 形式。

### 坑 2.4 adb 后台进程被会话回收

**现象**:`adb shell "orchestrator ... &"` 启动后进程消失,日志为空。

**根因**:adb shell 会话结束会清理后台子进程。

**解决**:用 `setsid sh -c '... > log 2>&1 < /dev/null &'` 脱离会话。

---

## 3. 板端 WiFi 坑

### 坑 3.1 板上 wpa_cli 是老版本,不支持 `-C`

**现象**:`wpa_cli -i wlan0 -C /run/wpa_supplicant status` 打印 usage 报错
(`invalid option -- 'C'`)。

**根因**:板上 wpa_supplicant/wpa_cli 是老版(2019 前后),老版用
`-p <dir>` 指定 ctrl 目录,`-C` 是 2022+ 新语法。

**解决**:`wifi_ctrl.c` 全部改用 `wpa_cli -i <if> -p <dir>`(新旧兼容)。

**预防**:板上工具先 `wpa_cli -h` 确认参数,再写代码。

### 坑 3.2 wpa_supplicant 已运行导致 orchestrator 启动失败

**现象**:orchestrator 日志:`ctrl_iface exists and seems to be in use` →
`wifi connection failed`。

**根因**:板重启后手动/init 已拉起 wpa_supplicant,orchestrator 又 exec 一个,
ctrl interface 冲突。

**解决**:`wifi_ctrl_start_supplicant` 先 `wpa_cli status` 探测,成功则跳过启动。

**预防**:编排进程对"依赖服务已在运行"必须幂等。

### 坑 3.3 WiFi 已 COMPLETED 但无 IP

**现象**:`wpa_state=COMPLETED` 但没有 `ip_address`。

**根因**:wpa_supplicant 只管 802.11 关联,DHCP 由 udhcpc 负责,需手动起。

**解决**:`udhcpc -i wlan0 -n -q` 获取 IP;`wifi_ctrl_connect_saved` 轮询
`wpa_state=COMPLETED` 且 `ip_address` 非空。

---

## 4. 网络与防火墙坑

### 坑 4.1 板访问宿主 IP 不通(No route / timeout / reset)

**现象**:
```text
wget: can't connect to remote host (192.168.10.101): No route to host
wget: download timed out
wget: error getting response: Connection reset by peer
```

**根因(按序排查)**:
1. WSL 网络模式(mirrored 时 WSL 与 Windows 共享 IP,必须访问共享 IP,
   不是 WSL 的 NAT 地址 172.x/10.x);
2. Windows 防火墙拦截入站 18080;
3. `.wslconfig` 的 `firewall=true` 额外拦截;
4. 旧的 `netsh portproxy` 占用 18080 导致 docker 端口绑定失败。

**解决**:
1. `.wslconfig` 保持最小:`[wsl2]\nvmIdleTimeout=-1`;
2. 防火墙放行:
   `netsh advfirewall firewall add rule name=hawkbit-18080 dir=in action=allow protocol=TCP localport=18080`;
3. 删除残留 portproxy:
   `netsh interface portproxy delete v4tov4 listenport=18080 listenaddress=0.0.0.0`;
4. 验证三板斧(见 debug.md 问题 1)。

**预防**:网络问题先按"WSL 模式 → 防火墙 → portproxy → 容器端口"顺序排查,
不要先改代码。

### 坑 4.2 WSL NAT 地址漂移

**现象**:portproxy 指向的 `10.66.66.2` 在 WSL 重启后变成 `172.19.249.233`,
转发失效。

**根因**:WSL NAT 模式下 eth0 地址每次启动可能变化。

**解决**:避免依赖固定 NAT 地址;用 mirrored 模式共享 Windows IP,或每次重新查询。

---

## 5. hawkBit 容器坑

### 坑 5.1 H2 内存库重启丢数据(target/action 全丢)

**现象**:容器一重启,`/rest/v1/targets` 变空,板上 swupdate 报 401。

**根因**:官方镜像默认 `jdbc:h2:mem:hawkbit` 内存库(官方标注仅 Test/Dev)。

**解决**:文件型 H2 + 卷:
```bash
-e SPRING_DATASOURCE_URL='jdbc:h2:file:/app/artifactrepo/hawkbit;MODE=LEGACY;AUTO_SERVER=TRUE'
-e SPRING_DATASOURCE_DRIVER_CLASS_NAME=org.h2.Driver
```
已验证容器重启后 target 保留。

### 坑 5.2 文件 H2 报 `Function "IDENTITY" not found`(HTTP 500)

**现象**:配了 `jdbc:h2:file:...` 后 API 返回 500,
日志:`Function "IDENTITY" not found`(EclipseLink-4002)。

**根因**:hawkBit 1.1.0 用 EclipseLink,SQL 依赖 H2 1.x 的 `IDENTITY` 函数;
H2 2.x 移除。**必须 `MODE=LEGACY`**。

**解决**:URL 加 `;MODE=LEGACY;AUTO_SERVER=TRUE`。

**预防**:不要设 `SPRING_JPA_DATABASE_PLATFORM=org.hibernate...`(hawkBit 用 EclipseLink 非 Hibernate)。

### 坑 5.3 latest-mysql 镜像是 v0.6.1 + MariaDB 驱动

**现象**:`hawkbit-update-server:latest-mysql` 拉取到 v0.6.1(老);
配 `jdbc:mysql://` 报 `Driver org.mariadb.jdbc.Driver claims to not accept jdbcUrl`。

**根因**:该 tag 内置 **MariaDB Connector/J**,只认 `jdbc:mariadb://`。

**解决**:
```bash
-e JAVA_OPTS='-Dspring.profiles.active=mysql -Dspring.datasource.url=jdbc:mariadb://host:3306/hawkbit...'
```
或用 v1.1.0 + 文件 H2(MODE=LEGACY)更省事。

**预防**:用 docker 镜像前先 `docker run --rm --entrypoint sh <img> -c 'env'` 看驱动与 profile。

### 坑 5.4 容器周期性被优雅停止(Exit=0,SIGTERM)

**现象**:`docker inspect hawkbit` 显示 Exit=0、OOM=false、反复 Restart;
日志末尾全是 `ionShutdownHook ... Shutdown initiated`。

**根因**:**WSL 虚拟机被宿主机周期性要求关机**(见坑 8.1),
docker.service 被 systemd 停止,所有容器被 SIGTERM。
与 hawkBit 本身无关。

**解决**:数据持久化(坑 5.1)+ 板上 swupdate 自动重试;
根治需修宿主机 WSL 服务。

---

## 6. SWUpdate 传输坑

### 坑 6.1 `-f cfg + -u ""` 导致 CLOSE_WAIT 卡死(只下载 sw-description/sig)

**现象**(最重要的坑):
- swupdate 日志只有:
  `Received 219B/363B of ...`(sw-description)+ `Received 847B/850B`(签名),
  之后**永远没有 cpio**,反复轮询;
- `netstat` 显示 swupdate 与 hawkbit 的 TCP 处于 `CLOSE_WAIT`;
- orchestrator 收不到 INIT,直到 `receive timeout` 退出;
- job 目录始终为空。

**根因**:orchestrator 启动 swupdate 用
`/sbin/swupdate -f swupdate.cfg -u ''`。swupdate 2019.11 在这种组合下
连接管理异常:下载完 deploymentBase JSON 后连接挂起(CLOSE_WAIT),
不继续下载主 artifact。

**解决**:改用 `-u` 直接传 suricatta 参数(官方示例方式):
```bash
/sbin/swupdate -L -l 3 -k <key> \
  -u "-t DEFAULT -u 192.168.10.101:18080 -i <id> -k <token> -p 5 -r 5 -w 2"
```
orchestrator 新增 `--hawkbit-url/--hawkbit-id/--hawkbit-token/--hawkbit-tenant/
--hawkbit-polldelay/--hawkbit-retries/--hawkbit-retrywait`,删除 `--swupdate-cfg`。

**验证**:修复后日志出现完整下载进度
`Received 134144B of 134144B` → `Installation in progress` →
`orchestrator: bundle accepted bytes=132608`。

**预防**:swupdate 启动参数遵循官方 `-u` 传参;不要依赖 cfg 的 suricatta 段(2019.11 有坑)。

### 坑 6.2 swupdate 下载的是整个 .swu,不是逐个 artifact

**现象**:以为会下载 3 个 artifact(description/sig/cpio),
实际日志只有两个 Received 事件,困惑于 cpio 去哪了。

**根因**:DDI deploymentBase 里是一个 artifact `gateway-hawkbit-e2e-v101.swu`
(134144B,即整个 SWU 文件)。swupdate 先下载 description+sig 用于校验,
再下载**整个 .swu**,解包后把 inner bundle 推给 remote handler。

**预防**:看 DDI 的 deploymentBase JSON 确认 artifact 结构,再判断进度。

### 坑 6.3 错误码对照

| 错误 | 含义 | 处理 |
|---|---|---|
| 7 | 连接被拒 | 容器未起/重启窗口;等就绪 |
| 28/56 | 连接中断 | 网络抖动/容器重启 |
| 401 | target 失效 | hawkBit 数据丢失,重新注册 |
| CLOSE_WAIT | 连接挂起不关闭 | 参数问题(坑 6.1),换 `-u` 传参 |

---

## 7. 脚本与部署坑

### 坑 7.1 hawkbit_action_wsl.sh readonly 变量冲突

**现象**:
```text
xxx.env: line 1: MANAGEMENT_URL: readonly variable
xxx.env: line 4: RELEASE_ID: readonly variable
```

**根因**:脚本顶部 `readonly MANAGEMENT_URL=...`、`readonly RELEASE_ID=...`,
而 `load_state` 里 `source` state 文件重新赋值,readonly 冲突。

**解决**:state 文件不再写这些常量行(它们本来就是脚本常量),
`load_state` 也不校验它们。

### 坑 7.2 e2e 脚本在 /mnt 下执行导致 WSL 9P 崩溃加剧

**现象**:脚本反复无输出/退出;WSL 日志 `p9io.cpp Operation canceled` 每 70 秒一次。

**根因**:`/mnt/c`(9P 文件共享)高频读写触发 WSL 9P 层故障,
进而 systemd power off 整个 VM。

**解决**:**把脚本与 SWU 复制到 WSL 内部文件系统**(`~/e2e/`)再执行,
彻底绕开 /mnt。

**预防**:涉及 WSL 的自动化一律放 `~/` 下,不要依赖 /mnt 路径。

### 坑 7.3 e2e 脚本凭据未加载就 wait_api

**现象**:脚本停在 "wait for hawkBit API" 后退出。

**根因**:`curl_api` 需要 `HAWKBIT_ADMIN_USER/PASSWORD`,但脚本未
`source` 凭据文件(hawkbit_action_wsl.sh 内部才加载)。

**解决**:脚本开头 `load_credentials()`(校验属主/权限后 source)。

### 坑 7.4 job-id 必须是 UUID v4

**现象**:`orchestrator: invalid job options code=-2`。

**根因**:`--job-id` 校验 `is_uuid_v4`,测试用的 `e2e-onboard-0001-...` 不合法。

**解决**:用 `cat /proc/sys/kernel/random/uuid` 生成。

---

## 8. WSL 环境坑

### 坑 8.1 WSL VM 周期性 power off(p9io Operation canceled)

**现象**(环境级,最难缠):
- `journalctl`:`Operation canceled @p9io.cpp:258 (AcceptAsync)` →
  `systemd-logind: The system will power off now!` → `Stopping docker.service`;
- 10 分钟内 616 次;容器每 ~70 秒全部重启;
- WSL `uptime` 有时不变(只停 docker)有时整机重启。

**根因**:WSL 的 9P 文件服务层异常,触发 VM 关机流程;
docker 容器随之被优雅停止。**与项目代码无关,属宿主机 WSL 服务故障**。

**应对**:
1. `.wslconfig` 保持最小(仅 `vmIdleTimeout=-1`);
2. 容器 `--restart unless-stopped` 自启;
3. hawkBit 数据持久化(坑 5.1),崩溃后无需重注册;
4. 板上 orchestrator/swupdate 自动重试;
5. 避免 /mnt 高频访问(坑 7.2);
6. 根治需更新 WSL 版本或修复宿主服务。

### 坑 8.2 .wslconfig 改动需 `wsl --shutdown` 才生效

**现象**:改 `vmIdleTimeout=-1` 后无效果。

**根因**:配置只在 VM 完全重启后生效。

**解决**:`wsl --shutdown` → 重新 `wsl.exe`。

**预防**:改 .wslconfig 后主动重启一次,再观察稳定性,不要反复叠加选项。

---

## 9. 调试方法论

1. **现象分层**:先分"宿主机/WSL/容器/板上代码"四层,用最小命令逐层验证
   (wget 302 → docker ps → swupdate 日志 → orchestrator 日志)。
2. **日志优先**:每个环节保留独立日志文件(容器 logs、swupdate log、
   orchestrator log、脚本 run.log),不要只看终端。
3. **幂等验证**:网络问题先"三板斧"(WSL 自测 127.0.0.1 → 192.168.10.101 →
   板上 wget),全过再动代码。
4. **参数最小化**:怀疑 swupdate 参数时,用 `-l 5`(TRACE)单独手动跑
   swupdate,看完整 channel 日志,比改 orchestrator 快得多。
5. **脚本文件化**:所有 adb/wsl 多段命令写成 .sh/.ps1 文件执行,
   避免 PowerShell 转义二次坑。
6. **数据持久化先行**:任何"容器重启丢状态"的问题,先解决持久化,
   再谈重试与自动化。
7. **成功标准硬证据**:传输完成的唯一判据是
   `orchestrator: bundle accepted bytes=<size>` + job 目录文件 + SHA256 匹配。
