
/* ====================================================================
 * 【重要声明 / Notice】
 * 本代码文件中的所有业务逻辑、底层网络命令解释及交互流程的相关注释，
 * 均由人工智能（AI 助理）自动生成，旨在协助开发者理解代码及后期维护。
 * ==================================================================== 
*/

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <unistd.h>     // 用于 getuid() 和 usleep() 等系统调用
#include <sys/types.h>  // 包含系统数据类型定义

using namespace std;

// === 终端 ANSI 转义序列定义（用于彩色输出和界面控制） ===
#define RESET   "\033[0m"      // 恢复默认颜色
#define GREEN   "\033[1;32m"   // 亮绿色
#define BLUE    "\033[1;36m"   // 亮青色/蓝色
#define YELLOW  "\033[1;33m"   // 亮黄色
#define RED     "\033[1;31m"   // 亮红色
#define PURPLE  "\033[1;35m"   // 亮紫色

// 存储单条转发规则的数据结构
struct ForwardRule {
    string proto;       // 协议类型: "tcp", "udp" 或 "tcp+udp"
    int local_port;     // 本地监听的端口
    string remote_ip;   // 远程目标的 IP 地址
    int remote_port;    // 远程目标的端口
    string comment;     // 规则备注，方便用户和 iptables 辨认
};

// NAT 核心管理类
class NatManager {
private:
    const string config_file = "rules.conf"; // 规则持久化存储的文件名
    vector<ForwardRule> rules;               // 内存中缓存的规则列表

    // 封装系统命令执行函数
    void exec(const string& cmd) {
        system(cmd.c_str()); // 调用 C 标准库的 system 执行 shell 命令
    }

    /**
     * 构建并执行底层的 Linux 网络防火墙命令
     * @param action "-A" 表示添加规则(Append)，"-D" 表示删除规则(Delete)
     */
    void exec_network_cmd(const string& p, int local_port, const string& rip, int rport, const string& comment, string action) {
        string lp = to_string(local_port);
        string rp = to_string(rport);
        // 为 iptables 规则加上备注模块，防止误删其他系统规则
        string cmt = " -m comment --comment \"" + comment + "\"";

        if (action == "-A") {
            // 1. DNAT 规则：将访问本地端口的流量，目标地址重定向到远程 IP:端口
            exec("iptables -t nat -A PREROUTING -p " + p + " --dport " + lp + " -j DNAT --to-destination " + rip + ":" + rp + cmt);
            // 2. SNAT (MASQUERADE)：伪装源 IP，确保远程目标响应时能将数据包原路返回
            exec("iptables -t nat -A POSTROUTING -p " + p + " -d " + rip + " --dport " + rp + " -j MASQUERADE" + cmt);
            // 3. FORWARD 允许流出：允许转发去往远程目标 IP 和端口的数据包
            exec("iptables -A FORWARD -p " + p + " -d " + rip + " --dport " + rp + " -j ACCEPT" + cmt);
            // 4. FORWARD 允许流入：允许从远程目标 IP 和端口返回的数据包通过
            exec("iptables -A FORWARD -p " + p + " -s " + rip + " --sport " + rp + " -j ACCEPT" + cmt);
            // 5. UFW 放行：如果系统启用了 UFW 防火墙，同步放行本地监听端口（重定向输出阻止刷屏）
            exec("ufw allow " + lp + "/" + p + " > /dev/null 2>&1");
        } 
        else if (action == "-D") {
            // 与添加规则完全对称，执行删除操作 (-D)
            exec("iptables -t nat -D PREROUTING -p " + p + " --dport " + lp + " -j DNAT --to-destination " + rip + ":" + rp + cmt);
            // 注意：当出现单端口映射多 IP 等极端情况时，iptables 按精确匹配删除
            exec("iptables -t nat -D POSTROUTING -p " + p + " -d " + rip + " --dport " + rp + " -j MASQUERADE" + cmt);
            exec("iptables -D FORWARD -p " + p + " -d " + rip + " --dport " + rp + " -j ACCEPT" + cmt);
            exec("iptables -D FORWARD -p " + p + " -s " + rip + " --sport " + rp + " -j ACCEPT" + cmt);
            // 在 UFW 中删除对应的放行规则
            exec("ufw delete allow " + lp + "/" + p + " > /dev/null 2>&1");
        }
    }

    // 根据协议类型，分发网络规则（支持同时处理 tcp 和 udp）
    void apply_network_rules(const ForwardRule& r, string action) {
        if (r.proto == "tcp+udp") {
            exec_network_cmd("tcp", r.local_port, r.remote_ip, r.remote_port, r.comment, action);
            exec_network_cmd("udp", r.local_port, r.remote_ip, r.remote_port, r.comment, action);
        } else {
            exec_network_cmd(r.proto, r.local_port, r.remote_ip, r.remote_port, r.comment, action);
        }
    }

public:
    // 系统环境初始化：一键开启 Linux 内核的 IPv4 转发功能（转发的核心前提）
    void init_system() {
        exec("sysctl -w net.ipv4.ip_forward=1 > /dev/null");
    }

