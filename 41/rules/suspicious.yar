rule Suspicious_Behavior_001
{
    meta:
        description = "检测可疑行为 - 修改系统文件和目录权限"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $chmod = "chmod 777" ascii
        $attrib = "attrib +h" ascii
        $icacls = "icacls" ascii
    condition:
        $chmod or $attrib or $icacls
}

rule Suspicious_Behavior_002
{
    meta:
        description = "检测可疑行为 - 关闭安全软件和防火墙"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $av1 = "Stop-Service" ascii
        $av2 = "sc stop" ascii
        $firewall = "netsh firewall" ascii
        $disable = "disable" ascii nocase
    condition:
        ($av1 or $av2) and ($firewall and $disable)
}

rule Suspicious_Behavior_003
{
    meta:
        description = "检测可疑行为 - 批量文件操作和删除"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $del = "del /f /q" ascii
        $rm = "rm -rf" ascii
        $format = "format c:" ascii nocase
    condition:
        $del or $rm or $format
}

rule Suspicious_Behavior_004
{
    meta:
        description = "检测可疑行为 - 系统信息收集和枚举"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $sys1 = "systeminfo" ascii nocase
        $sys2 = "whoami" ascii nocase
        $sys3 = "ipconfig" ascii nocase
        $sys4 = "net user" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_005
{
    meta:
        description = "检测可疑行为 - 进程和服务操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $proc1 = "taskkill" ascii nocase
        $proc2 = "tasklist" ascii nocase
        $proc3 = "net start" ascii nocase
        $proc4 = "net stop" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_006
{
    meta:
        description = "检测可疑行为 - 网络连接和端口扫描"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $net1 = "netstat -an" ascii nocase
        $net2 = "net use" ascii nocase
        $net3 = "ping -n" ascii nocase
        $scan = "nmap" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_007
{
    meta:
        description = "检测可疑行为 - 注册表操作和修改"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $reg1 = "reg add" ascii nocase
        $reg2 = "reg delete" ascii nocase
        $reg3 = "reg export" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_008
{
    meta:
        description = "检测可疑行为 - 远程桌面和WMI操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $rdp = "mstsc" ascii nocase
        $wmi = "wmic" ascii nocase
        $psexec = "psexec" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_009
{
    meta:
        description = "检测可疑行为 - 文件下载和上传"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $dl1 = "curl" ascii nocase
        $dl2 = "wget" ascii nocase
        $dl3 = "Invoke-WebRequest" ascii
        $upload = "ftp" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_010
{
    meta:
        description = "检测可疑行为 - 用户账户和权限操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $user1 = "net user" ascii nocase
        $user2 = "net localgroup administrators" ascii nocase
        $user3 = "runas" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_011
{
    meta:
        description = "检测可疑行为 - 服务和驱动操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $svc1 = "sc config" ascii nocase
        $svc2 = "sc query" ascii nocase
        $svc3 = "driverquery" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_012
{
    meta:
        description = "检测可疑行为 - 计划任务和启动项"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $task1 = "schtasks" ascii nocase
        $task2 = "at " ascii
        $startup = "startup" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_013
{
    meta:
        description = "检测可疑行为 - 环境变量和路径修改"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $env1 = "set PATH=" ascii nocase
        $env2 = "export PATH=" ascii nocase
        $env3 = "$env:" ascii
    condition:
        any of them
}

rule Suspicious_Behavior_014
{
    meta:
        description = "检测可疑行为 - 文件系统监控和变更"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $fs1 = "FileSystemWatcher" ascii
        $fs2 = "ReadDirectoryChanges" ascii
        $fs3 = "FindFirstChangeNotification" ascii
    condition:
        any of them
}

rule Suspicious_Behavior_015
{
    meta:
        description = "检测可疑行为 - 网络代理和转发"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $proxy1 = "socks" ascii nocase
        $proxy2 = "port forwarding" ascii nocase
        $proxy3 = "tunnel" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_016
{
    meta:
        description = "检测可疑行为 - 系统时间和日志操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $time = "date " ascii nocase
        $log1 = "wevtutil" ascii nocase
        $log2 = "Clear-EventLog" ascii
    condition:
        any of them
}

rule Suspicious_Behavior_017
{
    meta:
        description = "检测可疑行为 - 加密和解密操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $enc1 = "AES" ascii
        $enc2 = "DES" ascii
        $enc3 = "RSA" ascii
        $enc4 = "base64" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_018
{
    meta:
        description = "检测可疑行为 - 压缩和解压操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $zip1 = ".zip" ascii
        $zip2 = ".rar" ascii
        $zip3 = "7z" ascii nocase
        $tar = ".tar" ascii
    condition:
        any of them
}

rule Suspicious_Behavior_019
{
    meta:
        description = "检测可疑行为 - 进程注入和DLL加载"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $dll1 = "LoadLibrary" ascii
        $dll2 = "GetProcAddress" ascii
        $inject = "CreateRemoteThread" ascii
    condition:
        ($dll1 and $dll2) or $inject
}

rule Suspicious_Behavior_020
{
    meta:
        description = "检测可疑行为 - 反调试和反虚拟机"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $db1 = "IsDebuggerPresent" ascii
        $db2 = "CheckRemoteDebuggerPresent" ascii
        $vm = "VirtualBox" ascii
    condition:
        any of them
}

rule Suspicious_Behavior_021
{
    meta:
        description = "检测可疑行为 - 系统配置修改"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $cfg1 = "msconfig" ascii nocase
        $cfg2 = "regedit" ascii nocase
        $cfg3 = "gpedit" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_022
{
    meta:
        description = "检测可疑行为 - 网络嗅探和抓包"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $sniff1 = "winpcap" ascii nocase
        $sniff2 = "npcap" ascii nocase
        $sniff3 = "tcpdump" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_023
{
    meta:
        description = "检测可疑行为 - 自动化工具和脚本"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $auto1 = "AutoHotkey" ascii
        $auto2 = "AutoIt" ascii
        $auto3 = "python" ascii nocase
        $auto4 = "powershell -enc" ascii
    condition:
        any of them
}

rule Suspicious_Behavior_024
{
    meta:
        description = "检测可疑行为 - 文件关联和扩展名修改"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $ext1 = ".exe" ascii
        $ext2 = ".bat" ascii
        $ext3 = ".cmd" ascii
        $assoc = "assoc" ascii nocase
    condition:
        any of them
}

rule Suspicious_Behavior_025
{
    meta:
        description = "检测可疑行为 - 系统恢复和备份操作"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $bk1 = "vssadmin" ascii nocase
        $bk2 = "wbadmin" ascii nocase
        $bk3 = "bcdedit" ascii nocase
    condition:
        any of them
}
