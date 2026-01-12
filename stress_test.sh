#!/bin/bash

# 压力测试自动化脚本
# 逐级增加连接数，观察服务器性能变化

cd /home/hegang/UCP

# 创建测试结果目录
mkdir -p test_results
LOG_DIR="test_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
SUMMARY_LOG="$LOG_DIR/stress_test_summary_$TIMESTAMP.log"

echo "================================================"
echo "     TCP Server Stress Test Automation"
echo "================================================"
echo "Start time: $(date)"
echo "" | tee -a "$SUMMARY_LOG"

# 定义压力等级
# 格式: "连接数:持续时间秒:描述"
LOAD_LEVELS=(
    "10:10:轻负载测试"
    "50:15:中轻负载测试"
    "100:20:中等负载测试"
    "200:20:中重负载测试"
    "500:30:高负载测试"
    "1000:30:更高负载测试"
)

# 数据传输大小测试
MESSAGE_SIZES=(
    "ping\n"          # 5 字节
    "test message\n"  # 13 字节
    "This is a longer test message for bandwidth testing\n"  # 56 字节
)

# 检查服务器是否在运行
echo "[*] Checking if server is running on port 8888..."
if ! nc -z 127.0.0.1 8888 2>/dev/null; then
    echo "[!] Server not running on 127.0.0.1:8888"
    echo "[*] Make sure to run: ./bin/proactor_test &"
    echo ""
    read -p "Press enter after server is running..."
fi

echo ""
echo "========== Gradual Load Increase Test ==========" | tee -a "$SUMMARY_LOG"
echo "Testing with default message size (6 bytes)" | tee -a "$SUMMARY_LOG"
echo "" | tee -a "$SUMMARY_LOG"

# 逐级增加连接数测试
for load_config in "${LOAD_LEVELS[@]}"; do
    IFS=':' read -r conns duration desc <<< "$load_config"
    
    echo ""
    echo "[Test] $desc (Connections: $conns, Duration: ${duration}s)"
    echo "[Test] $desc (Connections: $conns, Duration: ${duration}s)" >> "$SUMMARY_LOG"
    
    log_file="$LOG_DIR/test_conn_${conns}_${TIMESTAMP}.log"
    
    ./bin/echo_bench \
        --connections "$conns" \
        --duration "$duration" \
        --message "test\n" \
        --log "$log_file" \
        2>&1 | tee -a "$SUMMARY_LOG"
    
    sleep 2  # 测试间隔
done

echo ""
echo "========== Message Size Impact Test ==========" | tee -a "$SUMMARY_LOG"
echo "Testing with 100 connections, 15 seconds" | tee -a "$SUMMARY_LOG"
echo "" | tee -a "$SUMMARY_LOG"

# 测试不同的消息大小
for idx in "${!MESSAGE_SIZES[@]}"; do
    msg="${MESSAGE_SIZES[$idx]}"
    msg_len=${#msg}
    
    echo ""
    echo "[Test] Message size test $((idx+1)): ${msg_len} bytes"
    echo "[Test] Message size test $((idx+1)): ${msg_len} bytes" >> "$SUMMARY_LOG"
    
    log_file="$LOG_DIR/test_msg_size_${msg_len}_${TIMESTAMP}.log"
    
    ./bin/echo_bench \
        --connections 100 \
        --duration 15 \
        --message "$msg" \
        --log "$log_file" \
        2>&1 | tee -a "$SUMMARY_LOG"
    
    sleep 2
done

echo ""
echo "========== Pressure Limit Test ==========" | tee -a "$SUMMARY_LOG"
echo "Finding the limit of the server" | tee -a "$SUMMARY_LOG"
echo "" | tee -a "$SUMMARY_LOG"

# 递进式寻找极限
EXTREME_LOADS=(
    "2000:30:极限测试1"
    "4000:30:极限测试2"
)

for load_config in "${EXTREME_LOADS[@]}"; do
    IFS=':' read -r conns duration desc <<< "$load_config"
    
    echo ""
    echo "[Test] $desc (Connections: $conns, Duration: ${duration}s)"
    echo "[Test] $desc (Connections: $conns, Duration: ${duration}s)" >> "$SUMMARY_LOG"
    
    log_file="$LOG_DIR/test_extreme_${conns}_${TIMESTAMP}.log"
    
    ./bin/echo_bench \
        --connections "$conns" \
        --duration "$duration" \
        --message "test\n" \
        --log "$log_file" \
        2>&1 | tee -a "$SUMMARY_LOG"
    
    sleep 3
done

echo ""
echo "================================================"
echo "Test completed at: $(date)"
echo "Summary log: $SUMMARY_LOG"
echo "Detail logs in: $LOG_DIR/"
echo "================================================"
echo ""
echo "To view results:"
echo "  cat $SUMMARY_LOG"
echo "  tail -f $LOG_DIR/test_*.log"
