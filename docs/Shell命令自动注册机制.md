# Shell 命令自动注册机制

本文档说明当前固件中 Shell 命令从“手工注册”改为“导出段自动注册”的实现方式，方便后续扩展命令与协作开发。

## 1. 设计目标

- 命令实现与 `System/sys_shell_core.c` 解耦。
- 命令可分散在多个业务文件，不需要集中在一个 init 里手动 `Register`。
- 新增命令时只改业务文件，主流程和 `main` 保持整洁。

---

## 2. 总体架构

当前机制由三部分组成：

1. **命令导出宏**
- 文件：`System/sys_shell_export.h`
- 宏：`EXPORT_SHELL_CMD(...)`
- 作用：把 `shell_cmd_desc_t` 放入链接段 `ShellCmd$$Table`。

2. **链接脚本段排序**
- 文件：`System/scatter/app1.sct`、`System/scatter/app2.sct`
- 顺序：`ShellCmd$$Base -> ShellCmd$$Table -> ShellCmd$$Limit`
- 作用：保证命令表在 ROM 中连续布局，便于遍历扫描。

3. **Core 自动扫描注册**
- 文件：`System/sys_shell_core.c`
- 函数：`System_ShellCore_RegisterBuiltins()`
- 作用：启动时遍历 `Base` 和 `Limit` 之间的命令描述符，逐条 `System_ShellCore_Register`。

---

## 3. 关键文件职责

- `System/sys_shell_core.c`
  - 只负责解析、权限/模式校验、命令分发、输出封装。
  - 不再内置业务命令逻辑。

- `User/app_shell_cmds_core.c`
  - 放基础与模式类命令：`help/echo/sysmode/momode/fault`。

- `User/app_shell_cmds_query.c`
  - 放查询类命令：`euler/depthtemp/power/cabin/chip`。

- `Project/project.uvprojx`
  - 必须把 `app_shell_cmds_core.c`、`app_shell_cmds_query.c` 加入工程编译。

---

## 4. 数据流（从上电到命令可用）

1. 编译阶段：各业务文件里的 `EXPORT_SHELL_CMD` 产生命令描述符常量。  
2. 链接阶段：scatter 把 `ShellCmd$$Base/Table/Limit` 按顺序放进 `ER_IROM1`。  
3. 初始化阶段：`System_ShellCore_Init()` 调用 `System_ShellCore_RegisterBuiltins()`。  
4. 扫描阶段：Core 从 `Base+1` 遍历到 `Limit`，自动注册所有命令。  
5. 运行阶段：收到 shell 输入后按命令名查表并执行 handler。

---

## 5. 新增命令标准步骤

以新增 `foo` 命令为例：

1. 在合适业务文件中实现 handler：

```c
static shell_ret_t prv_cmd_foo(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    System_ShellCore_Printf(ctx, "foo ok");
    return SHELL_RET_OK;
}
```

2. 在文件末尾导出：

```c
EXPORT_SHELL_CMD("foo", "foo request", prv_cmd_foo, SHELL_PERM_READONLY, SHELL_MODE_ANY);
```

3. 若是新 `.c` 文件，加入 `Project/project.uvprojx`。

4. 重新编译并在上位机执行 `help` 验证命令已出现。

---

## 6. 常见问题与排错

### 6.1 链接报错：`Undefined symbol shell_cmd_table_base/limit`

原因：旧方案依赖链接符号导出，在 ARMCC5 环境下不稳定。  
现方案已改为段标记对象（`ShellCmd$$Base/Limit`），不再依赖这两个符号。

若仍遇到类似问题，检查：
- `app1.sct/app2.sct` 是否包含：
  - `*(ShellCmd$$Base)`
  - `*(ShellCmd$$Table)`
  - `*(ShellCmd$$Limit)`
- `sys_shell_core.c` 是否使用 `s_shell_cmd_base_marker/s_shell_cmd_limit_marker` 扫描。
- 命令源文件是否真正加入工程编译。

### 6.2 `help` 看不到新命令

- 命令文件未加入 `uvprojx`。
- `EXPORT_SHELL_CMD` 没写在编译单元（例如被 `#if 0` 屏蔽）。
- 命令名重复，被注册阶段判冲突拒绝。

### 6.3 输入命令有响应但 Tab 不补全

这是上位机工具侧的本地补全列表问题。  
`tools/shell_nrt_client.py` 的 `COMMANDS` 需要同步新增命令。

---

## 7. 约束与建议

- 命令 handler 不要直接改数据池内部字段，优先走 `System_*` 统一接口。
- 写操作命令建议统一走 ModeManager，避免绕过状态机规则。
- 命令实现建议按业务拆文件，不要全部塞进一个 `core` 文件。
- 需要高风险控制时，利用 `permission_mask` 与 `allowed_mode_mask` 做双层限制。

---

## 8. 与现有代码的关系说明

当前自动注册机制已经在工程中启用，属于“可直接扩展”状态。  
后续你只需要在 `User/app_shell_cmds_*.c` 中新增命令并导出即可，不需要再改 `main` 或 `sys_shell_core.c`。

