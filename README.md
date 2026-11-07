# ThermoGuard SDK · S32K144热管理与UDS诊断ECU

基于JS-S32K144-0103评估板、S32 Design Studio和S32 SDK的嵌入式学习项目，采用裸机协作式调度，实现温度采集、冷却控制、CAN通信、UDS诊断和故障保护。

**已实测通过外部CAN双向收发、VIN多帧、远程控制及通信超时保护；PWM波形、实体按键保护和硬件看门狗等仍待完整上板验收。** 温度输入由电位器模拟，PWM表示冷却请求，尚未连接真实温度传感器和风扇。

## 功能

| 模块 | 已实现内容 |
|---|---|
| 周期调度 | LPIT 1ms时基、10ms控制任务、100ms状态上报、跳过周期计数 |
| 温度输入 | ADC0通道2采样、整数温度换算、一阶低通滤波、有效范围判断 |
| 控制 | 定点PI、输出限幅、抗积分饱和、正常输出变化率限制、FTM0通道0 PWM |
| 状态与保护 | OFF/RUNNING/FAILSAFE/DEGRADED；传感器异常确认、过温锁存和条件恢复 |
| 控制权 | LOCAL/REMOTE模式、本地停机优先、远程停机锁定与重新启用 |
| UART | LPUART0 115200 8N1、接收队列、按行命令、回复、溢出重同步、硬件接收错误后重挂 |
| CAN | Classic CAN 500kbit/s、内部回环、周期状态帧、独立命令/诊断接收队列和诊断发送邮箱 |
| 命令监督 | CRC-8/SAE-J1850、8位滚动计数器、重复/倒退拒绝、缺帧计数、目标范围检查、500ms超时 |
| ISO-TP | 最多256字节、SF/FF/CF/FC、流控、序号回绕、忙时重试、WAIT上限、Overflow与超时 |
| UDS | 会话、保活、DID读取及条件写入、DTC查询/清除、冻结帧和维护例程 |
| 故障记录 | 6类RAM DTC、首次故障冻结帧、出现计数、最近24条故障变化事件 |
| 运行监督 | 调度/PWM错误保护、条件喂狗、可启用的硬件WDOG、复位原因读取 |
| 电脑工具 | Windows串口工作台、CSV导出、USB-CAN客户端、DBC、软件热模型与自动化测试 |

## 工程结构

```text
Sources/main.c       初始化与主循环
include/             公共接口和应用配置
src/                 控制、状态机、驱动封装和通信诊断实现
Generated_Code/      Processor Expert生成的外设配置
SDK/                 NXP S32 SDK驱动
Project_Settings/    启动代码与链接脚本
host/                串口工作台、CAN客户端和DBC
tests/               核心、应用集成、驱动适配层和工具测试
tools/               构建和验证脚本
```

源码入口：[main.c](Sources/main.c)、[应用调度](src/app.c)、[CAN策略](src/remote_control.c)、[ISO-TP](src/isotp.c)、[UDS](src/uds.c)、[故障记录](src/diag_store.c)、[调度监督](src/scheduler_guard.c)。

## 构建与验证

开发环境：S32K144、S32 Design Studio for ARM、S32 SDK for S32K1xx、GNU Arm Embedded GCC 6.3。电脑测试使用MinGW GCC及Python 3.9+；串口工作台使用Python自带Tkinter和Windows串口API。

在工程根目录打开PowerShell：

```powershell
# 核心、应用集成、串口适配层、热模型、CAN客户端与面板自检
.\tools\verify.ps1

# 只运行C电脑测试
.\tools\build.ps1 -HostOnly

# 内部回环调试固件，硬件看门狗关闭
.\tools\build.ps1

# 外部CAN调试固件，硬件看门狗关闭
.\tools\build.ps1 -ExternalCan

# 外部CAN固件，开启硬件看门狗
.\tools\build.ps1 -ExternalCan -Watchdog
```

脚本默认查找现有`G:\S32D`内的编译器；换环境时通过`HOST_GCC`和`S32K_GCC`指定编译器可执行文件，Python路径可用`verify.ps1 -Python <完整路径>`指定。脚本不会自动下载安装依赖。

