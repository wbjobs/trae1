rule Ransomware_Generic_001
{
    meta:
        description = "检测勒索软件通用特征 - 文件加密和勒索信息"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $encrypt1 = "CryptGenKey" ascii
        $encrypt2 = "CryptEncrypt" ascii
        $ransom1 = "Your files have been encrypted" ascii nocase
        $ransom2 = "send" ascii nocase
        $btc = "bitcoin" ascii nocase
    condition:
        ($encrypt1 or $encrypt2) and ($ransom1 or ($ransom2 and $btc))
}

rule Ransomware_WannaCry_001
{
    meta:
        description = "检测WannaCry勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $wn1 = "WanaDecryptor" ascii
        $wn2 = ".WNCRY" ascii
        $wn3 = "WannaCry" ascii nocase
    condition:
        any of them
}

rule Ransomware_Locky_001
{
    meta:
        description = "检测Locky勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $locky1 = ".locky" ascii
        $locky2 = "Locky" ascii nocase
        $locky3 = "_Locky_recover" ascii
    condition:
        any of them
}

rule Ransomware_Cerber_001
{
    meta:
        description = "检测Cerber勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $cerb1 = ".cerber" ascii
        $cerb2 = "Cerber" ascii nocase
        $cerb3 = "# DECRYPT MY FILES #" ascii
    condition:
        any of them
}

rule Ransomware_CryptoLocker_001
{
    meta:
        description = "检测CryptoLocker勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $cl1 = "CryptoLocker" ascii nocase
        $cl2 = ".encrypted" ascii
        $cl3 = "DECRYPT_INSTRUCTION" ascii
    condition:
        any of them
}

rule Ransomware_Petya_001
{
    meta:
        description = "检测Petya/NotPetya勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $petya1 = "Petya" ascii nocase
        $petya2 = ".petya" ascii
        $petya3 = "Misha" ascii
    condition:
        any of them
}

rule Ransomware_Ryuk_001
{
    meta:
        description = "检测Ryuk勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $ryuk1 = "Ryuk" ascii nocase
        $ryuk2 = ".ryk" ascii
        $ryuk3 = "RyukReadMe" ascii
    condition:
        any of them
}

rule Ransomware_Emotet_001
{
    meta:
        description = "检测Emotet恶意软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $emo1 = "Emotet" ascii nocase
        $emo2 = ".emotet" ascii
    condition:
        any of them
}

rule Ransomware_TrickBot_001
{
    meta:
        description = "检测TrickBot木马特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "high"
    strings:
        $tb1 = "TrickBot" ascii nocase
        $tb2 = ".trickbot" ascii
        $tb3 = "TrickLoader" ascii
    condition:
        any of them
}

rule Ransomware_Conti_001
{
    meta:
        description = "检测Conti勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $conti1 = "Conti" ascii nocase
        $conti2 = ".conti" ascii
        $conti3 = "CONTI_" ascii
    condition:
        any of them
}

rule Ransomware_REvil_001
{
    meta:
        description = "检测REvil/Sodinokibi勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $rev1 = "REvil" ascii nocase
        $rev2 = ".sodinokibi" ascii
        $rev3 = "Sodinokibi" ascii nocase
    condition:
        any of them
}

rule Ransomware_DarkSide_001
{
    meta:
        description = "检测DarkSide勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $ds1 = "DarkSide" ascii nocase
        $ds2 = ".darkside" ascii
        $ds3 = "Readme.darkside" ascii
    condition:
        any of them
}

rule Ransomware_BlackMatter_001
{
    meta:
        description = "检测BlackMatter勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $bm1 = "BlackMatter" ascii nocase
        $bm2 = ".blackmatter" ascii
        $bm3 = "BlackMatter.txt" ascii
    condition:
        any of them
}

rule Ransomware_LockBit_001
{
    meta:
        description = "检测LockBit勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $lb1 = "LockBit" ascii nocase
        $lb2 = ".lockbit" ascii
        $lb3 = "Restore-My-Files" ascii
    condition:
        any of them
}

rule Ransomware_Hive_001
{
    meta:
        description = "检测Hive勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $hive1 = "Hive" ascii nocase
        $hive2 = ".hive" ascii
        $hive3 = "HOW_TO_DECRYPT" ascii
    condition:
        any of them
}

rule Ransomware_Akira_001
{
    meta:
        description = "检测Akira勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $akira1 = "Akira" ascii nocase
        $akira2 = ".akira" ascii
    condition:
        any of them
}

rule Ransomware_Cactus_001
{
    meta:
        description = "检测Cactus勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $cactus1 = "Cactus" ascii nocase
        $cactus2 = ".cactus" ascii
        $cactus3 = "README.Cactus.txt" ascii
    condition:
        any of them
}

rule Ransomware_Clop_001
{
    meta:
        description = "检测Clop勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $clop1 = "Clop" ascii nocase
        $clop2 = ".clop" ascii
        $clop3 = "CLOP_README" ascii
    condition:
        any of them
}

rule Ransomware_Phobos_001
{
    meta:
        description = "检测Phobos勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $pho1 = "Phobos" ascii nocase
        $pho2 = ".phobos" ascii
        $pho3 = "info.txt" ascii
    condition:
        any of them
}

rule Ransomware_GandCrab_001
{
    meta:
        description = "检测GandCrab勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $gc1 = "GandCrab" ascii nocase
        $gc2 = ".gandcrab" ascii
        $gc3 = "GDCB_README" ascii
    condition:
        any of them
}

rule Ransomware_Maze_001
{
    meta:
        description = "检测Maze勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $maze1 = "Maze" ascii nocase
        $maze2 = ".maze" ascii
        $maze3 = "DECRYPT-FILES.txt" ascii
    condition:
        any of them
}

rule Ransomware_Netwalker_001
{
    meta:
        description = "检测Netwalker勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $nw1 = "Netwalker" ascii nocase
        $nw2 = ".walker" ascii
        $nw3 = "NetWalkerReadMe" ascii
    condition:
        any of them
}

rule Ransomware_SamSam_001
{
    meta:
        description = "检测SamSam勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $sam1 = "SamSam" ascii nocase
        $sam2 = ".samsam" ascii
        $sam3 = "HELP_TO_DECRYPT_YOUR_FILES" ascii
    condition:
        any of them
}

rule Ransomware_RobbinHood_001
{
    meta:
        description = "检测RobbinHood勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $rh1 = "RobbinHood" ascii nocase
        $rh2 = "_robbinhood_" ascii
        $rh3 = "robbinhood_readme" ascii
    condition:
        any of them
}

rule Ransomware_DoppelPaymer_001
{
    meta:
        description = "检测DoppelPaymer勒索软件特征"
        author = "ScannerService"
        date = "2024-01-01"
        severity = "critical"
    strings:
        $dp1 = "DoppelPaymer" ascii nocase
        $dp2 = ".DoppelPaymer" ascii
        $dp3 = "README_FOR_DECRYPT.txt" ascii
    condition:
        any of them
}
