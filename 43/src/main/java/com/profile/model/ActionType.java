package com.profile.model;

public enum ActionType {
    VIEW("view"),
    CLICK("click"),
    FAVORITE("favorite"),
    PURCHASE("purchase");

    private final String code;

    ActionType(String code) {
        this.code = code;
    }

    public String getCode() { return code; }

    public static ActionType fromCode(String code) {
        for (ActionType type : values()) {
            if (type.code.equalsIgnoreCase(code)) {
                return type;
            }
        }
        return null;
    }
}
