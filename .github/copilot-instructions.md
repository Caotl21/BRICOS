# Project Guidelines

## Code Style
- Follow existing C style in the file you edit; keep naming, prefixes, and header usage consistent.

## Architecture
- This firmware uses a vertical stack: BSP -> Driver -> Task, with System as a horizontal support layer. See [BRICOS系统架构设计.md](BRICOS系统架构设计.md) for details.
- Keep dependency direction: Task may call Driver/BSP; Driver may call BSP; BSP must not call Driver/Task. Use callbacks/registration for upward notifications.
- Task-to-task communication should go through the System data pool or OSAL queues/semaphores, not direct function calls.

## Build and Test
- Build is done in Keil MDK-ARM. Open [Project/project.uvprojx](Project/project.uvprojx) and build the target (STM32F407ZGTx).
- Build artifacts are in [Project/Objects](Project/Objects). No CLI build scripts are provided.

## Conventions
- Use critical sections around data pool pull/push to preserve snapshot consistency. See [BRICOS系统架构设计.md](BRICOS系统架构设计.md).
- ISR code must not block or call APIs that can sleep.
- Monitoring/safety logic lives in Task_Monitor; it should change system modes but not drive PWM directly. See [监控与安全防线设计.md](监控与安全防线设计.md).
- Serial protocol details and CmdId definitions are in [上位机协议解析文档.md](上位机协议解析文档.md).
