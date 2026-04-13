#ifndef __APP_SHELL_H
#define __APP_SHELL_H

/*
 * @brief Shell 组装层初始化入口。
 * @note  该模块位于 User 层，负责把 System Shell Core 与具体传输适配器绑定，
 *        以保持 main.c 简洁并降低跨层耦合。
 */
void App_Shell_Init(void);

#endif /* __APP_SHELL_H */