输出统一位于工程`build/`目录：

| 输出 | 用途 |
|---|---|
| `loopback-debug/ThermoGuard_SDK.elf` | 内部回环教学调试 |
| `external-debug/ThermoGuard_SDK.elf` | 外部CAN调试 |
| `external-watchdog/ThermoGuard_SDK.elf` | 外部CAN与硬件WDOG验收 |
| `verification.txt` | 软件验证日志 |
| `thermal-model.csv` | 软件热模型记录 |
| `exports/` | 串口工作台导出的CSV |

也可在S32DS中导入现有工程，刷新后选择`Debug_FLASH`构建。默认模式位于[app_config.h](include/app_config.h)：外部CAN正常模式，硬件WDOG关闭。独立脚本显式指定回环或外部模式，不修改IDE默认配置。更换工作目录时检查S32DS工程的头文件搜索路径；`tests/`不要加入固件源目录。

## CAN协议

使用11位标准数据帧、Classic CAN、500kbit/s；CAN0引脚为PTE4 RX/PTE5 TX，ALT5。

| ID | 方向 | 用途 |
|---|---|---|
| `0x123` | ECU发送/内部回环 | 外部模式上电指纹帧；内部回环控制器自检 |
| `0x200` | ECU发送 | 100ms温度、目标、PWM、状态及故障标志 |
| `0x201` | ECU接收 | 启停和目标温度命令 |
| `0x700` | ECU接收 | ISO-TP诊断请求与流控 |
| `0x708` | ECU发送 | ISO-TP诊断响应与流控 |

`0x200`状态帧：温度2字节、目标2字节、PWM2字节、状态1字节、标志1字节；多字节字段大端。温度单位0.1℃，PWM单位‰。状态值0=OFF、1=RUNNING、2=FAILSAFE、3=DEGRADED。

`0x201`控制帧：

| 字节 | 内容 |
|---|---|
| 0 | 类型，固定`0x01` |
| 1 | 滚动计数器，0..255循环 |
| 2..3 | 大端有符号目标温度，合法范围200..800，即20.0..80.0℃ |
| 4 | bit0启用请求，其余位为0 |
| 5..6 | 保留为0 |
| 7 | 前7字节CRC-8/SAE-J1850 |

CRC：多项式`0x1D`、初值`0xFF`、结果异或`0xFF`、不反射。标准检查串`123456789`结果为`0x4B`。计数0、目标45.0℃、启用示例：`01 00 01 C2 01 00 00 34`。

本地模式下CAN命令只被记录；远程模式下通过校验的命令参与控制。重复、倒退、CRC错误和目标越界的帧不刷新超时。KEY2或串口OFF优先停机，远程恢复须先disable再enable。远程模式失去新鲜命令500ms后进入DEGRADED，冷却请求为1000‰。

DBC：[thermoguard.dbc](host/thermoguard.dbc)。CRC与计数器提供完整性及新鲜度监督，不提供身份认证。

## UDS服务

当前支持普通物理寻址和一个诊断会话，ISO-TP容量256字节；接收流控BS=8、STmin=5ms，传输超时1000ms，扩展会话S3超时5000ms。

| 服务 | 支持内容 |
|---|---|
| `10` | 默认会话01、扩展会话03 |
| `3E` | TesterPresent，支持抑制正响应 |
| `22` | 单DID读取 |
| `2E` | 扩展会话下按停机/健康条件写目标或模式 |
| `19 02` | 按状态掩码报告DTC |
| `19 04` | 指定DTC冻结帧 |
| `14 FF FF FF` | 停机且无活动故障时清除RAM故障记录 |
| `31 01 FF 00` | 满足恢复条件时解除过温锁存 |
| `31 01 FF 01` | 满足维护条件时复位PI积分 |

| DID | 内容 |
|---|---|
| `F190` | 演示VIN |
| `F180` | 固件版本，完整19字节`ThermoGuard-SDK-2.0`，UDS响应共22字节 |
| `F1A0` | 温度、目标、PWM、状态、标志及运行时间 |
| `F1A1` | DTC状态、计数及首次冻结帧详情 |
| `F1A2` | 最近24条故障变化事件 |
| `F1A3` | 目标温度；本地停机时允许条件写入 |
| `F1A4` | 本地/远程模式；停机时允许条件写入 |

