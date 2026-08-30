# OTA 校验职责边界

CAN 应用不承担 MCUboot 镜像安全验证职责。

## CAN 应用负责

- 下载会话和阶段顺序；
- 目标槽位、地址、长度和活动槽位保护；
- `TransferData` 的块序号、接收长度、Flash 写入和读回错误；
- `RequestTransferExit` 后确认数据已完整写入；
- 保存可恢复下载 checkpoint；启动自检成功后由确认路径写入 `image_ok`。

## MCUboot 负责

- 镜像头和镜像布局最终检查；
- SHA-256 完整性校验；
- TLV、KeyHash 和 ECDSA 签名验证；
- 版本选择和安全计数器防回滚；
- 复位后的槽位选择、启动和回滚。

发布工具负责把 MCUboot magic 写入交付镜像。CAN 应用不写入 magic，也不验证
待启动镜像的 trailer，只在当前镜像通过启动自检后写入 `image_ok`。

`0x37` 成功只表示完整的 pending 镜像已写入目标槽。UDS 侧的正响应不表示镜像
已经完成安全验真；只有复位后由 `E:\Simple_ST\Boot` 的 MCUboot 验证通过，镜像才允许执行。
