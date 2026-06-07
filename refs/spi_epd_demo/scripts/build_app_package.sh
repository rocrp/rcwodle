#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${SDK:-/Users/godzilla/Documents/Code/workspace/wodle-dev/repos/opensifli/SiFli-SDK}"
VERSION="V1.4.0.9001"
GENERATED_AT="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
FIRMWARE="flash_smoke"
BOARD="wodle"
APP_ADDR="0x12218000"
APP_REGION="0x00240000"

usage() {
    cat <<'USAGE'
用法:
  scripts/build_app_package.sh [选项]

选项:
  --sdk PATH        SiFli-SDK 路径，默认使用 wodle-dev 中的本地 SDK
  --firmware NAME  固件目录名，默认 flash_smoke
  --version VER    update.json 版本号，默认 V1.4.0.9001
  --at ISO_TIME    update.json generated_at，默认当前 UTC 时间
  -h, --help       显示帮助

输出:
  firmware/<NAME>/dist/hcpu_app.bin
  firmware/<NAME>/dist/update.json
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sdk)
            SDK="$2"
            shift 2
            ;;
        --firmware)
            FIRMWARE="$2"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --at)
            GENERATED_AT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "未知参数: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

PROJECT="$ROOT/firmware/$FIRMWARE/project"
BUILD="$PROJECT/build_wodle_hcpu"
BIN="$BUILD/output/main.bin"
ELF="$BUILD/main.elf"
DIST="$ROOT/firmware/$FIRMWARE/dist"
MANIFEST="$DIST/update.json"

if [[ ! -d "$SDK" ]]; then
    echo "SDK 不存在: $SDK" >&2
    exit 1
fi

if [[ ! -f "$SDK/export.sh" ]]; then
    echo "SDK export.sh 不存在: $SDK/export.sh" >&2
    exit 1
fi

if [[ ! -d "$PROJECT" ]]; then
    echo "固件工程不存在: $PROJECT" >&2
    exit 1
fi

echo "[1/6] 接入 SDK 板卡定义"
rm -rf "$SDK/customer/boards/wodle"
ln -s "$ROOT/board/wodle" "$SDK/customer/boards/wodle"

echo "[2/6] 激活 SDK 环境"
# shellcheck disable=SC1090
source "$SDK/export.sh"

if [[ "$FIRMWARE" == "text_render_smoke" ]]; then
    echo "[2.5/6] 生成文字渲染字体子集"
    python3 "$ROOT/tools/gen_text_font.py"
fi

if [[ "$FIRMWARE" == "gray4_smoke" ]]; then
    echo "[2.5/6] 生成 4 灰阶文字测试页"
    python3 "$ROOT/tools/gen_gray4_text_pages.py"
fi

echo "[3/6] 编译 $FIRMWARE"
cd "$PROJECT"
scons --board="$BOARD" -j8

echo "[4/6] 检查 ELF 链接地址"
FIRST_LOAD="$(arm-none-eabi-readelf -l "$ELF" | awk '/LOAD/{print $3; exit}')"
if [[ "$FIRST_LOAD" != "$APP_ADDR" ]]; then
    echo "ELF 第一个 LOAD 地址错误: $FIRST_LOAD，期望 $APP_ADDR" >&2
    exit 1
fi
echo "ELF 第一个 LOAD 地址: $FIRST_LOAD"

echo "[5/6] 生成仅 app 升级包"
mkdir -p "$DIST"
cp "$BIN" "$DIST/hcpu_app.bin"
uv run "$ROOT/tools/mk_update.py" "$DIST" --version "$VERSION" --at "$GENERATED_AT"

echo "[6/6] 检查 update.json"
python3 - "$MANIFEST" "$DIST/hcpu_app.bin" "$APP_ADDR" "$APP_REGION" <<'PY'
import json
import sys
from pathlib import Path

manifest = Path(sys.argv[1])
bin_path = Path(sys.argv[2])
app_addr = sys.argv[3]
app_region = sys.argv[4]

data = json.loads(manifest.read_text())
files = data.get("files", [])
if len(files) != 1:
    raise SystemExit(f"update.json 必须只包含 1 个文件，当前为 {len(files)}")

entry = files[0]
checks = {
    "name": "hcpu_app.bin",
    "addr": app_addr,
    "region_size": app_region,
    "file_id": 0,
    "size": bin_path.stat().st_size,
}

for key, expected in checks.items():
    actual = entry.get(key)
    if actual != expected:
        raise SystemExit(f"{key} 不匹配: {actual!r}，期望 {expected!r}")

print(f"update.json OK: size={entry['size']} crc={entry['crc32']}")
PY

echo
echo "完成:"
echo "  $DIST/hcpu_app.bin"
echo "  $MANIFEST"
