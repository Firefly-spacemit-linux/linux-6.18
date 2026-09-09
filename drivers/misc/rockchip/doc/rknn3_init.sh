#!/bin/bash
# rknn3_init.sh - 重启后执行：加载 pcie-rkep 驱动 + QEMU ARM64 环境
# 用法: sudo bash rknn3_init.sh
# 说明: 放到 rknn3-extract/ 目录下与 pcie-rkep.ko 和 system_root/ 同级

set -e

RKNN3_DIR="$(dirname "$(readlink -f "$0")")"
DRIVER="$RKNN3_DIR/pcie-rkep.ko"

echo "============================================"
echo "  RKNN3 环境初始化"
echo "============================================"

# 1. binfmt_misc
echo "[1/5] 加载 binfmt_misc..."
modprobe binfmt_misc 2>/dev/null || true
mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true

# 2. qemu-aarch64 注册
echo "[2/5] 注册 qemu-aarch64..."
if [ ! -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
    echo ':qemu-aarch64:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-aarch64:F' > /proc/sys/fs/binfmt_misc/register
    echo "  - ✅ 注册成功"
else
    echo "  - ✅ 已注册"
fi

# 3. 加载驱动
echo "[3/5] 加载 pcie-rkep 驱动..."
if [ -f "$DRIVER" ]; then
    if lsmod | grep -q "pcie_rkep"; then
        echo "  - ✅ 驱动已加载"
    else
        insmod "$DRIVER" && echo "  - ✅ 加载成功" || echo "  - ❌ 加载失败"
    fi
else
    echo "  - ⚠️ 未找到驱动: $DRIVER"
fi

# 4. 检查设备节点
echo "[4/5] 等待设备就绪..."
sleep 1
if ls /dev/rk* >/dev/null 2>&1; then
    echo "  - ✅ 设备节点: $(ls /dev/rk* 2>/dev/null)"
else
    echo "  - ⚠️ 未检测到设备节点"
fi

# 5. 验证 QEMU
echo "[5/5] 验证 ARM64 模拟..."
if qemu-aarch64 --version >/dev/null 2>&1; then
    echo "  - ✅ QEMU 可用: $(qemu-aarch64 --version 2>&1 | head -1)"
else
    echo "  - ❌ QEMU 不可用"
fi

echo ""
echo "============================================"
echo "  初始化完成！运行 bash rknn3_test.sh 验证"
echo "============================================"