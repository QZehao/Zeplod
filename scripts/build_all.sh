#!/bin/bash
# 批量构建脚本
# 用于为多个开发板构建项目

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "Zephyr 批量构建工具"
echo "============================================"

# 默认开发板列表
BOARDS=(
    "native_posix"
    "nucleo_f429zi"
    "nucleo_f767zi"
    "disco_l475_iot1"
    "mimxrt1050_evk"
)

# 解析参数
CLEAN_BUILD=false
OUTPUT_DIR=""
TARGET_BOARD=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -b|--board)
            TARGET_BOARD="$2"
            shift 2
            ;;
        -h|--help)
            echo "用法：$0 [选项]"
            echo ""
            echo "选项:"
            echo "  -c, --clean       清理后构建"
            echo "  -o, --output DIR  输出目录"
            echo "  -b, --board NAME  指定开发板（可多次使用）"
            echo "  -h, --help        显示帮助"
            exit 0
            ;;
        *)
            echo "未知选项：$1"
            exit 1
            ;;
    esac
done

# 如果指定了开发板，只构建指定的
if [ -n "$TARGET_BOARD" ]; then
    BOARDS=("$TARGET_BOARD")
fi

# 统计
SUCCESS_COUNT=0
FAIL_COUNT=0
START_TIME=$(date +%s)

# 构建每个开发板
for board in "${BOARDS[@]}"; do
    echo ""
    echo "============================================"
    echo "构建：$board"
    echo "============================================"
    
    BUILD_DIR="build_${board}"
    
    if [ -n "$OUTPUT_DIR" ]; then
        BUILD_DIR="$OUTPUT_DIR/$BUILD_DIR"
    fi
    
    # 构建命令
    BUILD_CMD="west build -b $board --build-dir $BUILD_DIR"
    
    if [ "$CLEAN_BUILD" = true ]; then
        BUILD_CMD="$BUILD_CMD --clean"
    fi
    
    echo "命令：$BUILD_CMD"
    echo ""
    
    cd "$PROJECT_ROOT"
    
    if $BUILD_CMD; then
        echo "✓ 构建成功：$board"
        ((SUCCESS_COUNT++))
        
        # 复制构建产物
        if [ -n "$OUTPUT_DIR" ]; then
            mkdir -p "$OUTPUT_DIR/release"
            if [ -f "$BUILD_DIR/zephyr/zephyr.bin" ]; then
                cp "$BUILD_DIR/zephyr/zephyr.bin" "$OUTPUT_DIR/release/zephyr_${board}.bin"
            fi
            if [ -f "$BUILD_DIR/zephyr/zephyr.elf" ]; then
                cp "$BUILD_DIR/zephyr/zephyr.elf" "$OUTPUT_DIR/release/zephyr_${board}.elf"
            fi
            if [ -f "$BUILD_DIR/zephyr/zephyr.hex" ]; then
                cp "$BUILD_DIR/zephyr/zephyr.hex" "$OUTPUT_DIR/release/zephyr_${board}.hex"
            fi
        fi
    else
        echo "✗ 构建失败：$board"
        ((FAIL_COUNT++))
    fi
done

# 统计
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo ""
echo "============================================"
echo "构建完成"
echo "============================================"
echo "成功：$SUCCESS_COUNT"
echo "失败：$FAIL_COUNT"
echo "总耗时：${DURATION}秒"
echo ""

if [ $FAIL_COUNT -gt 0 ]; then
    exit 1
fi
