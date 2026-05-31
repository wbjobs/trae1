rule Packer_UPX_001
{
    meta:
        description = "检测UPX压缩壳 - 通用可执行文件压缩工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $upx1 = "UPX!" ascii
        $upx2 = "UPX0" ascii
        $upx3 = "UPX1" ascii
    condition:
        any of them
}

rule Packer_ASPack_001
{
    meta:
        description = "检测ASPack壳 - 可执行文件压缩保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $aspack = "ASPack" ascii
        $aspack2 = ".aspack" ascii
    condition:
        any of them
}

rule Packer_PECompact_001
{
    meta:
        description = "检测PECompact壳 - PE文件压缩工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $pec1 = "PEC2" ascii
        $pec2 = "PECompact" ascii
    condition:
        any of them
}

rule Packer_NSIS_001
{
    meta:
        description = "检测NSIS安装包 - Nullsoft脚本安装系统"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $nsis = "Nullsoft" ascii
        $nsis2 = "NSIS" ascii
    condition:
        any of them
}

rule Packer_Themida_001
{
    meta:
        description = "检测Themida保护壳 - 高级软件保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $themida = "Themida" ascii
        $themida2 = "WinLicense" ascii
    condition:
        any of them
}

rule Packer_VMProtect_001
{
    meta:
        description = "检测VMProtect保护壳 - 虚拟化保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $vmp = "VMP0" ascii
        $vmp2 = "VMProtect" ascii
    condition:
        any of them
}

rule Packer_Armadillo_001
{
    meta:
        description = "检测Armadillo保护壳 - 软件保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $arm = "Armadillo" ascii
        $arm2 = ".armadillo" ascii
    condition:
        any of them
}

rule Packer_ASProtect_001
{
    meta:
        description = "检测ASProtect保护壳 - 软件保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $asp = "ASProtect" ascii
        $asp2 = ".aspack" ascii
    condition:
        any of them
}

rule Packer_PELock_001
{
    meta:
        description = "检测PELock保护壳 - 可执行文件保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $pel = "PELock" ascii
        $pel2 = ".pelock" ascii
    condition:
        any of them
}

rule Packer_MPRESS_001
{
    meta:
        description = "检测MPRESS壳 - 免费可执行文件压缩工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $mpress = "MPRESS" ascii
        $mpress2 = ".MPRESS1" ascii
        $mpress3 = ".MPRESS2" ascii
    condition:
        any of them
}

rule Packer_eXPressor_001
{
    meta:
        description = "检测eXPressor保护壳"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $exp = "eXPressor" ascii
        $exp2 = ".expressor" ascii
    condition:
        any of them
}

rule Packer_CrypKey_001
{
    meta:
        description = "检测CrypKey保护壳 - 软件许可保护"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $ck = "CrypKey" ascii
        $ck2 = ".crypkey" ascii
    condition:
        any of them
}

rule Packer_SmartAssembly_001
{
    meta:
        description = "检测SmartAssembly - .NET混淆保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $sa = "SmartAssembly" ascii
        $sa2 = ".smartassembly" ascii
    condition:
        any of them
}

rule Packer_Dotfuscator_001
{
    meta:
        description = "检测Dotfuscator - .NET混淆保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $dot = "Dotfuscator" ascii
        $dot2 = "PreEmptive" ascii
    condition:
        any of them
}

rule Packer_Confuser_001
{
    meta:
        description = "检测Confuser - .NET混淆保护工具"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $conf = "Confuser" ascii
        $conf2 = "ConfuserEx" ascii
    condition:
        any of them
}

rule Packer_PyInstaller_001
{
    meta:
        description = "检测PyInstaller打包的Python程序"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $pyi = "PYZ-00.pyz" ascii
        $pyi2 = "pyiboot" ascii
        $pyi3 = "pyi_rth" ascii
    condition:
        any of them
}

rule Packer_nuitka_001
{
    meta:
        description = "检测Nuitka编译的Python程序"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $nuitka = "NUITKA" ascii
        $nuitka2 = "__nuitka" ascii
    condition:
        any of them
}

rule Packer_GoCompiler_001
{
    meta:
        description = "检测Go编译器生成的可执行文件"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $go = "GODEBUG" ascii
        $go2 = "runtime.goexit" ascii
        $go3 = "GOROOT" ascii
    condition:
        any of them
}

rule Packer_RustCompiler_001
{
    meta:
        description = "检测Rust编译器生成的可执行文件"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $rust = ".rustc" ascii
        $rust2 = "rust_eh" ascii
        $rust3 = "RUST_BACKTRACE" ascii
    condition:
        any of them
}

rule Packer_InstallShield_001
{
    meta:
        description = "检测InstallShield安装包"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $ish = "InstallShield" ascii
        $ish2 = "IsUninst" ascii
    condition:
        any of them
}

rule Packer_InnoSetup_001
{
    meta:
        description = "检测Inno Setup安装包"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $inno = "Inno Setup" ascii
        $inno2 = "This installation was built with Inno Setup" ascii
    condition:
        any of them
}

rule Packer_WiX_001
{
    meta:
        description = "检测WiX Toolset安装包"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $wix = "WiX" ascii
        $wix2 = "burn.exe" ascii
    condition:
        any of them
}

rule Packer_7zSFX_001
{
    meta:
        description = "检测7z自解压存档"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $7z = "7-Zip" ascii
        $7z2 = "7zSFX" ascii
    condition:
        any of them
}

rule Packer_WinRAR_001
{
    meta:
        description = "检测WinRAR自解压存档"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "low"
    strings:
        $rar = "WinRAR" ascii
        $rar2 = "SFX" ascii
    condition:
        any of them
}

rule Packer_Suspicious_Generic_001
{
    meta:
        description = "检测可疑的打包特征 - 通用壳检测"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "medium"
    strings:
        $pack1 = { C3 CC CC CC CC CC CC CC CC CC CC CC CC CC CC }
        $pack2 = ".packed" ascii
        $pack3 = ".crypted" ascii
        $overlay = { 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 }
    condition:
        ($pack1 or $pack2 or $pack3) and uint16(0) == 0x5A4D
}
