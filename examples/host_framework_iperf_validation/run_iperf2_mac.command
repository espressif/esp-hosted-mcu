#!/bin/zsh

set -u
setopt pipefail

SCRIPT_DIR="${0:A:h}"
cd "$SCRIPT_DIR" || exit 1

DUT_IP="192.168.4.1"
TEST_TIME="30"
INTERVAL="3"
UDP_BANDWIDTH="20"
QUICK_CHECK_DURATION="10"
INTER_CASE_COOLDOWN="5"
CONTINUE_ON_ERROR=0
IPERF_BIN=""

on_off_text() {
    if (( $1 )); then
        echo "On / 开启"
    else
        echo "Off / 关闭"
    fi
}

show_menu() {
    echo
    echo "ESP-Hosted iPerf2 macOS Launcher / macOS 启动器"
    echo "DUT IP / 设备 IP: $DUT_IP"
    echo "Duration / 时长: ${TEST_TIME}s, Interval / 间隔: ${INTERVAL}s, UDP Bandwidth / UDP 带宽: ${UDP_BANDWIDTH}M"
    echo "Quick Check / 快速巡检时长: ${QUICK_CHECK_DURATION}s, Cooldown / 用例冷却: ${INTER_CASE_COOLDOWN}s"
    echo "Continue On Error / 失败后继续: $(on_off_text $CONTINUE_ON_ERROR)"
    if [[ -n "$IPERF_BIN" ]]; then
        echo "iPerf Path / iperf 路径: $IPERF_BIN"
    else
        echo "iPerf Path / iperf 路径: auto / 自动检测"
    fi
    echo
    echo "Select test path / 选择测试路径:"
    echo "  1) All cases / 全部测试"
    echo "  2) TCP RX"
    echo "  3) TCP TX"
    echo "  4) UDP RX"
    echo "  5) UDP TX"
    echo "  6) TCP RX + TCP TX"
    echo "  7) UDP RX + UDP TX"
    echo "  8) Quick check / 快速巡检"
    echo "  9) Edit settings / 修改参数"
    echo "  C) Toggle continue-on-error / 切换失败后继续"
    echo "  0) Exit"
    echo
}

resolve_cases() {
    case "$1" in
        1) echo "all" ;;
        2) echo "tcp-rx" ;;
        3) echo "tcp-tx" ;;
        4) echo "udp-rx" ;;
        5) echo "udp-tx" ;;
        6) echo "tcp-rx,tcp-tx" ;;
        7) echo "udp-rx,udp-tx" ;;
        8) echo "quick-check" ;;
        9) echo "settings" ;;
        [cC]) echo "toggle-continue" ;;
        0) echo "exit" ;;
        *) echo "invalid" ;;
    esac
}

pause_for_user() {
    echo
    read "REPLY?Press Enter to return to menu... / 回车返回菜单..."
}

edit_settings() {
    local input
    local current_iperf_bin

    echo
    read "input?DUT IP / 设备 IP [$DUT_IP]: "
    DUT_IP=${input:-$DUT_IP}

    read "input?Duration in seconds / 测试时长（秒） [$TEST_TIME]: "
    TEST_TIME=${input:-$TEST_TIME}

    read "input?Interval in seconds / 打印间隔（秒） [$INTERVAL]: "
    INTERVAL=${input:-$INTERVAL}

    read "input?UDP bandwidth in Mbps / UDP 带宽（Mbps） [$UDP_BANDWIDTH]: "
    UDP_BANDWIDTH=${input:-$UDP_BANDWIDTH}

    read "input?Quick check duration / 快速巡检时长（秒） [$QUICK_CHECK_DURATION]: "
    QUICK_CHECK_DURATION=${input:-$QUICK_CHECK_DURATION}

    read "input?Inter-case cooldown / 用例冷却（秒） [$INTER_CASE_COOLDOWN]: "
    INTER_CASE_COOLDOWN=${input:-$INTER_CASE_COOLDOWN}

    current_iperf_bin=${IPERF_BIN:-auto}
    read "input?iPerf path (empty = auto) / iperf 路径（留空自动检测） [$current_iperf_bin]: "
    if [[ -n "$input" ]]; then
        if [[ "$input" == "auto" ]]; then
            IPERF_BIN=""
        else
            IPERF_BIN="$input"
        fi
    fi
}

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not found. Install Python 3 first. / 未找到 python3，请先安装 Python 3。"
    pause_for_user
    exit 1
fi

while true; do
    clear 2>/dev/null || true
    show_menu
    read "selection?Enter selection [1] / 请输入选项 [1]: "
    selection=${selection:-1}

    cases=$(resolve_cases "$selection")
    if [[ "$cases" == "exit" ]]; then
        exit 0
    fi

    if [[ "$cases" == "settings" ]]; then
        edit_settings
        continue
    fi

    if [[ "$cases" == "toggle-continue" ]]; then
        if (( CONTINUE_ON_ERROR )); then
            CONTINUE_ON_ERROR=0
        else
            CONTINUE_ON_ERROR=1
        fi
        echo
        echo "Continue-on-error: $(on_off_text $CONTINUE_ON_ERROR)"
        pause_for_user
        continue
    fi

    if [[ "$cases" == "invalid" ]]; then
        echo
        echo "Invalid selection: $selection / 无效选项：$selection"
        pause_for_user
        continue
    fi

    cmd=(python3 tools/run_iperf2.py
        --dut-ip "$DUT_IP"
        --time "$TEST_TIME"
        --interval "$INTERVAL"
        --udp-bandwidth "$UDP_BANDWIDTH"
        --inter-case-cooldown "$INTER_CASE_COOLDOWN")

    if (( CONTINUE_ON_ERROR )); then
        cmd+=(--continue-on-error)
    fi

    if [[ -n "$IPERF_BIN" ]]; then
        cmd+=(--iperf-bin "$IPERF_BIN")
    fi

    if [[ "$cases" == "quick-check" ]]; then
        cmd+=(--quick-check --quick-check-duration "$QUICK_CHECK_DURATION")
    else
        cmd+=(--cases "$cases")
    fi

    echo
    echo "Running ${cases} ... / 正在执行 ${cases} ..."
    echo

    "${cmd[@]}"
    exit_code=$?

    echo
    if [[ $exit_code -eq 0 ]]; then
        echo "Test finished successfully. / 测试完成。"
    else
        echo "Test failed with exit code $exit_code. / 测试失败，退出码：$exit_code。"
    fi

    pause_for_user
done