    // 进入终端的“备用屏幕”（就像 vim 一样，退出程序后不污染原有的控制台历史输出）
    void enter_alt_screen() {
        cout << "\033[?1049h" << flush;
    }

    // 退出终端的“备用屏幕”并回到主控制台
    void exit_alt_screen() {
        cout << "\033[?1049l" << flush;
    }

    // 清空当前屏幕并将光标复位到左上角 (0,0) 位置
    void clear_canvas() {
        cout << "\033[2J\033[H" << flush;
    }

    // 阻塞函数，等待用户按下回车键，常用于展示完列表或提示信息后
    void wait_for_keypress() {
        cout << "\n按回车键返回主菜单...";
        cin.ignore(10000, '\n'); // 清空输入流中的多余字符
        cin.get();               // 捕获回车键
    }

    // 添加新规则的公共接口
    void add_rule(string proto, int lport, string rip, int rport, string comment) {
        ForwardRule r = {proto, lport, rip, rport, comment};
        apply_network_rules(r, "-A"); // 应用到内核防火墙
        rules.push_back(r);           // 同步到内存中
        save_to_file();               // 持久化到 rules.conf
        cout << GREEN << "\n[成功] 已添加 [" << proto << "] 转发并同步 UFW。" << RESET << endl;
    }

    // 删除指定索引规则的公共接口
    void remove_rule(int index) {
        if (index < 0 || index >= (int)rules.size()) {
            cout << RED << "\n[错误] 无效的索引!" << RESET << endl;
            return;
        }
        apply_network_rules(rules[index], "-D"); // 从内核防火墙中卸载
        rules.erase(rules.begin() + index);      // 从内存向量中抹除
        save_to_file();                          // 重新覆盖本地配置文件
        cout << GREEN << "\n[成功] 规则已删除，相关端口及 UFW 策略已全自动清理。" << RESET << endl;
    }

    // 美化输出：以表格形式打印当前所有的转发规则
    void list_rules() {
        cout << "=================================== 当前转发列表 ===================================" << endl;
        // left + setw 设置左对齐及固定列宽
        cout << left << setw(5) << "ID" << setw(12) << "协议类型" << setw(10) << "本地端口" << setw(25) << "远程目标" << "备注" << endl;
        cout << setfill('-') << setw(85) << "" << setfill(' ') << endl; // 打印分割线
        
        for (int i = 0; i < (int)rules.size(); ++i) {
            string target = rules[i].remote_ip + ":" + to_string(rules[i].remote_port);
            cout << left << GREEN << setw(5) << i << RESET
                 << left << setw(12) << rules[i].proto 
                 << BLUE << setw(10) << rules[i].local_port << RESET
                 << PURPLE << setw(25) << target << RESET
                 << rules[i].comment << endl;
        }
        if(rules.empty()) cout << " (当前无活跃转发规则)" << endl;
        cout << setfill('=') << setw(85) << "" << setfill(' ') << endl;
    }

    // 将内存中的所有规则格式化写入配置文件（以 '|' 作为分隔符）
    void save_to_file() {
        ofstream f(config_file);
        for (const auto& r : rules) {
            f << r.proto << "|" << r.local_port << "|" << r.remote_ip << "|" << r.remote_port << "|" << r.comment << endl;
        }
    }

    // 热重载：清空当前防火墙，重新从规则文件加载
    void reload_rules() {
        cout << "正在使当前内存中的规则从系统防火墙中卸载..." << endl;
        for (const auto& r : rules) {
            apply_network_rules(r, "-D"); // 逐个清理
        }
        rules.clear();

        cout << "正在重新从配置文件加载并应用规则..." << endl;
        ifstream f(config_file);
        string line;
        while (getline(f, line)) {
            if (line.empty()) continue;
            stringstream ss(line);
            ForwardRule r; string temp;
            // 依照 '|' 分隔符解析字段
            getline(ss, r.proto, '|');
            getline(ss, temp, '|'); r.local_port = stoi(temp);
            getline(ss, r.remote_ip, '|');
            getline(ss, temp, '|'); r.remote_port = stoi(temp);
            getline(ss, r.comment, '|');
            
            rules.push_back(r);
            apply_network_rules(r, "-A"); // 解析一条，立刻应用一条
        }
        cout << GREEN << "\n[成功] 规则已重新热载入！" << RESET << endl;
    }

