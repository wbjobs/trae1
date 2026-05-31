rule Trojan_Emotet_001
{
    meta:
        description = "检测Emotet木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $emo1 = "Emotet" ascii nocase
        $emo2 = "emotet" ascii
        $emo3 = "EMOTET" ascii
    condition:
        any of them
}

rule Trojan_Zeus_001
{
    meta:
        description = "检测Zeus/Zbot木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $zeus1 = "Zeus" ascii nocase
        $zeus2 = "Zbot" ascii nocase
        $zeus3 = "ntos\\local" ascii
    condition:
        any of them
}

rule Trojan_SpyEye_001
{
    meta:
        description = "检测SpyEye木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $spy1 = "SpyEye" ascii nocase
        $spy2 = "spyeye" ascii
    condition:
        any of them
}

rule Trojan_TrickBot_001
{
    meta:
        description = "检测TrickBot木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $tb1 = "TrickBot" ascii nocase
        $tb2 = "trickbot" ascii
        $tb3 = "TrickLoader" ascii
    condition:
        any of them
}

rule Trojan_Dridex_001
{
    meta:
        description = "检测Dridex木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $dr1 = "Dridex" ascii nocase
        $dr2 = "dridex" ascii
        $dr3 = "Cridex" ascii nocase
    condition:
        any of them
}

rule Trojan_QakBot_001
{
    meta:
        description = "检测QakBot/QBot木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $qak1 = "QakBot" ascii nocase
        $qak2 = "QBot" ascii nocase
        $qak3 = "QuackBot" ascii nocase
    condition:
        any of them
}

rule Trojan_AgentTesla_001
{
    meta:
        description = "检测Agent Tesla木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $at1 = "AgentTesla" ascii nocase
        $at2 = "Agent Tesla" ascii
    condition:
        any of them
}

rule Trojan_Remcos_001
{
    meta:
        description = "检测Remcos RAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $rc1 = "Remcos" ascii nocase
        $rc2 = "remcos" ascii
    condition:
        any of them
}

rule Trojan_NjRAT_001
{
    meta:
        description = "检测NjRAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $nj1 = "NjRAT" ascii nocase
        $nj2 = "njq8" ascii
        $nj3 = "njrat" ascii
    condition:
        any of them
}

rule Trojan_AsyncRAT_001
{
    meta:
        description = "检测AsyncRAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $ar1 = "AsyncRAT" ascii nocase
        $ar2 = "asyncrat" ascii
        $ar3 = "client.ini" ascii
    condition:
        any of them
}

rule Trojan_QuasarRAT_001
{
    meta:
        description = "检测QuasarRAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $qr1 = "Quasar" ascii nocase
        $qr2 = "QuasarRAT" ascii
    condition:
        any of them
}

rule Trojan_AzureRAT_001
{
    meta:
        description = "检测AzureRAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $az1 = "AzureRAT" ascii nocase
        $az2 = "azure_rat" ascii
    condition:
        any of them
}

rule Trojan_PlasmaRAT_001
{
    meta:
        description = "检测Plasma RAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $pl1 = "Plasma" ascii nocase
        $pl2 = "plasma_rat" ascii
    condition:
        any of them
}

rule Trojan_BlackNET_001
{
    meta:
        description = "检测BlackNET RAT木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $bn1 = "BlackNET" ascii nocase
        $bn2 = "blacknet" ascii
    condition:
        any of them
}

rule Trojan_LokiBot_001
{
    meta:
        description = "检测LokiBot木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $lk1 = "LokiBot" ascii nocase
        $lk2 = "lokibot" ascii
        $lk3 = "Loki" ascii fullword
    condition:
        any of them
}

rule Trojan_FormBook_001
{
    meta:
        description = "检测FormBook木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $fb1 = "FormBook" ascii nocase
        $fb2 = "formbook" ascii
        $fb3 = "FORMBOOK" ascii
    condition:
        any of them
}

rule Trojan_Keylogger_Generic_001
{
    meta:
        description = "检测通用键盘记录器特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $kl1 = "GetAsyncKeyState" ascii
        $kl2 = "GetKeyState" ascii
        $kl3 = "SetWindowsHookEx" ascii
        $kl4 = "WH_KEYBOARD_LL" ascii
    condition:
        ($kl1 or $kl2) and ($kl3 or $kl4)
}

rule Trojan_Banking_001
{
    meta:
        description = "检测银行木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $bank1 = "chrome.exe" ascii
        $bank2 = "firefox.exe" ascii
        $bank3 = "iexplore.exe" ascii
        $inject = "CreateRemoteThread" ascii
    condition:
        ($bank1 or $bank2 or $bank3) and $inject
}

rule Trojan_Clipper_001
{
    meta:
        description = "检测剪贴板劫持木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $cp1 = "OpenClipboard" ascii
        $cp2 = "SetClipboardData" ascii
        $cp3 = "GetClipboardData" ascii
        $crypto = "bc1" ascii
    condition:
        ($cp1 and $cp2) or ($cp3 and $crypto)
}

rule Trojan_Miner_001
{
    meta:
        description = "检测加密货币挖矿木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $mn1 = "xmrig" ascii nocase
        $mn2 = "minerd" ascii nocase
        $mn3 = "cpuminer" ascii nocase
        $pool = "stratum+tcp" ascii
    condition:
        any of them
}

rule Trojan_Stealer_001
{
    meta:
        description = "检测信息窃取木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $st1 = "passwords.txt" ascii
        $st2 = "cookies.txt" ascii
        $st3 = "logins.txt" ascii
        $st4 = "Login Data" ascii
    condition:
        any of them
}

rule Trojan_RAT_Generic_001
{
    meta:
        description = "检测通用远程访问木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $rat1 = "Remote Access" ascii nocase
        $rat2 = "remote_access" ascii
        $rat3 = "cmd.exe" ascii
        $rat4 = "powershell.exe" ascii
    condition:
        ($rat1 or $rat2) and ($rat3 or $rat4)
}

rule Trojan_Downloader_001
{
    meta:
        description = "检测恶意下载器特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $dl1 = "URLDownloadToFile" ascii
        $dl2 = "InternetOpen" ascii
        $dl3 = "WinHttpOpen" ascii
        $exe = "WinExec" ascii
    condition:
        ($dl1 or $dl2 or $dl3) and $exe
}

rule Trojan_Dropper_001
{
    meta:
        description = "检测恶意释放器特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $dr1 = "WriteFile" ascii
        $dr2 = "CreateFile" ascii
        $dr3 = ".tmp" ascii
        $drop = ".exe" ascii
    condition:
        ($dr1 and $dr2) and ($dr3 or $drop)
}

rule Trojan_Proxy_001
{
    meta:
        description = "检测代理木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $px1 = "socks5" ascii
        $px2 = "proxy" ascii nocase
        $px3 = "listen" ascii nocase
        $px4 = "forward" ascii nocase
    condition:
        ($px1 and $px2) and ($px3 or $px4)
}
