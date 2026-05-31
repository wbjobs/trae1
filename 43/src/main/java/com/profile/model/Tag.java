package com.profile.model;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public enum Tag {

    DIGITAL_LOVER("digital_lover",
            new HashSet<>(Arrays.asList("手机", "数码", "电脑", "相机", "耳机", "智能设备")),
            1, 2, 5, 10, 0.95),

    MATERNITY_USER("maternity_user",
            new HashSet<>(Arrays.asList("母婴", "奶粉", "纸尿裤", "童装", "玩具", "孕产")),
            1, 2, 5, 10, 0.95),

    HIGH_SPENDER("high_spender", null, 1, 2, 5, 10, 0.97),

    NIGHT_ACTIVE("night_active", null, 1, 1, 1, 1, 0.98),

    FASHION_SENSITIVE("fashion_sensitive",
            new HashSet<>(Arrays.asList("服装", "鞋靴", "箱包", "配饰", "美妆", "珠宝")),
            1, 2, 5, 10, 0.95),

    FOOD_LOVER("food_lover",
            new HashSet<>(Arrays.asList("食品", "生鲜", "零食", "饮料", "粮油", "酒水")),
            1, 2, 5, 10, 0.95),

    HOME_IMPROVER("home_improver",
            new HashSet<>(Arrays.asList("家居", "家纺", "家装", "厨具", "家具", "灯具")),
            1, 2, 5, 10, 0.95),

    SPORTS_FAN("sports_fan",
            new HashSet<>(Arrays.asList("运动", "户外", "健身", "骑行", "垂钓")),
            1, 2, 5, 10, 0.95),

    BOOK_WORM("book_worm",
            new HashSet<>(Arrays.asList("图书", "文具", "电子书", "教育")),
            1, 2, 5, 10, 0.95),

    CAR_OWNER("car_owner",
            new HashSet<>(Arrays.asList("汽车", "车品", "维修", "保养", "车载")),
            1, 2, 5, 10, 0.95),

    PET_OWNER("pet_owner",
            new HashSet<>(Arrays.asList("宠物", "狗粮", "猫粮", "玩具", "用品")),
            1, 2, 5, 10, 0.95),

    BARGAIN_HUNTER("bargain_hunter", null, 1, 2, 5, 10, 0.96),

    IMPULSIVE_BUYER("impulsive_buyer", null, 1, 2, 5, 10, 0.97),

    EARLY_BIRD("early_bird", null, 1, 1, 1, 1, 0.98),

    WEEKEND_SHOPPER("weekend_shopper", null, 1, 1, 1, 1, 0.98);

    private final String code;
    private final Set<String> categories;
    private final double viewScore;
    private final double clickScore;
    private final double favoriteScore;
    private final double purchaseScore;
    private final double weeklyDecayFactor;

    Tag(String code, Set<String> categories, double viewScore, double clickScore,
        double favoriteScore, double purchaseScore, double weeklyDecayFactor) {
        this.code = code;
        this.categories = categories;
        this.viewScore = viewScore;
        this.clickScore = clickScore;
        this.favoriteScore = favoriteScore;
        this.purchaseScore = purchaseScore;
        this.weeklyDecayFactor = weeklyDecayFactor;
    }

    public String getCode() { return code; }
    public Set<String> getCategories() { return categories; }
    public double getViewScore() { return viewScore; }
    public double getClickScore() { return clickScore; }
    public double getFavoriteScore() { return favoriteScore; }
    public double getPurchaseScore() { return purchaseScore; }
    public double getWeeklyDecayFactor() { return weeklyDecayFactor; }

    public boolean matchesCategory(String category) {
        if (categories == null) return true;
        return categories.contains(category);
    }

    public double scoreForAction(ActionType action) {
        if (action == null) return 0;
        switch (action) {
            case VIEW: return viewScore;
            case CLICK: return clickScore;
            case FAVORITE: return favoriteScore;
            case PURCHASE: return purchaseScore;
            default: return 0;
        }
    }
}