    // 开机自动加载（或初始化读取），逻辑与热重载的“后半段”相同
    void load_from_file() {
        rules.clear();
        ifstream f(config_file);
        string line;
        while (getline(f, line)) {
            if (line.empty()) continue;
            stringstream ss(line);
            ForwardRule r; string temp;
            getline(ss, r.proto, '|');
            getline(ss, temp, '|'); r.local_port = stoi(temp);
            getline(ss, r.remote_ip, '|');
            getline(ss, temp, '|'); r.remote_port = stoi(temp);
            getline(ss, r.comment, '|');
            rules.push_back(r);
            apply_network_rules(r, "-A"); 
        }
    }
};

// === 主程序入口 ===
int main(int argc, char* argv[]) {
    // 权限检查：修改 iptables 必须具备 root (UID 0) 权限
    if (getuid() != 0) {
        cout << RED << "错误: 必须以 root 权限运行 (sudo ./natgo)" << RESET << endl;
        return 1;
    }

    NatManager mgr;
    mgr.init_system(); // 启用内核转发支持

    // 快捷启动支持：若带了 "--load-only" 参数，只在后台默默加载规则，不进入 UI 菜单
    if (argc > 1 && string(argv[1]) == "--load-only") {
        mgr.load_from_file();
        return 0; // 加载完直接退出（适合写进开机自启脚本）
    }

    mgr.load_from_file(); // 默认进入时先从本地加载已有规则
    mgr.enter_alt_screen(); // 开启清爽无污染的备用屏幕界面

    int choice;
    while (true) {
        mgr.clear_canvas(); // 每次循环开始时清理屏幕
        
        // 打印主菜单
        cout << "===== 端口转发管理器 =====" << endl;
        cout << BLUE "1." RESET " 一键添加 TCP + UDP 转发 (推荐)" << endl;
        cout << BLUE "2." RESET " 仅添加 TCP 转发" << endl;
        cout << BLUE "3." RESET " 仅添加 UDP 转发" << endl;
        cout << BLUE "4." RESET " 查看当前转发列表" << endl;
        cout << BLUE "5." RESET " 删除指定转发规则" << endl;
        cout << BLUE "6." RESET " " YELLOW "🔄" RESET " 重新载入规则 (从 rules.conf 热更新)" << endl;
        cout << BLUE "0." RESET " 退出" << endl;
        cout << "请选择操作: ";
        if (!(cin >> choice)) break; // 捕获非法输入（如误输字母）则直接中止

        mgr.clear_canvas(); 

        // 处理添加转发规则 (选项 1, 2, 3)
        if (choice >= 1 && choice <= 3) {
            int lp, rp; string ip, cmt;
            cout << "===== 配置转发目标 =====" << endl;
            cout << "(提示: 输入 " RED "-1" RESET " 可随时取消并安全返回)" << endl;
            cout << "本地监听端口: "; cin >> lp;
            
            // 友好的随时取消机制
            if (lp == -1) {
                cout << YELLOW << "\n[已取消] 操作已中止，正在返回菜单..." << RESET << endl;
                usleep(400000); // 略微延时 400 毫秒提供视觉过渡
                continue; 
            }

            cout << "远程目标 IP: "; cin >> ip;
            cout << "远程目标端口: "; cin >> rp;
            cout << "备注名: "; cin.ignore(); getline(cin, cmt); // 吸收回车后读取带空格的整行备注

            // 根据菜单选择分发不同的协议入参
            if (choice == 1) mgr.add_rule("tcp+udp", lp, ip, rp, cmt);
            else if (choice == 2) mgr.add_rule("tcp", lp, ip, rp, cmt);
            else if (choice == 3) mgr.add_rule("udp", lp, ip, rp, cmt);
            
            mgr.wait_for_keypress(); 
        } 
        // 选项 4: 列表展示
        else if (choice == 4) {
            mgr.list_rules();        
            mgr.wait_for_keypress(); 
        } 
        // 选项 5: 规则删除
        else if (choice == 5) {
            mgr.list_rules();        
            if (!cin.eof()) {
                int id; 
                cout << "输入要删除的 ID (输入 " RED "-1" RESET " 取消并返回): "; 
                cin >> id;
                
                if (id == -1) {
                    cout << YELLOW << "\n[已取消] 操作已中止，正在返回菜单..." << RESET << endl;
                    usleep(400000);
                    continue; 
                }
                
                mgr.remove_rule(id);
                mgr.wait_for_keypress();
            }
        } 
        // 选项 6: 规则配置文件强制同步覆盖
        else if (choice == 6) {
            mgr.reload_rules();
            mgr.wait_for_keypress();
        } 
        // 选项 0: 退出系统
        else if (choice == 0) {
            break;
        }
    }

    mgr.exit_alt_screen(); // 退出前关闭备用屏幕，恢复用户本来的终端命令历史
    return 0;
}