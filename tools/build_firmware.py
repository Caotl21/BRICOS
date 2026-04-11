import os
import subprocess
import xml.etree.ElementTree as ET

# --- 配置信息 ---

# 获取当前脚本所在目录 (D:\...\tools)
script_dir = os.path.dirname(os.path.abspath(__file__))

# 找到项目根目录 (tools 的上一级)
root_dir = os.path.dirname(script_dir)

# Keil 安装路径下的 UV4.exe 路径
KEIL_EXE = r"D:\Keil_v5\UV4\UV4.exe" 

# 工程文件路径
PROJECT_PATH = os.path.join(root_dir, "Project", "project.uvprojx")

TARGET_NAME = "Target 1"

# 编译日志输出路径
LOG_FILE = os.path.join(root_dir, "build_log.txt")

def modify_keil_macros(xml_path, new_defines):
    """
    修改 .uvprojx 中的 C/C++ 宏定义
    :param xml_path: 工程文件路径
    :param new_defines: 字符串格式的宏定义，例如 "STM32F10X_MD,USE_STDPERIPH_DRIVER"
    """
    # 解析 XML
    tree = ET.parse(xml_path)
    root = tree.getroot()

    # 寻找 <Define> 节点
    # 注意：Keil 的 XML 结构很深，通常路径是：
    # Targets -> Target -> TargetOption -> TargetArmAds -> Cads -> VariousControls -> Define
    found = False
    for define_node in root.findall(".//Define"):
        define_node.text = new_defines
        found = True
    
    if found:
        tree.write(xml_path, encoding="utf-8", xml_declaration=True)
        print(f"成功更新宏定义为: {new_defines}")
    else:
        print("未找到 Define 节点，请检查工程文件结构。")

def build_keil_project():
    """
    通过命令行调用 Keil 进行编译
    """
    if not os.path.exists(KEIL_EXE):
        print("错误: 找不到 UV4.exe，请检查配置路径。")
        return

    # 构造命令
    # -r: 全局重新编译 (Rebuild)
    # -j0: 多核编译（加速）
    # -o: 将输出重定向到日志文件
    cmd = [KEIL_EXE, "-r", PROJECT_PATH, "-t", TARGET_NAME, "-o", LOG_FILE]

    print(f"正在编译工程: {TARGET_NAME} ...")
    
    try:
        # 运行并等待结束
        result = subprocess.run(cmd, shell=True)
        
        # Keil 的退出码含义:
        # 0: 无错误无警告, 1: 有警告, 2: 有错误, 3: 致命错误
        if result.returncode <= 1:
            print("🎉 编译成功!")
        else:
            print(f"❌ 编译失败，退出码: {result.returncode}")
            if os.path.exists(LOG_FILE):
                print(f"请检查日志文件: {LOG_FILE}")

    except Exception as e:
        print(f"执行出错: {e}")

if __name__ == "__main__":
    # 第一步：修改宏定义（可选，例如自动化测试不同功能）
    # modify_keil_macros(PROJECT_PATH, "STM32F10X_HD,USE_STDPERIPH_DRIVER,VERSION_V1")

    # 第二步：执行编译
    build_keil_project()