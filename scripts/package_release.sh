#!/bin/bash
# 固件打包脚本
# 用于发布时打包构建产物

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "Zephyr 固件打包工具"
echo "============================================"

# 默认值
VERSION=""
BUILD_DIR="build"
OUTPUT_DIR="release"

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version)
            VERSION="$2"
            shift 2
            ;;
        -b|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "用法：$0 [选项]"
            echo ""
            echo "选项:"
            echo "  -v, --version VER     版本号"
            echo "  -b, --build-dir DIR   构建目录（默认：build）"
            echo "  -o, --output DIR      输出目录（默认：release）"
            echo "  -h, --help            显示帮助"
            exit 0
            ;;
        *)
            echo "未知选项：$1"
            exit 1
            ;;
    esac
done

# 如果没有指定版本号，尝试从 git 获取
if [ -z "$VERSION" ]; then
    if command -v git &> /dev/null; then
        VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "unknown")
    else
        VERSION="unknown"
    fi
fi

echo "版本号：$VERSION"
echo "构建目录：$BUILD_DIR"
echo "输出目录：$OUTPUT_DIR"
echo ""

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 复制固件文件
echo "正在复制固件文件..."

if [ -f "$BUILD_DIR/zephyr/zephyr.bin" ]; then
    cp "$BUILD_DIR/zephyr/zephyr.bin" "$OUTPUT_DIR/zephyr_${VERSION}.bin"
    echo "  ✓ zephyr.bin"
fi

if [ -f "$BUILD_DIR/zephyr/zephyr.elf" ]; then
    cp "$BUILD_DIR/zephyr/zephyr.elf" "$OUTPUT_DIR/zephyr_${VERSION}.elf"
    echo "  ✓ zephyr.elf"
fi

if [ -f "$BUILD_DIR/zephyr/zephyr.hex" ]; then
    cp "$BUILD_DIR/zephyr/zephyr.hex" "$OUTPUT_DIR/zephyr_${VERSION}.hex"
    echo "  ✓ zephyr.hex"
fi

if [ -f "$BUILD_DIR/zephyr/zephyr.map" ]; then
    cp "$BUILD_DIR/zephyr/zephyr.map" "$OUTPUT_DIR/zephyr_${VERSION}.map"
    echo "  ✓ zephyr.map"
fi

# 创建版本信息文件
echo "正在创建版本信息..."
cat > "$OUTPUT_DIR/version.txt" << EOF
Zephyr Event-Driven Project Template
Version: $VERSION
Build Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
Build Host: $(hostname)

Files:
$(ls -la "$OUTPUT_DIR"/*.bin "$OUTPUT_DIR"/*.elf "$OUTPUT_DIR"/*.hex 2>/dev/null || echo "  No binary files")
EOF

echo "  ✓ version.txt"

# 复制 README 和许可证
cp "$PROJECT_ROOT/README.md" "$OUTPUT_DIR/" 2>/dev/null && echo "  ✓ README.md"
cp "$PROJECT_ROOT/LICENSE" "$OUTPUT_DIR/" 2>/dev/null && echo "  ✓ LICENSE"

# 创建压缩包
echo ""
echo "正在创建压缩包..."

ARCHIVE_NAME="zephyr_template_${VERSION}"

cd "$OUTPUT_DIR"

if command -v zip &> /dev/null; then
    zip -r "${ARCHIVE_NAME}.zip" . \
        -x "*.git*" \
        -x "*.o" \
        -x "*.d"
    echo "  ✓ ${ARCHIVE_NAME}.zip"
fi

if command -v tar &> /dev/null; then
    tar -czf "${ARCHIVE_NAME}.tar.gz" \
        --exclude='.git' \
        --exclude='*.o' \
        --exclude='*.d' \
        .
    echo "  ✓ ${ARCHIVE_NAME}.tar.gz"
fi

cd "$PROJECT_ROOT"

echo ""
echo "============================================"
echo "打包完成"
echo "============================================"
echo "输出目录：$OUTPUT_DIR"
echo ""
echo "文件列表:"
ls -la "$OUTPUT_DIR"
echo ""
