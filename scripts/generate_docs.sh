#!/bin/bash
# 生成 API 文档脚本
# 使用方法：./scripts/generate_docs.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "============================================"
echo "生成 API 文档"
echo "============================================"

# 检查 Doxygen 是否安装
if ! command -v doxygen &> /dev/null; then
    echo "错误：未找到 Doxygen，请先安装"
    echo "  Ubuntu/Debian: sudo apt-get install doxygen graphviz"
    echo "  macOS: brew install doxygen graphviz"
    echo "  Windows: choco install doxygen graphviz"
    exit 1
fi

# 创建输出目录
mkdir -p "$PROJECT_ROOT/docs/api"

# 生成文档
cd "$PROJECT_ROOT"
echo "正在生成文档..."
doxygen Doxyfile

echo ""
echo "============================================"
echo "文档生成成功！"
echo "============================================"
echo "输出目录：$PROJECT_ROOT/docs/api/html"
echo ""
echo "在浏览器中打开："
echo "  Windows: start docs/api/html/index.html"
echo "  macOS:   open docs/api/html/index.html"
echo "  Linux:   xdg-open docs/api/html/index.html"
echo ""
