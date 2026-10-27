"""SDK串口工作台：使用Windows自带串口API，不需要额外Python包。"""
import argparse
import csv
from datetime import datetime
from pathlib import Path
import queue
import tkinter as tk
from tkinter import ttk, messagebox

from serial_transport import Serial


def parse_telemetry(line):
    result = {}
    for field in line.split(","):
        key, separator, value = field.partition("=")
        if separator:
            try:
                result[key.strip()] = int(value.strip())
            except ValueError:
                return None
    required = {"T_dC", "SET_dC", "STATE", "PWM_pm", "VALID"}
    return result if required <= result.keys() else None


class Panel:
    def __init__(self, root):
        self.root, self.serial = root, None
        self.rows = []
        root.title("ThermoGuard SDK 串口工作台")
        root.geometry("920x600")
        main = ttk.Frame(root, padding=16)
        main.pack(fill="both", expand=True)
        connect = ttk.Frame(main)
        connect.pack(fill="x")
        self.port = tk.StringVar(value="COM7")
        ttk.Label(connect, text="串口 115200 / 8N1").pack(side="left")
        ttk.Entry(connect, textvariable=self.port, width=12).pack(side="left", padx=8)
        ttk.Button(connect, text="连接", command=self.open).pack(side="left")
        ttk.Button(connect, text="断开", command=self.close).pack(side="left", padx=8)
        self.connection = tk.StringVar(value="未连接")
        ttk.Label(connect, textvariable=self.connection).pack(side="left")

        self.status = tk.StringVar(value="等待ECU遥测")
        ttk.Label(main, textvariable=self.status, font=("Microsoft YaHei UI", 14)).pack(anchor="w", pady=18)
        buttons = ttk.Frame(main)
        buttons.pack(fill="x")
        for title, command in (("启用", "ON"), ("停用", "OFF"), ("本地模式", "LOCAL"), ("远程模式", "REMOTE")):
            ttk.Button(buttons, text=title, command=lambda c=command: self.send(c)).pack(side="left", padx=3)
        self.target = tk.IntVar(value=450)
        ttk.Spinbox(buttons, from_=200, to=800, textvariable=self.target, width=8).pack(side="left", padx=12)
        ttk.Button(buttons, text="设置目标(0.1℃)", command=self.set_target).pack(side="left")
        ttk.Button(buttons, text="导出CSV到G盘", command=self.export).pack(side="right")
        ttk.Label(main, text="远程模式需要CAN控制心跳；目标设置要求本地停机。页面显示以ECU回传为准。").pack(anchor="w", pady=10)
        self.log = tk.Text(main, height=20, wrap="none", font=("Consolas", 10))
        self.log.pack(fill="both", expand=True)
        root.protocol("WM_DELETE_WINDOW", self.shutdown)
        root.after(100, self.poll)

    def open(self):
        if self.serial is not None:
            return
        try:
            self.serial = Serial(self.port.get())
            self.connection.set("已连接 " + self.port.get().upper())
        except (OSError, ValueError, AttributeError) as error:
            messagebox.showerror("连接失败", str(error))

    def close(self):
        if self.serial is not None:
            self.serial.close()
            self.serial = None
        self.connection.set("未连接")

    def send(self, command):
        if self.serial is None:
            messagebox.showinfo("串口", "请先连接ECU串口。")
            return
        try:
            self.serial.send(command)
            self.append("> " + command)
        except (OSError, RuntimeError) as error:
            messagebox.showerror("发送失败", str(error))

    def set_target(self):
        try:
            value = self.target.get()
            if not 200 <= value <= 800:
                raise ValueError("目标范围为200..800，即20.0..80.0℃")
        except (ValueError, tk.TclError) as error:
            messagebox.showerror("目标温度", str(error))
            return
        self.send("SET " + str(value))

    def append(self, text):
        self.log.insert("end", text + "\n")
        if int(self.log.index("end-1c").split(".")[0]) > 500:
            self.log.delete("1.0", "100.0")
        self.log.see("end")

    def poll(self):
        if self.serial is not None:
            for _ in range(100):
                try:
                    line = self.serial.lines.get_nowait()
                except queue.Empty:
                    break
                if isinstance(line, dict):
                    self.append("串口错误: " + line.get("error", "unknown"))
                    self.close()
                    break
                self.append(line)
                values = parse_telemetry(line)
                if values is not None:
                    state = {0: "关闭", 1: "运行", 2: "故障保护", 3: "降级"}.get(values["STATE"], "未知")
                    self.status.set("温度 %.1f℃   目标 %.1f℃   PWM %d‰   %s   模式:%s" % (
                        values["T_dC"] / 10, values["SET_dC"] / 10, values["PWM_pm"], state,
                        "远程" if values.get("REMOTE", 0) else "本地"))
                    values["PC_TIME"] = datetime.now().isoformat(timespec="milliseconds")
                    self.rows.append(values)
                    if len(self.rows) > 10000:
                        del self.rows[:1000]
        self.root.after(100, self.poll)

    def export(self):
        if not self.rows:
            messagebox.showinfo("导出", "尚无遥测数据。")
            return
        out = Path(__file__).resolve().parents[1] / "build" / "exports"
        try:
            out.mkdir(parents=True, exist_ok=True)
            path = out / ("telemetry-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".csv")
            fields = sorted(set().union(*(row.keys() for row in self.rows)))
            with path.open("w", newline="", encoding="utf-8-sig") as file:
                writer = csv.DictWriter(file, fieldnames=fields)
                writer.writeheader()
                writer.writerows(self.rows)
            messagebox.showinfo("已导出", str(path))
        except OSError as error:
            messagebox.showerror("导出失败", str(error))

    def shutdown(self):
        self.close()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        assert parse_telemetry("T_dC=303,SET_dC=450,STATE=0,PWM_pm=0,VALID=1")["T_dC"] == 303
        assert parse_telemetry("ACK REQ=1") is None
        assert parse_telemetry("T_dC=oops") is None
        root = tk.Tk()
        root.withdraw()
        Panel(root)
        root.update_idletasks()
        root.destroy()
        print("PASS: panel telemetry parsing and hidden window construction")
        return
    root = tk.Tk()
    Panel(root)
    root.mainloop()


if __name__ == "__main__":
    main()
