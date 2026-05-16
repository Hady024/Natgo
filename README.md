# Natgo - 轻量级 Linux 端口转发管理器

natgo 是一个用 C++ 编写的、专门为 Linux 服务器（如阿里云、腾讯云等）设计的轻量级高效端口转发管理工具。它基于 iptables 和 ufw，提供了开箱即用的单页交互菜单，同时支持规则的持久化存储与开机全自动热加载。

## ✨ 项目特性

* 🚀 极简交互：全彩色、单页无缝刷新的命令行菜单，不留任何历史垃圾，退出程序后自动还原终端原本的命令历史。
* 🛡️ 四层转发：支持一键添加 TCP、UDP 或 TCP+UDP 双协议同步转发。
* 🤝 生态联动：全自动同步配置系统 iptables 规则及 UFW 防火墙策略，免去手动放行端口的烦恼。
* 💾 规则持久化：规则自动保存至本地 rules.conf，支持一键热重载。
* ⚡ 后台静默模式：支持 --load-only 参数，专为开机自启和自动化脚本设计，无任何菜单干扰。

## 🛠️ 环境要求

* Linux 操作系统（推荐 Ubuntu / Debian）
* 拥有 root 权限
* 已安装 g++ 编译器、iptables 和 ufw

## 📦 安装与编译

1. 克隆本项目或下载源码到服务器的 /root 目录下。
2. 在终端执行以下命令进行编译并赋予执行权限：

```bash
g++ -o natgo main.cpp
chmod +x ./natgo
```

## 🚀 使用指南

### 1. 交互菜单模式
必须以 root 权限启动：

```bash
sudo ./natgo
```

启动后将进入彩色管理菜单：
* 1 / 2 / 3：添加转发规则（支持随时输入 -1 安全取消并返回）。
* 4：查看当前活跃的转发列表。
* 5：删除指定的转发规则（相关端口和 UFW 策略会自动清理）。
* 6：从 rules.conf 配置文件中热更新并重新应用所有规则。
* 0：安全退出，无缝恢复你原本的终端命令历史。

### 2. 后台静默加载模式
仅从配置文件恢复防火墙规则，不打开交互菜单：

```bash
sudo ./natgo --load-only
```

## 🔄 配置开机全自动热加载

如果你希望服务器在重启后自动恢复之前配置的所有转发规则，无需重新移动文件，直接在 /root 目录下通过 systemd 服务即可实现。

1. 创建 systemd 服务配置文件：

```bash
sudo nano /etc/systemd/system/natgo.service
```

2. 将以下内容粘贴进去：

```ini
[Unit]
Description=Natgo Port Forwarding Boot Loader
After=network.target network-online.target ufw.service iptables.service
Wants=network-online.target

[Service]
Type=oneshot
WorkingDirectory=/root
ExecStart=/root/natgo --load-only
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

3. 保存并退出（在 nano 中按 Ctrl+O 保存，Ctrl+X 退出）。

4. 激活并启动该开机自启服务：

```bash
sudo systemctl daemon-reload
sudo systemctl enable natgo.service
sudo systemctl start natgo.service
```

5. 检查服务状态，确保显示绿色的 active (exited)：

```bash
sudo systemctl status natgo.service
```

## 📄 规则存储格式

所有规则都保存在同目录下的 rules.conf 中，格式如下。你也可以直接手动编辑此文件，随后在程序内按 6 键热重载：

```txt
协议类型|本地端口|远程目标ip|远程目标端口
tcp+udp|11111|1.1.1.1|11111|hk1番茄
tcp|22222|2.2.2.2|22222|黑五
```

## ⚠️ 注意事项

* 请确保本地监听端口未被其他服务占用。
* 在云服务器上使用时，除了系统内部的 ufw，请记得同时在云厂商的安全组/控制台防火墙中放行对应的本地监听端口。
