#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
串口监控工具
用于监控 Zephyr 设备的串口输出

用法：python scripts/serial_monitor.py [-h] [-p PORT] [-b BAUD]

示例:
    python scripts/serial_monitor.py -p COM3 -b 115200
    python scripts/serial_monitor.py -p /dev/ttyUSB0 -b 115200
"""

import sys
import serial
import serial.tools.list_ports
import argparse
import time
from datetime import datetime

# ANSI 颜色代码
class Colors:
    RESET = '\033[0m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'

def list_ports():
    """列出可用的串口"""
    print(f"{Colors.CYAN}可用的串口:{Colors.RESET}")
    print("-" * 50)
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("  未找到串口")
        return []
    
    for port in ports:
        print(f"  {port.device}: {port.description}")
        if port.manufacturer:
            print(f"    制造商：{port.manufacturer}")
        if port.serial_number:
            print(f"    序列号：{port.serial_number}")
    print("-" * 50)
    return ports

def monitor_port(port, baudrate, timeout=0.1):
    """监控串口输出"""
    try:
        ser = serial.Serial(port, baudrate, timeout=timeout)
        print(f"{Colors.GREEN}已打开串口：{port} @ {baudrate}bps{Colors.RESET}")
        print(f"{Colors.YELLOW}按 Ctrl+C 退出{Colors.RESET}")
        print("-" * 50)
        
        line_buffer = ""
        message_count = 0
        error_count = 0
        start_time = time.time()
        
        while True:
            try:
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting).decode('utf-8', errors='replace')
                    
                    for char in data:
                        if char == '\n':
                            # 处理完整行
                            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                            
                            # 根据日志级别着色
                            line_color = Colors.WHITE
                            if "[ERR]" in line_buffer or "[E]" in line_buffer:
                                line_color = Colors.RED
                                error_count += 1
                            elif "[WRN]" in line_buffer or "[W]" in line_buffer:
                                line_color = Colors.YELLOW
                            elif "[INF]" in line_buffer or "[I]" in line_buffer:
                                line_color = Colors.GREEN
                            elif "[DBG]" in line_buffer or "[D]" in line_buffer:
                                line_color = Colors.CYAN
                            
                            print(f"{Colors.BLUE}[{timestamp}]{Colors.RESET} {line_color}{line_buffer}{Colors.RESET}")
                            message_count += 1
                            line_buffer = ""
                        else:
                            line_buffer += char
                    
                time.sleep(0.01)
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"{Colors.RED}读取错误：{e}{Colors.RESET}")
                error_count += 1
                time.sleep(0.1)
        
    except serial.SerialException as e:
        print(f"{Colors.RED}无法打开串口 {port}: {e}{Colors.RESET}")
        sys.exit(1)
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
        
        # 打印统计
        elapsed = time.time() - start_time
        print("\n" + "=" * 50)
        print(f"{Colors.CYAN}统计信息:{Colors.RESET}")
        print(f"  运行时间：{elapsed:.1f} 秒")
        print(f"  消息数量：{message_count}")
        print(f"  错误数量：{error_count}")
        if elapsed > 0:
            print(f"  平均速率：{message_count/elapsed:.1f} 消息/秒")
        print("=" * 50)

def main():
    parser = argparse.ArgumentParser(
        description='Zephyr 串口监控工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s -p COM3 -b 115200     Windows 示例
  %(prog)s -p /dev/ttyUSB0 -b 115200  Linux 示例
  %(prog)s -l                  列出可用串口
        """
    )
    
    parser.add_argument('-p', '--port', type=str, help='串口名称 (如 COM3 或 /dev/ttyUSB0)')
    parser.add_argument('-b', '--baud', type=int, default=115200, help='波特率 (默认：115200)')
    parser.add_argument('-l', '--list', action='store_true', help='列出可用串口')
    
    args = parser.parse_args()
    
    if args.list:
        list_ports()
        return 0
    
    if not args.port:
        # 自动选择第一个串口
        ports = serial.tools.list_ports.comports()
        if ports:
            args.port = ports[0].device
            print(f"{Colors.YELLOW}未指定串口，使用默认：{args.port}{Colors.RESET}")
        else:
            print(f"{Colors.RED}错误：未找到串口，请使用 -l 查看可用串口{Colors.RESET}")
            return 1
    
    monitor_port(args.port, args.baud)
    return 0

if __name__ == '__main__':
    sys.exit(main())
