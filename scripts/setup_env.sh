#!/bin/bash
# =============================================================================
# Zephyr 环境设置脚本 (Linux/macOS)
# =============================================================================
# 用法：source scripts/setup_env.sh
# =============================================================================

set -e

echo "============================================"
echo "Zephyr 环境设置"
echo "============================================"

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="$PROJECT_ROOT/zephyr_config.env"

# 检查配置文件是否存在
if [ ! -f "$CONFIG_FILE" ]; then
    echo "错误：找不到 zephyr_config.env！"
    echo "请复制 zephyr_config.env.template 到 zephyr_config.env 并编辑路径。"
    return 1 2>/dev/null || exit 1
fi

# 加载配置
echo "正在从 zephyr_config.env 加载配置..."
set -a
source "$CONFIG_FILE"
set +a

# 验证路径
if [ -z "$ZEPHYR_BASE" ]; then
    echo "错误：配置中未设置 ZEPHYR_BASE！"
    return 1 2>/dev/null || exit 1
fi

if [ -z "$ZEPHYR_SDK_INSTALL_DIR" ]; then
    echo "错误：配置中未设置 ZEPHYR_SDK_INSTALL_DIR！"
    return 1 2>/dev/null || exit 1
fi

if [ ! -d "$ZEPHYR_BASE" ]; then
    echo "错误：ZEPHYR_BASE 路径不存在：$ZEPHYR_BASE"
    return 1 2>/dev/null || exit 1
fi

if [ ! -d "$ZEPHYR_SDK_INSTALL_DIR" ]; then
    echo "错误：ZEPHYR_SDK_INSTALL_DIR 路径不存在：$ZEPHYR_SDK_INSTALL_DIR"
    return 1 2>/dev/null || exit 1
fi

# 导出环境变量
export ZEPHYR_BASE
export ZEPHYR_SDK_INSTALL_DIR

# 添加 Zephyr 工具到 PATH
if [ -d "$ZEPHYR_SDK_INSTALL_DIR/arm-zephyr-eabi/bin" ]; then
    export PATH="$ZEPHYR_SDK_INSTALL_DIR/arm-zephyr-eabi/bin:$PATH"
fi

if [ -d "$ZEPHYR_SDK_INSTALL_DIR/tools/bin" ]; then
    export PATH="$ZEPHYR_SDK_INSTALL_DIR/tools/bin:$PATH"
fi

# 运行 Zephyr 环境设置脚本（如果存在）
if [ -f "$ZEPHYR_BASE/scripts/env.sh" ]; then
    echo "正在运行 Zephyr 环境脚本..."
    source "$ZEPHYR_BASE/scripts/env.sh"
fi

echo "============================================"
echo "环境配置成功！"
echo "============================================"
echo "ZEPHYR_BASE=$ZEPHYR_BASE"
echo "ZEPHYR_SDK_INSTALL_DIR=$ZEPHYR_SDK_INSTALL_DIR"
echo "============================================"
echo ""
echo "现在可以构建项目："
echo "  west build -b ${DEFAULT_BOARD:-<your_board>} ."
echo ""
