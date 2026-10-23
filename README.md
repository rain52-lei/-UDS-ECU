# S32K144 ThermoGuard ECU

基于 NXP S32K144 的车载热管理与诊断 ECU 学习项目。工程使用 S32 Design Studio、Processor Expert 和 S32 SDK，采用裸机非阻塞任务结构。

## 当前功能

- LPIT 1 ms 系统时基以及 10 ms / 100 ms 周期任务。
- 两路按键输入、消抖与启停请求。
- ADC0 通道 2 温度采样、滤波和传感器有效性判断。
- 传感器故障、过温锁存与 ECU 状态机。
- FTM0 通道 0 PWM 冷却输出以及 PI 控制。
- LPUART0 115200 bit/s 中断接收、环形缓冲区和文本命令解析。
- Classic CAN 500 kbit/s、8 字节载荷和内部回环测试。
- CAN 状态帧周期发送以及控制帧连续接收框架。

## CAN 报文

| 标准 ID | 方向 | 用途 |
|---|---|---|
| `0x123` | 内部回环 | CAN 控制器自检 |
| `0x200` | ECU 发送 | 温度、目标值、PWM、状态与故障标志 |
| `0x201` | ECU 接收 | 控制命令接收框架 |

`0x200` 当前采用 8 字节大端字段布局：

| 字节 | 内容 |
|---|---|
| 0–1 | 过滤温度，单位 0.1 °C，有符号 16 位 |
| 2–3 | 目标温度，单位 0.1 °C，有符号 16 位 |
| 4–5 | 冷却 PWM，0–1000 ‰ |
| 6 | ECU 状态 |
| 7 | 传感器有效、传感器故障、过温锁存、使能请求标志 |

## 工程结构

- `src/`、`include/`：应用、控制、通信和硬件抽象代码。
- `Sources/main.c`：初始化顺序和主循环。
- `Generated_Code/`：Processor Expert 生成的外设配置。
- `Project_Settings/`：启动代码和链接脚本。
- `SDK/`：工程使用的 S32 SDK 驱动源文件。
- `ProcessorExpert.pe`：Processor Expert 配置。

## 开发环境

- MCU：NXP S32K144（100 引脚）
- IDE：S32 Design Studio for ARM
- 编译器：GNU Arm Embedded GCC 6.3
- SDK：S32 SDK for S32K1xx RTM 3.0.0

在 S32 Design Studio 中使用 **File → Import → Existing Projects into Workspace** 导入工程，然后选择 `Debug_FLASH` 配置构建。

## 当前验证状态

- 工程已通过严格语法检查和 `Debug_FLASH` 完整编译链接。
- CAN0 内部回环已验证通过。
- USB-CAN 物理总线、`0x201` 外部接收和连续重新挂接仍待实测。
- ISO-TP 与 UDS 服务尚未实现，后续会继续加入。

仓库不包含编译产物、历史备份、Processor Expert 本机缓存以及带本机绝对路径的调试启动配置。