6类DTC分别对应传感器、过温、远程命令超时、bus-off、运行监督和PWM错误，编号`C10001`至`C10006`。记录包含活动/已存储状态、出现计数和首次温度/PWM/时间快照。

## 串口与主机工具

串口参数115200、8N1。命令以LF或CRLF结束：

```text
OFF
LOCAL
SET 450
ON
OFF
```

`LOCAL`/`REMOTE`选择模式，`SET`单位0.1℃且要求本地停机。ON/OFF回复`ACK REQ=1/0`，模式和目标回复`OK`或`ERROR CMD`；实际状态以ECU周期遥测为准。

启动Windows串口工作台：

```powershell
python -B host\panel.py
```

USB-CAN客户端需要适配器驱动和[python-can依赖](host/requirements-can.txt)。以下为PCAN示例，按实际适配器修改interface/channel：

```powershell
# 读取VIN，包含ISO-TP多帧响应
python -B host\can_client.py --interface pcan --channel PCAN_USBBUS1 --uds "22 F1 90"

# ECU先停机，请求远程模式后发送10秒控制心跳，结束时发送停机帧
python -B host\can_client.py --interface pcan --channel PCAN_USBBUS1 --remote --heartbeat 10 --enable --target 450
```

物理通信前核对板卡版本、收发器外部供电、CAN0拨码、H/L、共地及两端终端匹配。内部回环不经过这些外部电路。

## 验证状态

软件验收已通过：

- C核心210159条断言，包含完整版本字符串回归和10万轮确定性随机协议输入。
- 真实`app.c`应用集成30项断言，硬件和时间接口由测试替换。
- 256字节ISO-TP双端传输、流控、序号回绕、忙时重试和异常处理。
- UART适配层队列溢出及错误后重新挂接测试。
- CAN客户端8项Python测试，串口面板解析与隐藏窗口自检。
- 180秒软件热模型：最终45.000℃、最高48.867℃，属于软件仿真结果。
- S32DS `Debug_FLASH`及独立回环/外部CAN看门狗固件构建；全源独立构建启用`-Wall -Wextra -Werror -pedantic`。

2026-10-27构建占用记录，单位字节；修复后的实际占用以本次构建输出为准：

| 构建 | text | data | bss |
|---|---:|---:|---:|
| S32DS Debug_FLASH | 38096 | 684 | 9920 |
| 内部回环调试版 | 38124 | 684 | 9920 |
| 外部CAN看门狗版 | 37856 | 684 | 9840 |

链接脚本预留4KB栈；size中的bss包含栈、堆等NOBITS预留。这些构建和电脑测试结果不能代替实际PWM波形及硬件看门狗复位验收。

2026-11-07外部总线验收：

- PCAN-USB、500kbit/s，10秒收到100帧`0x200`；TesterPresent及17字节演示VIN多帧读取可重复通过。
- 远程启停、坏CRC拒绝、重复计数器拒绝、目标越界拒绝、255到0回绕、通信超时及恢复、DTC和冻结帧等17个检查点通过。
- UDS记录读取、错误请求负响应、抑制正响应、5秒会话超时等15个检查点通过。
- 实测发现`F180`软件版本最后一位被截断；现按字符串实际长度生成22字节响应，并加入完整载荷、响应长度及边界回归断言。
- 本地串口OFF优先级未完成验收；不能将纯软件模拟或CAN返回的PWM请求当作实体按键、实际PWM波形和风扇动作已验证。

## 实现边界

这是电位器模拟输入的热管理与诊断原型。DTC、事件和目标参数保存在RAM中，复位或断电会丢失。尚未接入真实温度传感器、风扇、转速反馈或EEPROM/Flash持久化。

UDS和ISO-TP覆盖上表所列项目子集；不包含安全访问、程序刷写、Bootloader、功能寻址、CAN FD或完整标准合规认证。默认调试版关闭硬件WDOG，启用版的名义超时约1秒，实际复位时间仍需板上测量。

仓库排除学习笔记、讲解文档、历史备份、本机缓存及编译产物。
