package com.quant.gateway.config;

import java.util.*;

public final class SectorMapping {

    private static final Map<String, List<String>> SECTOR_MEMBERS = new HashMap<>();
    private static final Map<String, String> SYMBOL_TO_SECTOR = new HashMap<>();

    static {
        register("bank",       "600519.SH", "601398.SH", "601288.SH", "600036.SH");
        register("tech",       "300750.SZ", "002415.SZ", "688981.SH", "000858.SZ");
        register("finance",    "601628.SH", "601318.SH", "600030.SH");
        register("consumer",   "600887.SH", "000568.SZ", "000651.SZ");
        register("energy",     "601857.SH", "600028.SH", "601985.SH");
    }

    private static void register(String sector, String... symbols) {
        List<String> list = new ArrayList<>(Arrays.asList(symbols));
        SECTOR_MEMBERS.put(sector, list);
        for (String s : symbols) {
            SYMBOL_TO_SECTOR.put(s, sector);
        }
    }

    public static String sectorOf(String symbol) {
        return SYMBOL_TO_SECTOR.getOrDefault(symbol, "default");
    }

    public static List<String> symbolsOf(String sector) {
        return SECTOR_MEMBERS.getOrDefault(sector, Collections.emptyList());
    }

    public static Set<String> allSectors() {
        return SECTOR_MEMBERS.keySet();
    }

    public static Set<String> allSymbols() {
        return SYMBOL_TO_SECTOR.keySet();
    }

    private SectorMapping() {}
}
