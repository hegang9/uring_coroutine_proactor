#!/bin/bash

# --- 配置区域 ---
# 目标 URL，默认为本机 6666 端口
TARGET_URL=${1:-"http://192.168.236.127:6666"}
# 每次压测持续时间
DURATION="20s"
# 固定 wrk 线程数为 4，避免抢占过多 CPU 资源影响服务端
THREADS=4
# 压测连接数配置
START_CONN=100
MAX_CONN=4000
STEP=100
# 结果保存文件
OUTPUT_FILE="benchmark_summary.csv"

# --- 检查依赖 ---
if ! command -v wrk &> /dev/null; then
    echo "错误: 未找到 'wrk' 命令。请先安装它 (例如: sudo apt install wrk 或 sudo yum install wrk)"
    exit 1
fi

echo "=========================================================="
echo "🚀 开始自动化压测"
echo "目标地址: $TARGET_URL"
echo "持续时间/轮: $DURATION"
echo "wrk线程数: $THREADS"
echo "并发连接数: $START_CONN -> $MAX_CONN (步长: $STEP)"
echo "=========================================================="

# 初始化 CSV 文件头
echo "Connections,RPS,Avg_Latency,Transfer/sec,Errors" > $OUTPUT_FILE

echo "正在进行压测，详细日志如下:"

for (( c=$START_CONN; c<=$MAX_CONN; c+=$STEP )); do
    # wrk 的线程数不能超过连接数，做个保护
    curr_threads=$THREADS
    if [ $c -lt $THREADS ]; then
        curr_threads=$c
    fi
    
    # 运行 wrk
    # -t: 线程数, -c: 连接数, -d: 持续时间, --latency: 打印延迟分布, --timeout: 超时时间
    output=$(wrk -t$curr_threads -c$c -d$DURATION --timeout 5s --latency $TARGET_URL)
    
    # --- 解析输出结果 ---
    
    # 1. 提取 Requests/sec
    # 示例: Requests/sec: 964144.79
    rps=$(echo "$output" | grep "Requests/sec:" | awk '{print $2}')
    
    # 2. 提取平均延迟 (Latency Avg)
    # 示例: Latency   501.77us  141.79us   8.31ms   89.36%
    # head -n 1 是为了防止有多个 Latency 行
    latency=$(echo "$output" | grep "Latency" | head -n 1 | awk '{print $2}')
    
    # 3. 提取吞吐量
    # 示例: Transfer/sec:    100.22MB
    transfer=$(echo "$output" | grep "Transfer/sec:" | awk '{print $2}')
    
    # 4. 检查是否有 Socket errors
    # 示例: Socket errors: connect 0, read 0, write 0, timeout 55
    errors=$(echo "$output" | grep "Socket errors:")
    if [ -z "$errors" ]; then
        errors="None"
    else
        # 简化显示，只显示冒号后面的部分
        errors=$(echo "$errors" | cut -d':' -f2 | xargs)
    fi
    
    # --- 输出与保存 ---
    
    # 打印 wrk 原始输出到控制台
    echo "=========================================================="
    echo "Connections: $c"
    echo "$output"
    
    # 写入 CSV
    echo "$c,$rps,$latency,$transfer,\"$errors\"" >> $OUTPUT_FILE
    
    # 稍微休息一下，避免端口暂未释放
    sleep 5
done

echo "=========================================================="
echo "✅ 测试完成！summary 已保存至: $OUTPUT_FILE"
echo "你可以使用 Excel 或脚本分析该 CSV 文件以绘制 RPS 曲线。"
